import QtQuick
import Drift

// Dafin: Save as New Template (from the selected title) and Edit details (of a saved one).
// Name, optional description, and the category, with a new category made right here. Saving as
// new never overwrites anything: it always mints a new template.
ThemedDialog {
    id: root

    // "save": a new template from the selected clip. "edit": details of the template presetId.
    property string mode: "save"
    property string presetId: ""
    property var categories: []
    readonly property bool creatingCategory: categoryBox.currentIndex === categoryBox.count - 1
    readonly property string chosenName: nameField.text.trim()

    title: mode === "save" ? qsTr("Save as new template") : qsTr("Template details")
    acceptText: mode === "save" ? qsTr("Save") : qsTr("Done")
    preferredWidth: Theme.dialogWidthSm
    acceptEnabled: chosenName.length > 0 && (!creatingCategory || newCategoryField.text.trim().length > 0)

    function refreshCategories(selectId) {
        root.categories = EditorState.templateCategories()
        let index = 0
        for (let i = 0; i < root.categories.length; ++i)
            if (root.categories[i].id === selectId)
                index = i + 1
        categoryBox.currentIndex = index
    }

    function openForSave(suggestedName, categoryId) {
        mode = "save"
        presetId = ""
        nameField.text = suggestedName || ""
        descriptionField.text = ""
        newCategoryField.text = ""
        refreshCategories(categoryId || "")
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function openForEdit(preset) {
        mode = "edit"
        presetId = preset.id
        nameField.text = preset.label || ""
        descriptionField.text = preset.description || ""
        newCategoryField.text = ""
        refreshCategories(preset.category || "")
        open()
        nameField.forceActiveFocus()
    }

    function chosenCategory() {
        if (creatingCategory)
            return EditorState.addTemplateCategory(newCategoryField.text.trim())
        const i = categoryBox.currentIndex - 1
        return i >= 0 && i < categories.length ? categories[i].id : ""
    }

    onAccepted: {
        const details = {
            "label": chosenName,
            "description": descriptionField.text.trim(),
            "category": chosenCategory()
        }
        if (mode === "save")
            EditorState.saveTextTemplate(EditorState.selectedTrack, EditorState.selectedClip, details)
        else
            EditorState.setUserTextPresetDetails(presetId, details)
    }

    contentItem: Column {
        width: parent ? parent.width : 320
        spacing: Theme.spacingSm

        Text {
            text: qsTr("Name")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedTextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("My Electric Pop")
        }

        Text {
            text: qsTr("Description (optional)")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedTextField {
            id: descriptionField
            width: parent.width
            placeholderText: qsTr("What it is for")
        }

        Text {
            text: qsTr("Category")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedComboBox {
            id: categoryBox
            width: parent.width
            model: [qsTr("Uncategorized")].concat(root.categories.map(c => c.name)).concat([qsTr("New category…")])
            onActivated: index => {
                if (index === count - 1)
                    newCategoryField.forceActiveFocus()
            }
        }
        ThemedTextField {
            id: newCategoryField
            visible: root.creatingCategory
            width: parent.width
            placeholderText: qsTr("YouTube Titles")
        }

        Text {
            visible: root.mode === "save"
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Saved as a new template in My templates. The template you started from stays as it is, and this title keeps its look.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
