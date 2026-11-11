import QtQuick
import QtQuick.Layouts
import Drift

// Dafin: saves the picture shown in the preview at the playhead as a PNG at the project's full
// resolution (Pictures/Dafin Screenshots). Only the picture, none of the editing overlays.
IconButton {
    Layout.alignment: Qt.AlignVCenter
    glyph: Theme.icons.image
    variant: "text"
    tooltip: qsTr("Save a screenshot of the preview (full resolution)")
    onClicked: EditorState.savePreviewScreenshot()
}
