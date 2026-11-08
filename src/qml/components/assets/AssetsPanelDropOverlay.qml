import QtQuick
import Drift
// Drag feedback. Dropping files onto the panel used to give no visual
// confirmation that it was even a valid target.
//
// Dafin: over Titles, Stickers and Audio (or their buttons in the top bar) it says the files go to
// the user's own library for that tab, and that other media still goes to Media.
Rectangle {
    id: root

    property bool active: false
    // "titles", "lotties", "sounds" or "" (Media).
    property string collection: ""
    property string collectionName: ""

    anchors.fill: parent
    z: 50
    radius: Theme.c.assetsPanelDropOverlay.radius
    color: Qt.rgba(Theme.c.assetsPanelDropOverlay.accent.r, Theme.c.assetsPanelDropOverlay.accent.g, Theme.c.assetsPanelDropOverlay.accent.b, 0.08)
    border.width: Theme.c.assetsPanelDropOverlay.borderWidth
    border.color: Theme.c.assetsPanelDropOverlay.accent
    visible: opacity > 0
    opacity: active ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    EmptyState {
        anchors.centerIn: parent
        glyph: root.collection.length > 0 ? Theme.icons.download : Theme.icons.upload
        title: root.collection === "titles" ? qsTr("Drop to add the templates to Titles")
               : root.collection.length > 0 ? qsTr("Drop to add to %1").arg(root.collectionName)
               : qsTr("Drop to import")
        hint: root.collection === "titles"
              ? qsTr(".mogrt templates, or a .zip with a .mogrt and its preview video. Other media goes to Media.")
              : root.collection === "lotties"
                ? qsTr("Lottie animations (.json, .lottie). Other media goes to Media.")
                : root.collection === "sounds"
                  ? qsTr("Audio files. Video and images go to Media.")
                  : qsTr("Video, audio and image files")
    }
}
