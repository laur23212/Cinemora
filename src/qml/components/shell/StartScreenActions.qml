import QtQuick
import Drift

// The two ways in, sized like Filmora's launcher: the first thing the eye lands on.
Row {
    id: actions
    signal newProjectRequested()
    signal openProjectRequested()
    width: parent.width
    spacing: Theme.spacingLg

    component BigButton: ThemedButton {
        font.pixelSize: 17
        font.weight: Font.Medium
        glyphSize: 22
        leftPadding: 26
        rightPadding: 30
        topPadding: 15
        bottomPadding: 15
        radius: Theme.radiusLg
    }

    BigButton {
        variant: "primary"
        glyph: Theme.icons.plus
        text: qsTr("New Project")
        onClicked: actions.newProjectRequested()
    }

    BigButton {
        variant: "secondary"
        glyph: Theme.icons.folder
        text: qsTr("Open Project…")
        tooltip: qsTr("Dafin (.drift), Filmora (.wfp) and Premiere Pro (.prproj) projects")
        onClicked: actions.openProjectRequested()
    }
}
