import QtQuick
import Quickshell
import Quickshell.Io

/*
  System-data collector for the Departure HUD.

  Polls /proc, /sys and a couple of CLI tools every `settings.updateMs`.
  Exposed as live properties — Hud.qml binds against them.
*/
Item {
  id: root
  property var settings: ({})

  readonly property int updateMs: settings.updateMs || 1000
  readonly property string disksToShow: settings.disksToShow || "/,/home"
  readonly property string netInterfaceOverride: settings.netInterface || ""
  readonly property string netMode: settings.netMode || "sum"
  readonly property string gpuScriptPath: settings.gpuScriptPath || "~/.local/bin/gpuinfo.sh"

  // CPU
  property var cpuPercents: []
  property var cpuFreqsGHz: []
  property real cpuAvgPercent: 0
  property real cpuFreqAvgGHz: 0
  property real cpuFreqMaxGHz: 5.0

  // Memory
  property real memUsedPct: 0
  property real memCachePct: 0
  property real swapUsedPct: 0
  property real memUsedGB: 0
  property real memCacheGB: 0
  property real swapUsedGB: 0
  property real memTotalGB: 0
  property real swapTotalGB: 0

  // Network
  property string netInterface: ""
  property real netDownBps: 0
  property real netUpBps: 0

  // Battery
  property bool hasBattery: false
  property real batPercent: 0
  property string batState: "UNKNOWN"
  property real batRateW: 0
  property int batMinutesLeft: 0
  property int batCycles: 0

  // Disks
  property var disks: []

  // Thermals
  property real cpuTempC: 0
  property real gpuTempC: 0
  property real ssdTempC: 0
  property real caseTempC: 0
  property bool hasGpuTemp: false
  property bool hasSsdTemp: false
  property bool hasCaseTemp: false

  // GPU
  property real gpuLoadPct: 0
  property real gpuPowerW: 0
  property int  gpuClockMHz: 0
  property int  gpuClockMaxMHz: 0
  property string gpuName: ""

  // Display
  property bool hasBrightness: false
  property int brightnessPct: 0
  property int nightLightPct: 0

  // Audio
  property int volumePct: 0
  property bool volMuted: false

  // Static / slow-moving
  property string hostName: "host"
  property string userName: "user"
  property string kernelVersion: ""
  property string shellName: ""
  property int uptimeSec: 0
  property var loadAvg: [0, 0, 0]

  property var _prevCpuTotals: ({})
  property var _prevCpuIdles:  ({})
  property var _prevNetRx:     ({})
  property var _prevNetTx:     ({})
  property real _lastNetTickMs: 0

  Timer {
    id: pollTimer
    interval: root.updateMs
    repeat: true
    running: true
    triggeredOnStart: true
    onTriggered: root.poll()
  }

  Timer {
    id: slowTimer
    interval: Math.max(5000, root.updateMs * 5)
    repeat: true
    running: true
    triggeredOnStart: true
    onTriggered: {
      diskProc.running = true
      batCyclesProc.running = true
    }
  }

  function poll() {
    if (!cpuProc.running)        cpuProc.running = true
    if (!memProc.running)        memProc.running = true
    if (!netProc.running)        netProc.running = true
    if (!freqProc.running)       freqProc.running = true
    if (!loadProc.running)       loadProc.running = true
    if (!uptimeProc.running)     uptimeProc.running = true
    if (!battProc.running)       battProc.running = true
    if (!thermProc.running)      thermProc.running = true
    if (!brightnessProc.running) brightnessProc.running = true
    if (!volumeProc.running)     volumeProc.running = true
    if (!gpuProc.running)        gpuProc.running = true
  }

  Component.onCompleted: identityProc.running = true

  Process {
    id: identityProc
    command: ["sh", "-c", "printf '%s\\n%s\\n%s\\n%s\\n' \"$(hostname)\" \"$(id -un)\" \"$(uname -r)\" \"$(basename ${SHELL:-/bin/sh})\""]
    stdout: StdioCollector {
      onStreamFinished: {
        var lines = text.split('\n')
        if (lines.length >= 1 && lines[0]) root.hostName = lines[0].trim()
        if (lines.length >= 2 && lines[1]) root.userName = '@' + lines[1].trim()
        if (lines.length >= 3 && lines[2]) root.kernelVersion = lines[2].trim()
        if (lines.length >= 4 && lines[3]) root.shellName = lines[3].trim()
      }
    }
  }

  Process {
    id: cpuProc
    command: ["cat", "/proc/stat"]
    stdout: StdioCollector { onStreamFinished: root._parseCpu(text) }
  }

  function _parseCpu(text) {
    var lines = text.split('\n')
    var newTotals = {}
    var newIdles = {}
    var loads = []
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i]
      if (!line.startsWith("cpu")) continue
      var parts = line.split(/\s+/)
      var key = parts[0]
      if (key === "cpu") continue
      if (!/^cpu\d+$/.test(key)) continue
      var user   = parseInt(parts[1])  || 0
      var nice   = parseInt(parts[2])  || 0
      var system = parseInt(parts[3])  || 0
      var idle   = parseInt(parts[4])  || 0
      var iowait = parseInt(parts[5])  || 0
      var irq    = parseInt(parts[6])  || 0
      var sirq   = parseInt(parts[7])  || 0
      var steal  = parseInt(parts[8])  || 0
      var total  = user + nice + system + idle + iowait + irq + sirq + steal
      var idleAll = idle + iowait
      var pTotal = _prevCpuTotals[key]
      var pIdle  = _prevCpuIdles[key]
      var pct = 0
      if (pTotal !== undefined && total > pTotal) {
        var dt = total - pTotal
        var di = idleAll - pIdle
        pct = Math.max(0, Math.min(100, (1.0 - di / dt) * 100))
      }
      newTotals[key] = total
      newIdles[key]  = idleAll
      loads.push(pct)
    }
    _prevCpuTotals = newTotals
    _prevCpuIdles  = newIdles
    if (loads.length > 0) {
      cpuPercents = loads
      var sum = 0
      for (var k = 0; k < loads.length; k++) sum += loads[k]
      cpuAvgPercent = sum / loads.length
    }
  }

  Process {
    id: freqProc
    command: ["cat", "/proc/cpuinfo"]
    stdout: StdioCollector { onStreamFinished: root._parseFreq(text) }
  }

  function _parseFreq(text) {
    var lines = text.split('\n')
    var freqs = []
    for (var i = 0; i < lines.length; i++) {
      var m = lines[i].match(/^cpu MHz\s*:\s*([\d.]+)/)
      if (m) freqs.push(parseFloat(m[1]) / 1000.0)
    }
    if (freqs.length > 0) {
      cpuFreqsGHz = freqs
      var sum = 0, mx = 0
      for (var k = 0; k < freqs.length; k++) { sum += freqs[k]; if (freqs[k] > mx) mx = freqs[k] }
      cpuFreqAvgGHz = sum / freqs.length
      if (mx > cpuFreqMaxGHz) cpuFreqMaxGHz = mx
    }
  }

  Process {
    id: memProc
    command: ["cat", "/proc/meminfo"]
    stdout: StdioCollector { onStreamFinished: root._parseMem(text) }
  }

  function _parseMem(text) {
    var info = {}
    var lines = text.split('\n')
    for (var i = 0; i < lines.length; i++) {
      var m = lines[i].match(/^(\w+):\s+(\d+)/)
      if (m) info[m[1]] = parseInt(m[2])
    }
    var totalKb  = info["MemTotal"] || 0
    var freeKb   = info["MemFree"]  || 0
    var availKb  = info["MemAvailable"] || freeKb
    var buffKb   = info["Buffers"]  || 0
    var cacheKb  = info["Cached"]   || 0
    var sReclaim = info["SReclaimable"] || 0
    var usedKb   = totalKb - availKb
    var cacheAllKb = buffKb + cacheKb + sReclaim
    var swapTotalKb = info["SwapTotal"] || 0
    var swapFreeKb  = info["SwapFree"]  || 0
    var swapUsedKb  = swapTotalKb - swapFreeKb

    memTotalGB  = totalKb / 1024 / 1024
    memUsedGB   = usedKb  / 1024 / 1024
    memCacheGB  = cacheAllKb / 1024 / 1024
    swapTotalGB = swapTotalKb / 1024 / 1024
    swapUsedGB  = swapUsedKb  / 1024 / 1024
    memUsedPct  = totalKb > 0 ? (usedKb / totalKb * 100) : 0
    memCachePct = totalKb > 0 ? (cacheAllKb / totalKb * 100) : 0
    swapUsedPct = swapTotalKb > 0 ? (swapUsedKb / swapTotalKb * 100) : 0
  }

  Process {
    id: netProc
    command: ["cat", "/proc/net/dev"]
    stdout: StdioCollector { onStreamFinished: root._parseNet(text) }
  }

  function _parseNet(text) {
    var lines = text.split('\n')
    var nowMs = Date.now()
    var dt = _lastNetTickMs === 0 ? (root.updateMs / 1000.0) : (nowMs - _lastNetTickMs) / 1000.0
    _lastNetTickMs = nowMs

    var newRx = {}, newTx = {}
    var override = (netInterfaceOverride || "").trim()

    for (var i = 0; i < lines.length; i++) {
      var line = lines[i]
      var colon = line.indexOf(":")
      if (colon < 0) continue
      var iface = line.substring(0, colon).trim()
      if (!iface || iface === "lo") continue
      var fields = line.substring(colon + 1).trim().split(/\s+/)
      if (fields.length < 10) continue
      var rx = parseFloat(fields[0])
      var tx = parseFloat(fields[8])
      if (!isFinite(rx) || !isFinite(tx)) continue
      newRx[iface] = rx
      newTx[iface] = tx
    }

    var haveBaseline = false
    for (var k in _prevNetRx) { haveBaseline = true; break }
    if (!haveBaseline || dt <= 0) {
      _prevNetRx = newRx; _prevNetTx = newTx
      netInterface = override ? override : (function () {
        var best = "", bestSum = -1
        for (var n in newRx) { var s = (newRx[n] || 0) + (newTx[n] || 0); if (s > bestSum) { bestSum = s; best = n } }
        return best
      })()
      return
    }

    var totalDownB = 0, totalUpB = 0
    var bestIface = "", bestDelta = -1
    for (var n in newRx) {
      var dRx = Math.max(0, newRx[n] - (_prevNetRx[n] !== undefined ? _prevNetRx[n] : newRx[n]))
      var dTx = Math.max(0, newTx[n] - (_prevNetTx[n] !== undefined ? _prevNetTx[n] : newTx[n]))
      totalDownB += dRx
      totalUpB   += dTx
      var d = dRx + dTx
      if (d > bestDelta) { bestDelta = d; bestIface = n }
    }

    var pickName = ""
    var dRxPick = 0, dTxPick = 0
    if (override && newRx[override] !== undefined) {
      pickName = override
      dRxPick = Math.max(0, newRx[override] - (_prevNetRx[override] || newRx[override]))
      dTxPick = Math.max(0, newTx[override] - (_prevNetTx[override] || newTx[override]))
    } else if (netMode === "sum") {
      pickName = bestIface
      dRxPick = totalDownB
      dTxPick = totalUpB
    } else {
      pickName = bestIface
      dRxPick = Math.max(0, (newRx[bestIface] || 0) - (_prevNetRx[bestIface] || newRx[bestIface] || 0))
      dTxPick = Math.max(0, (newTx[bestIface] || 0) - (_prevNetTx[bestIface] || newTx[bestIface] || 0))
    }

    netDownBps = dRxPick / dt
    netUpBps   = dTxPick / dt

    _prevNetRx = newRx
    _prevNetTx = newTx
    netInterface = pickName + (netMode === "sum" && !override ? " (sum)" : "")
  }

  Process {
    id: loadProc
    command: ["cat", "/proc/loadavg"]
    stdout: StdioCollector {
      onStreamFinished: {
        var p = text.trim().split(/\s+/)
        if (p.length >= 3) root.loadAvg = [parseFloat(p[0]), parseFloat(p[1]), parseFloat(p[2])]
      }
    }
  }

  Process {
    id: uptimeProc
    command: ["cat", "/proc/uptime"]
    stdout: StdioCollector {
      onStreamFinished: {
        var p = text.trim().split(/\s+/)
        if (p.length >= 1) root.uptimeSec = Math.floor(parseFloat(p[0]))
      }
    }
  }

  Process {
    id: battProc
    command: ["sh", "-c",
      "for f in /sys/class/power_supply/BAT*/uevent; do [ -f \"$f\" ] && cat \"$f\" && break; done"]
    stdout: StdioCollector { onStreamFinished: root._parseBatt(text) }
  }

  function _parseBatt(text) {
    if (!text || text.trim().length === 0) { hasBattery = false; return }
    var info = {}
    var lines = text.split('\n')
    for (var i = 0; i < lines.length; i++) {
      var eq = lines[i].indexOf("=")
      if (eq > 0) info[lines[i].substring(0, eq)] = lines[i].substring(eq + 1).trim()
    }
    if (!info["POWER_SUPPLY_PRESENT"] || info["POWER_SUPPLY_PRESENT"] === "0") {
      hasBattery = false; return
    }
    hasBattery = true
    var capacity = parseFloat(info["POWER_SUPPLY_CAPACITY"] || "0")
    batPercent = capacity
    batState = (info["POWER_SUPPLY_STATUS"] || "UNKNOWN").toUpperCase()

    var rateW = 0
    if (info["POWER_SUPPLY_POWER_NOW"]) {
      rateW = parseFloat(info["POWER_SUPPLY_POWER_NOW"]) / 1e6
    } else if (info["POWER_SUPPLY_CURRENT_NOW"] && info["POWER_SUPPLY_VOLTAGE_NOW"]) {
      var I = parseFloat(info["POWER_SUPPLY_CURRENT_NOW"]) / 1e6
      var V = parseFloat(info["POWER_SUPPLY_VOLTAGE_NOW"]) / 1e6
      rateW = I * V
    }
    if (batState === "DISCHARGING") rateW = -Math.abs(rateW)
    else if (batState === "CHARGING") rateW = Math.abs(rateW)
    batRateW = rateW

    var fullUah = parseFloat(info["POWER_SUPPLY_CHARGE_FULL"] || info["POWER_SUPPLY_ENERGY_FULL"] || "0")
    var nowUah  = parseFloat(info["POWER_SUPPLY_CHARGE_NOW"]  || info["POWER_SUPPLY_ENERGY_NOW"]  || "0")
    var rate    = parseFloat(info["POWER_SUPPLY_CURRENT_NOW"] || info["POWER_SUPPLY_POWER_NOW"]   || "0")
    if (rate > 0 && nowUah > 0) {
      var hours
      if (batState === "DISCHARGING")     hours = nowUah / rate
      else if (batState === "CHARGING")   hours = (fullUah - nowUah) / rate
      else                                hours = 0
      batMinutesLeft = Math.max(0, Math.round(hours * 60))
    } else {
      batMinutesLeft = 0
    }
  }

  Process {
    id: batCyclesProc
    command: ["sh", "-c",
      "for f in /sys/class/power_supply/BAT*/cycle_count; do [ -f \"$f\" ] && cat \"$f\" && break; done"]
    stdout: StdioCollector {
      onStreamFinished: {
        var v = parseInt(text.trim())
        if (!isNaN(v)) root.batCycles = v
      }
    }
  }

  Process {
    id: diskProc
    command: ["sh", "-c",
      "df -P -B1 " + _mountArgs() + " 2>/dev/null | tail -n +2"]
    stdout: StdioCollector { onStreamFinished: root._parseDisks(text) }
  }

  function _mountArgs() {
    var parts = (root.disksToShow || "/").split(",")
    var out = []
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i].trim()
      if (p.length > 0) out.push("'" + p.replace(/'/g, "'\\''") + "'")
    }
    return out.join(" ")
  }

  function _parseDisks(text) {
    var lines = text.split('\n')
    var out = []
    for (var i = 0; i < lines.length; i++) {
      var ln = lines[i].trim()
      if (!ln) continue
      var f = ln.split(/\s+/)
      if (f.length < 6) continue
      var total = parseInt(f[1])
      var used  = parseInt(f[2])
      var mount = f[5]
      if (total > 0) {
        out.push({
          mount: mount,
          usedPct: used / total * 100,
          usedGB:  used  / 1024 / 1024 / 1024,
          totalGB: total / 1024 / 1024 / 1024
        })
      }
    }
    if (out.length > 0) root.disks = out
  }

  Process {
    id: thermProc
    command: ["sh", "-c",
      "for d in /sys/class/hwmon/hwmon*; do n=$(cat \"$d/name\" 2>/dev/null); for t in \"$d\"/temp*_input; do " +
      "  [ -f \"$t\" ] || continue; lbl=$(cat \"${t%_input}_label\" 2>/dev/null); " +
      "  v=$(cat \"$t\" 2>/dev/null); echo \"$n|$lbl|$v\"; done; done"]
    stdout: StdioCollector { onStreamFinished: root._parseTherm(text) }
  }

  function _parseTherm(text) {
    var lines = text.split('\n')
    var cpuVals = [], gpuVals = [], ssdVals = [], caseVals = []
    for (var i = 0; i < lines.length; i++) {
      var ln = lines[i]
      if (!ln) continue
      var p = ln.split("|")
      if (p.length < 3) continue
      var name  = (p[0] || "").toLowerCase()
      var label = (p[1] || "").toLowerCase()
      var v     = parseFloat(p[2]) / 1000.0
      if (isNaN(v) || v <= 0 || v > 200) continue
      if (name.indexOf("nvme") >= 0 || name.indexOf("nvm") >= 0 ||
          label.indexOf("ssd") >= 0 || label.indexOf("composite") >= 0) {
        ssdVals.push(v)
      } else if (name.indexOf("amdgpu") >= 0 || name.indexOf("nouveau") >= 0 ||
                 name.indexOf("nvidia") >= 0 || name.indexOf("i915") >= 0 ||
                 label.indexOf("edge") >= 0  || label.indexOf("junction") >= 0 ||
                 label.indexOf("gpu") >= 0) {
        gpuVals.push(v)
      } else if (name.indexOf("coretemp") >= 0 || name.indexOf("k10temp") >= 0 ||
                 name.indexOf("zenpower") >= 0 || label.indexOf("package") >= 0 ||
                 label.indexOf("tctl") >= 0   || label.indexOf("tdie") >= 0 ||
                 (label.indexOf("core") === 0 && cpuVals.length < 16)) {
        cpuVals.push(v)
      } else if (name.indexOf("acpitz") >= 0 || name.indexOf("ec") >= 0) {
        caseVals.push(v)
      }
    }
    function maxOf(a) { var m = 0; for (var i = 0; i < a.length; i++) if (a[i] > m) m = a[i]; return m }
    if (cpuVals.length)  cpuTempC  = maxOf(cpuVals)
    if (gpuVals.length)  { gpuTempC = maxOf(gpuVals); hasGpuTemp  = true }
    if (ssdVals.length)  { ssdTempC = maxOf(ssdVals); hasSsdTemp  = true }
    if (caseVals.length) { caseTempC = maxOf(caseVals); hasCaseTemp = true }
  }

  Process {
    id: brightnessProc
    command: ["sh", "-c",
      "for d in /sys/class/backlight/*; do [ -d \"$d\" ] || continue; " +
      "  b=$(cat \"$d/brightness\" 2>/dev/null); m=$(cat \"$d/max_brightness\" 2>/dev/null); " +
      "  [ -n \"$b\" ] && [ -n \"$m\" ] && echo \"$b $m\" && break; done"]
    stdout: StdioCollector {
      onStreamFinished: {
        var p = text.trim().split(/\s+/)
        if (p.length === 2) {
          var b = parseInt(p[0]), m = parseInt(p[1])
          if (m > 0) {
            root.hasBrightness = true
            root.brightnessPct = Math.round(b / m * 100)
          }
        }
      }
    }
  }

  Process {
    id: volumeProc
    command: ["sh", "-c",
      "if command -v wpctl >/dev/null 2>&1; then wpctl get-volume @DEFAULT_AUDIO_SINK@; " +
      "elif command -v pactl >/dev/null 2>&1; then pactl get-sink-volume @DEFAULT_SINK@; fi"]
    stdout: StdioCollector {
      onStreamFinished: {
        var t = (text || "").toLowerCase()
        var muted = t.indexOf("muted") >= 0 && t.indexOf("muted: no") < 0
        var m = t.match(/volume:\s+([\d.]+)/)
        if (m) {
          root.volMuted = muted
          root.volumePct = Math.round(parseFloat(m[1]) * 100)
          return
        }
        m = t.match(/(\d+)\s*%/)
        if (m) {
          root.volMuted = muted
          root.volumePct = parseInt(m[1])
        }
      }
    }
  }

  Process {
    id: gpuProc
    command: ["sh", "-c", "p=\"" + root.gpuScriptPath.replace(/^~/, "$HOME") + "\"; " +
                          "[ -x \"$p\" ] && \"$p\" 2>/dev/null"]
    stdout: StdioCollector { onStreamFinished: root._parseGpu(text) }
  }

  function _parseGpu(s) {
    if (!s || s.trim().length === 0) return
    var obj
    try { obj = JSON.parse(s) } catch (e) { return }
    var t = (obj && obj.tooltip) ? String(obj.tooltip) : ""

    var mTemp  = t.match(/Temperature\s*[:=]\s*([\d.]+)\s*°?\s*C/i)
    var mUtil  = t.match(/Utilization\s*[:=]\s*([\d.]+)\s*%/i)
    var mPower = t.match(/Power[^:]*[:=]\s*([\d.]+)/i)
    var mClock = t.match(/Clock[^:]*[:=]\s*([\d.]+)\s*\/\s*([\d.]+)\s*MHz/i)
    var mName  = t.match(/[^\n]*NVIDIA[^\n]*|[^\n]*Radeon[^\n]*|[^\n]*Intel[^\n]*GPU[^\n]*/i)

    if (mTemp)  { root.gpuTempC    = parseFloat(mTemp[1]);  root.hasGpuTemp = true }
    if (mUtil)  { root.gpuLoadPct  = parseFloat(mUtil[1]) }
    if (mPower) { root.gpuPowerW   = parseFloat(mPower[1]) }
    if (mClock) {
      root.gpuClockMHz    = Math.round(parseFloat(mClock[1]))
      root.gpuClockMaxMHz = Math.round(parseFloat(mClock[2]))
    }
    if (mName)  { root.gpuName     = mName[0].trim() }

    if (!mTemp && obj.text) {
      var mt = String(obj.text).match(/([\d.]+)\s*°?\s*C/i)
      if (mt) { root.gpuTempC = parseFloat(mt[1]); root.hasGpuTemp = true }
    }
  }

  IpcHandler {
    target: "departure-hud"
    function poll() { root.poll() }
  }
}
