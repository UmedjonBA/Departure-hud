import QtQuick
import QtQuick.Layouts

/*
  Departure HUD — visual layout.
  Pure presentation; binds against `sys` (SysData) and `settings` (loaded JSON).
*/
Item {
  id: root

  property var settings: ({})
  property var sys: null
  property bool active: true

  readonly property real userScale: settings.scale || 1.0
  readonly property bool useBackground: settings.useBackground !== undefined ? settings.useBackground : true

  readonly property color cAccent: Qt.color(settings.accentColor || "#f08a28")
  readonly property color cHot:    Qt.color(settings.hotColor    || "#ff5a3c")
  readonly property color cBg:     Qt.color(settings.bgColor     || "#0d0d0d")

  readonly property color cFg:     cAccent
  readonly property color cFgDim:  Qt.darker(cFg, 2.4)
  readonly property color cFgMid:  Qt.darker(cFg, 1.8)
  readonly property color cFgSoft: Qt.darker(cFg, 6.0)

  readonly property bool showScope: settings.showScope !== undefined ? settings.showScope : true
  readonly property int starCount: settings.starCount !== undefined ? settings.starCount : 20
  readonly property int cpuMax: settings.cpuMaxTemp || 90
  readonly property int gpuMax: settings.gpuMaxTemp || 85
  readonly property int ssdMax: settings.ssdMaxTemp || 65
  readonly property int caseMax: 50

  readonly property int baseW: 1180
  readonly property int baseH: 600
  implicitWidth:  baseW * userScale
  implicitHeight: baseH * userScale

  FontLoader {
    id: depFont
    source: Qt.resolvedUrl("fonts/DepartureMono-Regular.otf")
  }
  readonly property string fontFamily: depFont.name || "monospace"

  function pad2(n) { return String(Math.round(n)).padStart(2, '0') }
  function pad3(n) { return String(Math.round(n)).padStart(3, '0') }

  function fmtRate(bps) {
    if (!isFinite(bps) || bps < 0) return "0 B"
    if (bps < 1024)         return Math.round(bps) + " B"
    if (bps < 1024*1024)    return (bps / 1024).toFixed(bps < 10*1024 ? 1 : 0) + " K"
    if (bps < 1024*1024*1024) {
      var m = bps / (1024 * 1024)
      return m.toFixed(m < 10 ? 1 : 0) + " M"
    }
    return (bps / (1024 * 1024 * 1024)).toFixed(2) + " G"
  }

  function rateBarPct(bps) {
    if (!isFinite(bps) || bps <= 0) return 0
    var lo = 0
    var hi = 8
    var v = Math.log10(1 + bps)
    var f = (v - lo) / (hi - lo)
    return Math.max(0, Math.min(100, f * 100))
  }

  Rectangle {
    id: bgRect
    anchors.fill: parent
    color: root.cBg
    visible: root.useBackground
  }

  Item {
    anchors.fill: parent

    Item {
      id: scaler
      width: root.baseW
      height: root.baseH
      transformOrigin: Item.TopLeft
      scale: Math.min(root.width / root.baseW, root.height / root.baseH)

      property string clockText: "00:00:00.00"
      property string uptimeText: "00D 00:00:00"
      Timer {
        interval: 100
        repeat: true
        running: root.active
        triggeredOnStart: true
        onTriggered: {
          var d = new Date()
          scaler.clockText =
              String(d.getHours()).padStart(2, '0') + ':' +
              String(d.getMinutes()).padStart(2, '0') + ':' +
              String(d.getSeconds()).padStart(2, '0') + '.' +
              String(Math.floor(d.getMilliseconds() / 10)).padStart(2, '0')
          var s = root.sys ? root.sys.uptimeSec : 0
          var dd = Math.floor(s / 86400)
          var hh = Math.floor((s % 86400) / 3600)
          var mm = Math.floor((s % 3600) / 60)
          var ss = s % 60
          scaler.uptimeText =
              String(dd).padStart(2, '0') + 'D ' +
              String(hh).padStart(2, '0') + ':' +
              String(mm).padStart(2, '0') + ':' +
              String(ss).padStart(2, '0')
        }
      }

      readonly property int padX: 18
      readonly property int padY: 10
      readonly property int gapC: 14
      readonly property int gapR: 8
      readonly property int leftW:  195
      readonly property int rightW: 220
      readonly property int midX:   padX + leftW + gapC
      readonly property int midW:   width - padX*2 - leftW - rightW - gapC*2
      readonly property int rightX: padX + leftW + gapC + midW + gapC
      readonly property int topH:    24
      readonly property int botH:    38
      readonly property int midY:    padY + topH + gapR
      readonly property int midRowH: height - padY*2 - topH - botH - gapR*2
      readonly property int botY:    padY + topH + gapR + midRowH + gapR

      Item {
        x: scaler.padX; y: scaler.padY; width: scaler.leftW; height: scaler.topH
        Column {
          spacing: 0
          Text { text: "SYS TIME:"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11 }
          Text { text: scaler.clockText; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 13 }
        }
      }

      Item {
        id: topHead
        x: scaler.midX; y: scaler.padY + 4
        width: scaler.midW; height: scaler.topH
        Text {
          id: topTitle
          anchors.centerIn: parent
          text: "S Y S M O N"
          color: root.cFg
          font.family: root.fontFamily
          font.pixelSize: 13
          font.letterSpacing: 6
        }
        TicksRow {
          anchors.left: parent.left
          anchors.right: topTitle.left
          anchors.rightMargin: 8
          anchors.verticalCenter: topTitle.verticalCenter
          height: 14
          color: root.cFg; family: root.fontFamily; count: 14
        }
        TicksRow {
          anchors.left: topTitle.right
          anchors.leftMargin: 8
          anchors.right: parent.right
          anchors.verticalCenter: topTitle.verticalCenter
          height: 14
          color: root.cFg; family: root.fontFamily; count: 14
        }
      }

      Item {
        x: scaler.rightX; y: scaler.padY; width: scaler.rightW; height: scaler.topH
        Column {
          anchors.right: parent.right
          spacing: 0
          Text { text: "UPTIME:";       color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                 horizontalAlignment: Text.AlignRight; width: parent.parent.width }
          Text { text: scaler.uptimeText; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 13
                 horizontalAlignment: Text.AlignRight; width: parent.parent.width }
        }
      }

      Column {
        x: scaler.padX; y: scaler.midY; width: scaler.leftW; height: scaler.midRowH
        spacing: 12

        Section {
          width: parent.width
          label: "CPU"
          color: root.cFg; bg: root.cBg; family: root.fontFamily

          Text { text: "CORE LOAD %"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width
            spacing: 3
            Repeater {
              model: root.sys ? root.sys.cpuPercents : []
              delegate: BarRow {
                width: parent.width
                pct:  modelData
                val:  root.pad2(modelData) + "%"
                hot:  modelData > 88
                fg:   root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
                family: root.fontFamily
              }
            }
          }
          Text { text: "FREQ GHz"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width
            spacing: 3
            Repeater {
              model: root.sys ? root.sys.cpuFreqsGHz.slice(0, 2) : []
              delegate: BarRow {
                width: parent.width
                pct: Math.min(100, modelData / Math.max(1, root.sys.cpuFreqMaxGHz) * 100)
                val: Number(modelData).toFixed(2)
                fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
                family: root.fontFamily
              }
            }
          }
        }

        Section {
          width: parent.width
          label: "MEM"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          Text { text: "USED / CACHE / SWAP"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width; spacing: 3
            BarRow { width: parent.width; pct: root.sys?.memUsedPct  ?? 0; val: (root.sys?.memUsedGB  ?? 0).toFixed(1)+"G"; fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot; family: root.fontFamily }
            BarRow { width: parent.width; pct: root.sys?.memCachePct ?? 0; val: (root.sys?.memCacheGB ?? 0).toFixed(1)+"G"; fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot; family: root.fontFamily }
            BarRow { width: parent.width; pct: root.sys?.swapUsedPct ?? 0; val: (root.sys?.swapUsedGB ?? 0).toFixed(1)+"G"; fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot; family: root.fontFamily }
          }
        }

        Section {
          width: parent.width
          label: "AUDIO"
          color: root.cFg; bg: root.cBg; family: root.fontFamily

          Text { text: "MASTER VOL"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10
                 anchors.horizontalCenter: parent.horizontalCenter }

          VolumeKnob {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 110; height: 58
            fg: root.cFg; dim: root.cFgDim; hot: root.cHot
            percent: root.sys?.volumePct ?? 0
            family: root.fontFamily
          }

          Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 5
            SegButton { label: "SP"; on: true;  fg: root.cFg; bg: root.cBg; family: root.fontFamily }
            SegButton { label: "HP"; on: false; fg: root.cFg; bg: root.cBg; family: root.fontFamily }
            SegButton { label: "BT"; on: false; fg: root.cFg; bg: root.cBg; family: root.fontFamily }
            SegButton { label: "HD"; on: false; fg: root.cFg; bg: root.cBg; family: root.fontFamily }
          }
          Text { text: "OUTPUT"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 9; font.letterSpacing: 1.2
                 anchors.horizontalCenter: parent.horizontalCenter }
        }
      }

      Item {
        id: midCol
        x: scaler.midX; y: scaler.midY; width: scaler.midW; height: scaler.midRowH

        YearStack {
          id: yearStack
          width: 70
          height: parent.height
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter
          fg: root.cFg
          fgDim: root.cFgDim
          family: root.fontFamily
        }

        Scope {
          id: scope
          anchors.left: yearStack.right
          anchors.right: pitchStack.left
          anchors.verticalCenter: parent.verticalCenter
          height: Math.min(parent.height, width * 0.96)
          fg: root.cFg
          bg: root.cBg
          family: root.fontFamily
          starCount: root.starCount
          visible: root.showScope
          active: root.active
          netActive: {
            var b = (root.sys?.netDownBps ?? 0) + (root.sys?.netUpBps ?? 0)
            if (b <= 0) return 0
            return Math.min(99, Math.round(Math.log10(1 + b) * 12))
          }
          cpuTotal: Math.round(root.sys?.cpuAvgPercent ?? 0)
          gpuTotal: Math.round(root.sys?.gpuLoadPct ?? 0)
        }

        PitchStack {
          id: pitchStack
          width: 70
          height: parent.height
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          fg: root.cFg
          fgDim: root.cFgDim
          family: root.fontFamily
          active: root.active
        }
      }

      Column {
        x: scaler.rightX; y: scaler.midY; width: scaler.rightW; height: scaler.midRowH
        spacing: 12

        Section {
          width: parent.width
          label: "GPU"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          visible: root.sys ? root.sys.hasGpuTemp : false

          Text { text: "LOAD %"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width
            spacing: 3
            BarRow {
              width: parent.width
              pct: root.sys?.gpuLoadPct ?? 0
              val: root.pad2(root.sys?.gpuLoadPct ?? 0) + "%"
              hot: (root.sys?.gpuLoadPct ?? 0) > 90
              fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
              family: root.fontFamily
            }
          }
          Text { text: "CLOCK MHz"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width
            spacing: 3
            BarRow {
              width: parent.width
              pct: Math.min(100, (root.sys?.gpuClockMHz ?? 0) / Math.max(1, root.sys?.gpuClockMaxMHz ?? 2000) * 100)
              val: String(root.sys?.gpuClockMHz ?? 0)
              fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
              family: root.fontFamily
            }
          }
        }

        Section {
          width: parent.width
          label: "THERM"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          Grid {
            columns: 3
            columnSpacing: 12
            rowSpacing: 2
            width: parent.width

            Text { text: "CPU"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11 }
            Text { text: Math.round(root.sys?.cpuTempC ?? 0) + "°C"; font.family: root.fontFamily; font.pixelSize: 11
                   color: (root.sys?.cpuTempC ?? 0) > root.cpuMax * 0.95 ? root.cHot : root.cFg }
            Text { text: root.cpuMax; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   horizontalAlignment: Text.AlignRight; width: 28 }

            Text { text: "GPU"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasGpuTemp : false }
            Text { text: Math.round(root.sys?.gpuTempC ?? 0) + "°C"; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasGpuTemp : false
                   color: (root.sys?.gpuTempC ?? 0) > root.gpuMax * 0.95 ? root.cHot : root.cFg }
            Text { text: root.gpuMax; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasGpuTemp : false
                   horizontalAlignment: Text.AlignRight; width: 28 }

            Text { text: "SSD"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasSsdTemp : false }
            Text { text: Math.round(root.sys?.ssdTempC ?? 0) + "°C"; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasSsdTemp : false
                   color: (root.sys?.ssdTempC ?? 0) > root.ssdMax * 0.95 ? root.cHot : root.cFg }
            Text { text: root.ssdMax; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasSsdTemp : false
                   horizontalAlignment: Text.AlignRight; width: 28 }

            Text { text: "CHASSIS"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasCaseTemp : false }
            Text { text: Math.round(root.sys?.caseTempC ?? 0) + "°C"; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasCaseTemp : false
                   color: (root.sys?.caseTempC ?? 0) > root.caseMax * 0.95 ? root.cHot : root.cFg }
            Text { text: root.caseMax; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                   visible: root.sys ? root.sys.hasCaseTemp : false
                   horizontalAlignment: Text.AlignRight; width: 28 }
          }
        }

        Section {
          width: parent.width
          label: "BATT"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          visible: root.sys ? root.sys.hasBattery : false

          readonly property real drainPctPerHour: {
            if (!root.sys || root.sys.batState !== "DISCHARGING") return 0
            var m = root.sys.batMinutesLeft
            if (m <= 0) return 0
            return (root.sys.batPercent ?? 0) * 60 / m
          }
          readonly property real drainMax: 30

          Text { text: "CHARGE %"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          BarRow {
            width: parent.width
            pct: root.sys?.batPercent ?? 0
            val: root.pad3(root.sys?.batPercent ?? 0)
            hot: (root.sys?.batPercent ?? 0) < 20
            fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
            family: root.fontFamily
          }
          Text { text: "DRAIN %/H"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          BarRow {
            width: parent.width
            pct: Math.min(100, parent.drainPctPerHour / parent.drainMax * 100)
            val: parent.drainPctPerHour.toFixed(1) + "/H"
            hot: parent.drainPctPerHour > 20
            fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
            family: root.fontFamily
          }
          Grid {
            columns: 2
            columnSpacing: 10
            rowSpacing: 6
            width: parent.width
            KvBlock { width: (parent.width - parent.columnSpacing) / 2; k: "STATE"; v: root.sys?.batState ?? "UNKNOWN"; fg: root.cFg; family: root.fontFamily }
            KvBlock {
              width: (parent.width - parent.columnSpacing) / 2; alignRight: true
              k: "RATE"
              v: {
                var r = root.sys?.batRateW ?? 0
                return (r >= 0 ? "+" : "−") + Math.abs(r).toFixed(1) + "W"
              }
              fg: root.cFg; family: root.fontFamily
            }
            KvBlock {
              width: (parent.width - parent.columnSpacing) / 2
              k: "TIME LEFT"
              v: {
                var m = root.sys?.batMinutesLeft ?? 0
                return (Math.floor(m / 60)).toString().padStart(2, '0') + ':' + (m % 60).toString().padStart(2, '0')
              }
              fg: root.cFg; family: root.fontFamily
            }
            KvBlock { width: (parent.width - parent.columnSpacing) / 2; alignRight: true; k: "CYCLES"; v: String(root.sys?.batCycles ?? 0); fg: root.cFg; family: root.fontFamily }
          }
        }

        Section {
          width: parent.width
          label: "DISK"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          Text {
            color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10
            text: {
              var d = root.sys?.disks ?? []
              return d.map(function (x) { return x.mount }).join("  ")
            }
          }
          Column {
            width: parent.width; spacing: 3
            Repeater {
              model: root.sys ? root.sys.disks : []
              delegate: BarRow {
                width: parent.width
                pct: modelData.usedPct
                val: Math.round(modelData.usedPct) + "%"
                hot: modelData.usedPct > 90
                fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
                family: root.fontFamily
              }
            }
          }
        }

        Section {
          width: parent.width
          label: "NET"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          Text {
            color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10
            text: "↓ DOWN / ↑ UP" + (root.sys?.netInterface ? "  [" + root.sys.netInterface + "]" : "")
          }
          Column {
            width: parent.width; spacing: 3
            BarRow {
              width: parent.width
              pct: root.rateBarPct(root.sys?.netDownBps ?? 0)
              val: root.fmtRate(root.sys?.netDownBps ?? 0)
              fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
              family: root.fontFamily
            }
            BarRow {
              width: parent.width
              pct: root.rateBarPct(root.sys?.netUpBps ?? 0)
              val: root.fmtRate(root.sys?.netUpBps ?? 0)
              fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot
              family: root.fontFamily
            }
          }
        }

        Section {
          width: parent.width
          label: "DISPLAY"
          color: root.cFg; bg: root.cBg; family: root.fontFamily
          visible: root.sys ? root.sys.hasBrightness : false
          Text { text: "BRIGHTNESS / NIGHT"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 10 }
          Column {
            width: parent.width; spacing: 3
            BarRow { width: parent.width; pct: root.sys?.brightnessPct ?? 0; val: (root.sys?.brightnessPct ?? 0)+"%"; fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot; family: root.fontFamily }
            BarRow { width: parent.width; pct: root.sys?.nightLightPct ?? 0; val: (root.sys?.nightLightPct ?? 0)+"%"; fg: root.cFg; bg: root.cBg; soft: root.cFgSoft; hotC: root.cHot; family: root.fontFamily }
          }
        }
      }

      Item {
        x: scaler.padX; y: scaler.botY; width: scaler.leftW; height: scaler.botH
        Rectangle {
          anchors.verticalCenter: parent.verticalCenter
          color: root.cFg
          implicitWidth: pillTxt.implicitWidth + 12
          height: pillTxt.implicitHeight + 4
          Text {
            id: pillTxt; anchors.centerIn: parent
            text: "SYSMON"; color: "#0a0a0a"
            font.family: root.fontFamily; font.pixelSize: 11; font.letterSpacing: 0.5
          }
        }
      }

      Item {
        x: scaler.midX
        y: scaler.botY
        width: scaler.midW
        height: scaler.botH
        Row {
          anchors.centerIn: parent
          spacing: 26
          KvInline { k: "HOST:";   v: root.sys?.hostName      ?? "host"; fg: root.cFg; family: root.fontFamily }
          KvInline { k: "KERNEL:"; v: root.sys?.kernelVersion ?? "";     fg: root.cFg; family: root.fontFamily }
          KvInline { k: "SHELL:";  v: root.sys?.shellName     ?? "";     fg: root.cFg; family: root.fontFamily }
          KvInline { k: "USER:";   v: root.sys?.userName      ?? "@";    fg: root.cFg; family: root.fontFamily }
        }
      }

      Item {
        x: scaler.rightX; y: scaler.botY; width: scaler.rightW; height: scaler.botH
        Column {
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          spacing: 0
          Text { text: "LOAD AVG:"; color: root.cFg; font.family: root.fontFamily; font.pixelSize: 11
                 horizontalAlignment: Text.AlignRight; width: parent.parent.width }
          Text {
            color: root.cFg; font.family: root.fontFamily; font.pixelSize: 12
            horizontalAlignment: Text.AlignRight; width: parent.parent.width
            text: {
              var l = root.sys?.loadAvg ?? [0, 0, 0]
              return l[0].toFixed(2) + ' ' + l[1].toFixed(2) + ' ' + l[2].toFixed(2)
            }
          }
        }
      }
    }
  }

  component TicksRow: Item {
    id: ticksRoot
    property color color: "#fff"
    property string family: "monospace"
    property int count: 14
    implicitHeight: 14

    Row {
      anchors.fill: parent
      Repeater {
        model: ticksRoot.count
        delegate: Item {
          width: ticksRoot.width / Math.max(1, ticksRoot.count)
          height: ticksRoot.height
          Text {
            anchors.centerIn: parent
            text: "|"
            color: ticksRoot.color
            font.family: ticksRoot.family
            font.pixelSize: 13
            opacity: (index % 3 === 1) ? 0.35 : 1
          }
        }
      }
    }
  }

  component Section: Column {
    id: sec
    property string label: ""
    property color color: "#fff"
    property color bg: "#000"
    property string family: "monospace"
    spacing: 4

    Item {
      id: bracket
      width: parent.width
      height: 14
      Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left; anchors.right: parent.right
        height: 1; color: sec.color
      }
      Rectangle { width: 1; height: 7; color: sec.color
                  anchors.top: parent.top; anchors.left: parent.left }
      Rectangle { width: 1; height: 7; color: sec.color
                  anchors.top: parent.top; anchors.right: parent.right }
      Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: -1
        color: sec.bg
        width: bracketLbl.implicitWidth + 16
        height: bracketLbl.implicitHeight
        Text {
          id: bracketLbl
          anchors.centerIn: parent
          text: sec.label
          color: sec.color
          font.family: sec.family
          font.pixelSize: 13
          font.letterSpacing: 2
        }
      }
    }
  }

  component BarRow: Item {
    id: br
    property real pct: 0
    property string val: ""
    property bool hot: false
    property color fg: "#fff"
    property color bg: "#000"
    property color soft: "#222"
    property color hotC: "#f00"
    property string family: "monospace"
    implicitHeight: 10

    Text {
      id: valTxt
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      text: br.val
      color: br.fg
      font.family: br.family
      font.pixelSize: 11
    }
    Rectangle {
      anchors.left: parent.left
      anchors.right: valTxt.left
      anchors.rightMargin: 6
      anchors.verticalCenter: parent.verticalCenter
      height: 8
      color: br.soft
      Rectangle {
        id: fill
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: Math.max(0, Math.min(1, br.pct / 100)) * parent.width
        color: br.hot ? br.hotC : br.fg
        Behavior on width { NumberAnimation { duration: 350 } }
        Rectangle {
          anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
          width: 3; color: br.bg
        }
      }
    }
  }

  component KvBlock: Column {
    id: kvb
    property string k: ""
    property string v: ""
    property color fg: "#fff"
    property bool alignRight: false
    property string family: "monospace"
    spacing: 0

    Text {
      text: kvb.k
      color: kvb.fg
      font.family: kvb.family
      font.pixelSize: 9
      width: parent.width
      horizontalAlignment: kvb.alignRight ? Text.AlignRight : Text.AlignLeft
    }
    Text {
      text: kvb.v
      color: kvb.fg
      font.family: kvb.family
      font.pixelSize: 11
      width: parent.width
      horizontalAlignment: kvb.alignRight ? Text.AlignRight : Text.AlignLeft
      elide: Text.ElideRight
    }
  }

  component KvInline: Row {
    id: kvi
    property string k: ""
    property string v: ""
    property color fg: "#fff"
    property string family: "monospace"
    spacing: 4
    anchors.verticalCenter: parent.verticalCenter
    Text { text: kvi.k; color: kvi.fg; font.family: kvi.family; font.pixelSize: 11 }
    Text { text: kvi.v; color: kvi.fg; font.family: kvi.family; font.pixelSize: 12 }
  }

  component SegButton: Rectangle {
    id: sb
    property string label: ""
    property bool on: false
    property color fg: "#fff"
    property color bg: "#000"
    property string family: "monospace"
    width: 28; height: 24
    color: on ? fg : bg
    border.color: fg; border.width: 1
    Text {
      anchors.centerIn: parent
      text: sb.label
      color: sb.on ? "#0a0a0a" : sb.fg
      font.family: sb.family
      font.pixelSize: 11
    }
  }

  component VolumeKnob: Item {
    id: knob
    property int percent: 0
    property color fg: "#fff"
    property color dim: "#444"
    property color hot: "#ff5a3c"
    property string family: "monospace"
    width: 110; height: 58
    readonly property bool overload: percent > 100

    Canvas {
      id: knobCanvas
      anchors.fill: parent
      onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var cx = width / 2, cy = height - 6, r = Math.min(width, height) - 16
        ctx.strokeStyle = knob.dim
        ctx.lineWidth = 1
        ctx.beginPath()
        ctx.arc(cx, cy, r, Math.PI, 2 * Math.PI, false)
        ctx.stroke()
        var pctClamped = Math.max(0, Math.min(100, knob.percent))
        var ang = Math.PI + (pctClamped / 100) * Math.PI
        ctx.strokeStyle = knob.overload ? knob.hot : knob.fg
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.moveTo(cx, cy)
        ctx.lineTo(cx + Math.cos(ang) * (r - 2), cy + Math.sin(ang) * (r - 2))
        ctx.stroke()
        ctx.fillStyle = knob.overload ? knob.hot : knob.fg
        ctx.beginPath()
        ctx.arc(cx, cy, 2, 0, 2 * Math.PI)
        ctx.fill()
      }
      Connections {
        target: knob
        function onPercentChanged() { knobCanvas.requestPaint() }
      }
    }

    Text {
      anchors.bottom: parent.bottom
      anchors.horizontalCenter: parent.horizontalCenter
      text: knob.percent
      color: knob.overload ? knob.hot : knob.fg
      font.family: knob.family
      font.pixelSize: 11
    }

    Text {
      visible: knob.overload
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.bottom
      anchors.bottomMargin: Math.min(knob.width, knob.height) / 2 - 2
      text: "OVERLOAD"
      color: knob.hot
      font.family: knob.family
      font.pixelSize: 11
      font.letterSpacing: 1.5
      font.bold: true
    }
  }

  component YearStack: Item {
    id: ys
    property color fg: "#fff"
    property color fgDim: "#555"
    property string family: "monospace"
    Column {
      anchors.right: parent.right
      anchors.rightMargin: 10
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width - 10
      spacing: 6
      Text { text: "YEAR"; color: ys.fg; font.family: ys.family; font.pixelSize: 10
             font.letterSpacing: 1.5; width: parent.width; horizontalAlignment: Text.AlignRight }
      Repeater {
        model: 11
        delegate: Item {
          width: parent.width
          height: 18
          property int yearVal: 2020 + index
          property bool sel: yearVal === (new Date()).getFullYear()
          Text {
            id: yearTxt
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: parent.yearVal
            color: parent.sel ? ys.fg : ys.fgDim
            font.family: ys.family
            font.pixelSize: 12
          }
          Text {
            anchors.right: yearTxt.left
            anchors.rightMargin: -2
            anchors.verticalCenter: parent.verticalCenter
            text: parent.sel ? "[" : ""
            color: ys.fg
            font.family: ys.family
            font.pixelSize: 12
          }
          Text {
            anchors.left: yearTxt.right
            anchors.leftMargin: -2
            anchors.verticalCenter: parent.verticalCenter
            text: parent.sel ? "]" : ""
            color: ys.fg
            font.family: ys.family
            font.pixelSize: 12
          }
        }
      }
    }
  }

  component PitchStack: Item {
    id: ps
    property color fg: "#fff"
    property color fgDim: "#555"
    property string family: "monospace"
    property bool active: true
    readonly property var scaleValues: [30, 25, 20, 15, 10, 5, 0, -5, -10, -15, -20, -25, -30]
    property int _nearestIdx: 0

    Column {
      id: psCol
      anchors.left: parent.left
      anchors.leftMargin: 10
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width - 10
      spacing: 6
      Text {
        text: "ACCEL\nFPS²"
        color: ps.fg
        font.family: ps.family
        font.pixelSize: 10
        font.letterSpacing: 1.5
        lineHeight: 0.9
      }
      Repeater {
        id: scaleRep
        model: ps.scaleValues
        delegate: Item {
          width: parent.width
          height: 16
          property int v: ps.scaleValues[index]
          property bool sel: index === ps._nearestIdx
          Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: {
              var s = parent.v > 0 ? "+" : (parent.v < 0 ? "−" : " ")
              return s + String(Math.abs(parent.v)).padStart(2, '0')
            }
            color: parent.sel ? ps.fg : ps.fgDim
            font.family: ps.family
            font.pixelSize: 12
          }
        }
      }
    }

    Text {
      id: marker
      text: "◂"
      color: ps.fg
      font.family: ps.family
      font.pixelSize: 32
      x: 34
      y: 0
    }

    Timer {
      interval: 100
      repeat: true
      running: ps.active
      onTriggered: {
        var t = Date.now() / 1000
        var v =
          Math.sin(t * 0.16)       * 18 +
          Math.sin(t * 0.34 + 1.5) *  9 +
          Math.sin(t * 0.56 + 0.7) *  4
        var nItems = ps.scaleValues.length
        if (nItems < 2) return
        var minV = ps.scaleValues[nItems - 1]
        var maxV = ps.scaleValues[0]
        var f = Math.max(0, Math.min(1, (maxV - v) / (maxV - minV)))
        var idx = Math.round(f * (nItems - 1))
        var headerH = psCol.children[0].height
        var rowH = 16
        var spacing = psCol.spacing
        var startY = headerH + spacing
        var totalRows = nItems
        var rowStep = rowH + spacing
        var firstTop = startY
        var lastTop  = startY + (totalRows - 1) * rowStep
        var top = firstTop + f * (lastTop - firstTop)
        marker.y = psCol.y + top - marker.height / 2 + rowH / 2
        ps._nearestIdx = idx
      }
    }
  }

  component Scope: Item {
    id: sc
    property color fg: "#fff"
    property color bg: "#000"
    property string family: "monospace"
    property int starCount: 30
    property int netActive: 0
    property int cpuTotal: 0
    property int gpuTotal: 0
    property bool active: true

    readonly property int vbW: 480
    readonly property int vbH: 460

    Canvas {
      id: staticCanvas
      anchors.fill: parent
      renderStrategy: Canvas.Threaded
      onWidthChanged:  requestPaint()
      onHeightChanged: requestPaint()
      onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var w = width, h = height
        var sx = w / sc.vbW, sy = h / sc.vbH
        var unit = Math.min(sx, sy)
        ctx.lineWidth = 1
        ctx.strokeStyle = sc.fg

        var cx = 240 * sx, cy = 230 * sy, R = 180 * unit

        ctx.beginPath()
        ctx.moveTo(45 * sx, 130 * sy)
        ctx.lineTo(30 * sx, 145 * sy)
        ctx.lineTo(30 * sx, 315 * sy)
        ctx.lineTo(45 * sx, 330 * sy)
        ctx.stroke()
        ctx.beginPath()
        ctx.moveTo(435 * sx, 130 * sy)
        ctx.lineTo(450 * sx, 145 * sy)
        ctx.lineTo(450 * sx, 315 * sy)
        ctx.lineTo(435 * sx, 330 * sy)
        ctx.stroke()

        ctx.beginPath()
        ctx.moveTo(30  * sx, 230 * sy); ctx.lineTo(37  * sx, 230 * sy)
        ctx.moveTo(450 * sx, 230 * sy); ctx.lineTo(443 * sx, 230 * sy)
        ctx.stroke()

        ctx.beginPath()
        ctx.arc(cx, cy, R, 0, 2 * Math.PI)
        ctx.stroke()

        ctx.save()
        ctx.beginPath()
        ctx.arc(cx, cy, R, 0, 2 * Math.PI)
        ctx.clip()
        ctx.beginPath()
        ctx.ellipse(cx - 280 * sx, cy - 130 * sy, 560 * sx, 260 * sy)
        ctx.stroke()
        ctx.beginPath()
        ctx.ellipse(cx - 130 * sx, cy - 280 * sy, 260 * sx, 560 * sy)
        ctx.stroke()
        ctx.restore()

        ctx.beginPath()
        ctx.moveTo(48  * sx, 230 * sy); ctx.lineTo(432 * sx, 230 * sy)
        ctx.moveTo(240 * sx, 42  * sy); ctx.lineTo(240 * sx, 418 * sy)
        ctx.stroke()

        ctx.lineWidth = 1.5
        ctx.beginPath()
        ctx.moveTo(228 * sx, 230 * sy); ctx.lineTo(236 * sx, 230 * sy)
        ctx.moveTo(244 * sx, 230 * sy); ctx.lineTo(252 * sx, 230 * sy)
        ctx.moveTo(240 * sx, 218 * sy); ctx.lineTo(240 * sx, 226 * sy)
        ctx.moveTo(240 * sx, 234 * sy); ctx.lineTo(240 * sx, 242 * sy)
        ctx.stroke()
      }
    }

    Item {
      id: starsField
      anchors.fill: parent
      readonly property real sx: width / sc.vbW
      readonly property real sy: height / sc.vbH
      readonly property real unit: Math.min(sx, sy)
      readonly property real cx: 240 * sx
      readonly property real cy: 230 * sy
      readonly property real maxR: 175 * unit

      Repeater {
        id: starsRep
        model: sc.starCount
        delegate: Rectangle {
          id: star
          property real angle: 0
          property real cosA:  1
          property real sinA:  0
          property real radius: 0
          property real speed: 50
          color: sc.fg
          antialiasing: false
          width:  1; height: 1
          visible: sc.active
        }
      }

      function reroll(s) {
        s.angle = Math.random() * Math.PI * 2
        s.cosA  = Math.cos(s.angle)
        s.sinA  = Math.sin(s.angle)
        s.speed = 35 + Math.random() * 60
      }

      function initStars() {
        for (var i = 0; i < starsRep.count; i++) {
          var s = starsRep.itemAt(i)
          if (!s) continue
          reroll(s)
          s.radius = Math.random() * Math.max(1, maxR)
        }
      }

      function step(dt) {
        var localMaxR = maxR
        if (localMaxR <= 0) return
        var localCx = cx, localCy = cy
        for (var i = 0; i < starsRep.count; i++) {
          var s = starsRep.itemAt(i)
          if (!s) continue
          var r = s.radius + s.speed * dt * unit
          if (r > localMaxR) { reroll(s); r = 1 }
          s.radius = r
          var frac = r / localMaxR
          var sz = Math.max(1, Math.min(4.5, 0.8 + frac * 3.5))
          s.width  = sz
          s.height = sz
          s.opacity = 0.25 + frac * 0.75
          s.x = localCx + s.cosA * r - sz / 2
          s.y = localCy + s.sinA * r - sz / 2
        }
      }

      Timer {
        id: starsTick
        interval: 100
        repeat: true
        running: sc.visible && sc.active
        property real lastT: 0
        onTriggered: {
          var now = Date.now()
          var dt = lastT === 0 ? 0.066 : Math.min(0.1, (now - lastT) / 1000)
          lastT = now
          starsField.step(dt)
        }
      }

      Component.onCompleted: Qt.callLater(initStars)
    }

    Rectangle {
      anchors.right: parent.right
      anchors.rightMargin: parent.width * 0.22
      anchors.top: parent.top
      anchors.topMargin: parent.height * 0.26
      color: sc.bg
      border.color: sc.fg
      border.width: 1
      width: readoutCol.implicitWidth + 14
      height: readoutCol.implicitHeight + 6
      Column {
        id: readoutCol
        anchors.centerIn: parent
        spacing: 0
        Text { text: "NET " + String(sc.netActive).padStart(2, '0'); color: sc.fg; font.family: sc.family; font.pixelSize: 11 }
        Text { text: "CPU " + String(sc.cpuTotal).padStart(3, '0'); color: sc.fg; font.family: sc.family; font.pixelSize: 11 }
        Text { text: "GPU " + String(sc.gpuTotal).padStart(3, '0'); color: sc.fg; font.family: sc.family; font.pixelSize: 11 }
      }
    }

    Rectangle {
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.bottom
      anchors.bottomMargin: parent.height * 0.09
      color: sc.fg
      width: autoTxt.implicitWidth + 18
      height: autoTxt.implicitHeight + 4
      Text {
        id: autoTxt
        anchors.centerIn: parent
        text: "A U T O"
        color: "#0a0a0a"
        font.family: sc.family
        font.pixelSize: 11
        font.letterSpacing: 4
      }
    }
  }
}
