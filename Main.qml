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

    Hud {
        id: hud
        objectName: "hud"
        anchors.centerIn: parent
        sys: root.sys
        settings: root.settings
    }
}
