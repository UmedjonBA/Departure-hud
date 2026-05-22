import QtQuick
import QtQuick.Window

/*
  Departure HUD — entry point.

  C++ side (main.cpp) creates the window, picks the screen, configures
  layer-shell (if available) and the input mask, then sets visible = true.

  Context properties available here:
    - appSettings        : merged JSON config (QVariantMap)
    - sysData            : SysData C++ object with system metrics
    - layerShellEnabled  : whether LayerShellQt was compiled in
*/
Window {
    id: root
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus

    property var settings: appSettings
    property var sys: sysData

    // True when the compositor is asking for frames from this surface.
    // Goes false when the HUD is fully occluded by another window, the
    // session is locked, or the output is asleep — at which point all
    // animation timers in the HUD pause and SysData stops polling.
    readonly property bool exposed: visibility ? visibility.visible : true

    Hud {
        id: hud
        objectName: "hud"
        anchors.centerIn: parent
        sys: root.sys
        settings: root.settings
        active: root.exposed
    }
}
