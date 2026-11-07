import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "toolbar"
    title: qsTr("Toolbars")

    SettingsRow {
        title: qsTr("Button positions")
        searchId: "toolbar-positions"
        description: qsTr("Press and hold any button on the top bar, under the preview or on the timeline toolbar, then drag it to a new place. On the timeline, drop an action on the More button to move it into that menu. Esc cancels a drag.")
        keywords: "move drag rearrange order position reset buttons"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset all")
            onClicked: {
                EditorState.preferences.resetBarLayouts()
                toolbarCustomizer.reload()
            }
        }
    }

    SettingsRow {
        stacked: true
        title: qsTr("Toolbar layout")
        searchId: "toolbar-layout"
        description: qsTr("Choose which actions appear on the toolbar above the timeline and which go in its overflow menu.")
        keywords: "buttons actions overflow menu customize"
        query: page.query

        TimelineToolbarCustomizer {
            id: toolbarCustomizer
            autoSave: true
            width: parent.width
        }
    }
}
