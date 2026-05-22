import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

/*
  Departure HUD — standalone Wayland app entry point.

  Loads settings from a JSON config file (path picked from
  $DEPARTURE_HUD_CONFIG, falling back to $XDG_CONFIG_HOME/departure-hud/config.json
  or ~/.config/departure-hud/config.json), then opens one layer-shell
  surface per screen — the HUD itself is centered inside, and the empty
  area is click-through via an input mask.
*/
ShellRoot {
  id: root

  readonly property var defaults: ({
    scale: 1.0,
    useBackground: true,
    accentColor: "#f08a28",
    hotColor: "#ff5a3c",
    bgColor: "#0d0d0d",
    updateMs: 1000,
    disksToShow: "/,/home",
    netInterface: "",
    netMode: "sum",
    cpuMaxTemp: 90,
    gpuMaxTemp: 85,
    ssdMaxTemp: 65,
    showScope: true,
    starCount: 30,
    gpuScriptPath: "~/.local/bin/gpuinfo.sh",
    // Standalone-app extras:
    screen: "",            // empty = primary; otherwise the screen name (e.g. "DP-1")
    layer: "bottom",       // background | bottom | top | overlay  — "bottom" sits above wallpaper, below normal windows
    clickThrough: true,    // mask input region to the HUD only
    keyboardFocus: "none"  // none | exclusive | ondemand
  })

  readonly property string configPath: {
    var env = Quickshell.env("DEPARTURE_HUD_CONFIG")
    if (env && env.length > 0) return env
    var xdg = Quickshell.env("XDG_CONFIG_HOME")
    var home = Quickshell.env("HOME")
    var base = (xdg && xdg.length > 0) ? xdg : (home + "/.config")
    return base + "/departure-hud/config.json"
  }

  property var settings: defaults

  FileView {
    id: configFile
    path: root.configPath
    blockLoading: true
    preload: true
    printErrors: false
    onLoaded: root._reload()
    onLoadFailed: root._reload()
  }

  Component.onCompleted: _reload()

  function _reload() {
    var merged = Object.assign({}, defaults)
    var txt = ""
    try { txt = configFile.text() } catch (e) {}
    if (txt && txt.trim().length > 0) {
      try {
        var u = JSON.parse(txt)
        for (var k in u) if (u[k] !== undefined && u[k] !== null) merged[k] = u[k]
      } catch (e) {
        console.warn("departure-hud: could not parse", configPath, "—", e)
      }
    } else {
      console.info("departure-hud: no config at", configPath, "— using defaults")
    }
    settings = merged
  }

  SysData {
    id: sysData
    settings: root.settings
  }

  function _layerFor(name) {
    switch ((name || "").toLowerCase()) {
      case "background": return WlrLayer.Background
      case "bottom":     return WlrLayer.Bottom
      case "top":        return WlrLayer.Top
      case "overlay":
      default:           return WlrLayer.Overlay
    }
  }

  function _kbFor(name) {
    switch ((name || "").toLowerCase()) {
      case "exclusive": return WlrKeyboardFocus.Exclusive
      case "ondemand":  return WlrKeyboardFocus.OnDemand
      case "none":
      default:          return WlrKeyboardFocus.None
    }
  }

  Variants {
    model: {
      var screens = Quickshell.screens || []
      var name = (root.settings.screen || "").trim()
      if (name.length === 0) return screens.length > 0 ? [screens[0]] : []
      for (var i = 0; i < screens.length; i++) if (screens[i].name === name) return [screens[i]]
      return screens.length > 0 ? [screens[0]] : []
    }

    delegate: PanelWindow {
      id: win
      required property ShellScreen modelData
      screen: modelData
      color: "transparent"

      WlrLayershell.layer: root._layerFor(root.settings.layer)
      WlrLayershell.namespace: "departure-hud"
      WlrLayershell.keyboardFocus: root._kbFor(root.settings.keyboardFocus)
      WlrLayershell.exclusionMode: ExclusionMode.Ignore

      anchors {
        top: true; bottom: true; left: true; right: true
      }

      mask: root.settings.clickThrough ? hudMask : null

      Region {
        id: hudMask
        item: hud
      }

      Hud {
        id: hud
        anchors.centerIn: parent
        sys: sysData
        settings: root.settings
      }
    }
  }
}
