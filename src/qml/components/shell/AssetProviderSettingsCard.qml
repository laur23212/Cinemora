import QtQuick
import Drift

// One provider in Settings > Asset Providers: status, what the account is for, the sign-in form
// (fields come from the provider), and Connect / Reconnect / Log out. Saved secrets are never
// shown — a saved field reads "Saved" and stays empty unless the user types a new value.
Rectangle {
    id: root

    required property var provider       // OnlineAssets.provider(id) map
    property string lastMessage: ""
    property bool awaitingCode: false

    readonly property string state_: provider.state || "notConnected"
    readonly property bool stored: (provider.storedFields || []).length > 0
    readonly property bool busy: !!provider.busy

    implicitHeight: content.implicitHeight + Theme.spacingMd * 2
    radius: Theme.radiusMd
    color: Theme.c.assetsPanel.pickerBackground
    border.width: 1
    border.color: state_ === "expired" ? "#e67e22" : Theme.panelBorder

    function fieldValues() {
        const out = {}
        for (let i = 0; i < fieldsRepeater.count; ++i) {
            const f = fieldsRepeater.itemAt(i)
            if (f && f.value.length > 0)
                out[f.fieldId] = f.value
        }
        return out
    }
    function clearFields() {
        for (let i = 0; i < fieldsRepeater.count; ++i) {
            const f = fieldsRepeater.itemAt(i)
            if (f) f.clear()
        }
    }

    Connections {
        target: OnlineAssets
        function onAuthFinished(id, ok, message) {
            if (id !== root.provider.id)
                return
            root.lastMessage = message
            if (ok) {
                root.clearFields()
                root.awaitingCode = false
            }
        }
    }

    Column {
        id: content
        x: Theme.spacingMd
        y: Theme.spacingMd
        width: parent.width - Theme.spacingMd * 2
        spacing: Theme.spacingSm

        // ── header: name + status pill ──
        Item {
            width: parent.width
            height: Math.max(nameText.implicitHeight, pill.height)

            Text {
                id: nameText
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.provider.name
                color: Theme.panelForeground
                font.pixelSize: Theme.fontSizeBase + 1
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: nameText.right
                anchors.leftMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                text: (root.provider.kinds || []).map(k => ({ video: qsTr("Video"), music: qsTr("Music"), sfx: qsTr("Sound effects"),
                                                             sticker: qsTr("Stickers"), title: qsTr("Titles") })[k] || k).join(" · ")
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
            }
            Rectangle {
                id: pill
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: pillText.implicitHeight + 6
                width: pillText.implicitWidth + (root.busy ? 30 : 14)
                radius: height / 2
                color: root.state_ === "connected" ? "#2622c37a"
                     : root.state_ === "expired" ? "#33e67e22"
                     : root.state_ === "error" ? "#33c0392b"
                     : "#22888888"
                CircularProgress {
                    visible: root.busy
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    size: 12
                    indeterminate: true
                }
                Text {
                    id: pillText
                    anchors.right: parent.right
                    anchors.rightMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.DemiBold
                    color: Theme.panelForeground
                    text: root.busy ? qsTr("Checking…")
                        : root.state_ === "connected" ? (root.provider.signedIn ? qsTr("Signed in") : qsTr("Connected"))
                        : root.state_ === "expired" ? qsTr("Sign-in expired")
                        : root.state_ === "error" ? qsTr("Problem")
                        : root.state_ === "notRequired" ? qsTr("No account needed")
                        : qsTr("Not connected")
                }
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.panelForeground
            font.pixelSize: Theme.fontSizeSm
            text: root.provider.description + (root.provider.license ? "  " + qsTr("Licence: %1.").arg(root.provider.license) : "")
        }
        Text {
            width: parent.width
            visible: !!root.provider.nonCommercialOnly
            wrapMode: Text.WordWrap
            color: "#e67e22"
            font.pixelSize: Theme.fontSizeXs
            text: qsTr("The %1 API may only be used for non-commercial projects.").arg(root.provider.name)
        }
        Text {
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: Theme.mutedForeground
            font.pixelSize: Theme.fontSizeXs
            text: root.provider.accountHint || ""
        }

        // ── sign-in form ──
        Repeater {
            id: fieldsRepeater
            model: root.provider.fields || []
            delegate: Row {
                id: fieldRow
                required property var modelData
                readonly property string fieldId: modelData.id
                readonly property string value: input.text.trim()
                readonly property bool saved: (root.provider.storedFields || []).indexOf(modelData.id) >= 0
                function clear() { input.text = "" }
                width: content.width
                spacing: Theme.spacingSm
                Text {
                    width: Math.min(190, content.width * 0.36)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: fieldRow.modelData.label + (fieldRow.modelData.optional ? "" : " *")
                    color: Theme.panelForeground
                    font.pixelSize: Theme.fontSizeSm
                }
                ThemedTextField {
                    id: input
                    width: content.width - Math.min(190, content.width * 0.36) - Theme.spacingSm
                    echoMode: fieldRow.modelData.secret ? TextInput.Password : TextInput.Normal
                    placeholderText: fieldRow.saved ? qsTr("Saved — type to replace") : (fieldRow.modelData.placeholder || "")
                    onAccepted: connectButton.clicked()
                }
            }
        }

        // OAuth step 2: Freesound shows a code after the user allows access.
        Row {
            visible: root.awaitingCode
            width: parent.width
            spacing: Theme.spacingSm
            ThemedTextField {
                id: codeField
                width: parent.width - finishButton.width - Theme.spacingSm
                placeholderText: qsTr("Paste the code %1 showed you").arg(root.provider.name)
                onAccepted: finishButton.clicked()
            }
            ThemedButton {
                id: finishButton
                variant: "primary"
                text: qsTr("Finish sign-in")
                enabled: codeField.text.trim().length > 0 && !root.busy
                onClicked: {
                    OnlineAssets.completeSignIn(root.provider.id, codeField.text)
                    codeField.text = ""
                }
            }
        }

        // ── actions ──
        Flow {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedButton {
                id: connectButton
                visible: (root.provider.fields || []).length > 0
                variant: root.stored ? "secondary" : "primary"
                text: root.stored ? qsTr("Save changes") : (root.provider.supportsRegistration ? qsTr("Connect or register") : qsTr("Connect"))
                enabled: !root.busy && Object.keys(root.fieldValues()).length > 0
                onClicked: OnlineAssets.connectProvider(root.provider.id, root.fieldValues())
            }
            ThemedButton {
                visible: !!root.provider.supportsOAuth
                variant: "secondary"
                glyph: Theme.icons.externalLink
                text: root.provider.signedIn ? qsTr("Sign in again") : qsTr("Sign in for original files")
                enabled: !root.busy && ((root.provider.storedFields || []).indexOf("clientId") >= 0 || root.fieldValues().clientId !== undefined)
                onClicked: {
                    if (OnlineAssets.beginSignIn(root.provider.id, root.fieldValues()))
                        root.awaitingCode = true
                    else
                        root.lastMessage = qsTr("Add the client ID (and secret) first.")
                }
            }
            ThemedButton {
                visible: root.stored
                variant: "secondary"
                glyph: Theme.icons.refresh
                text: qsTr("Reconnect")
                enabled: !root.busy
                onClicked: OnlineAssets.reconnect(root.provider.id)
            }
            ThemedButton {
                visible: root.stored
                variant: "ghost"
                glyph: Theme.icons.x
                text: qsTr("Log out")
                enabled: !root.busy
                onClicked: {
                    OnlineAssets.logout(root.provider.id)
                    root.lastMessage = qsTr("Logged out. Saved sign-in removed.")
                }
            }
            ThemedButton {
                visible: (root.provider.signupUrl || "").length > 0 && !root.stored
                variant: "ghost"
                glyph: Theme.icons.externalLink
                text: qsTr("Get a free key")
                onClicked: OnlineAssets.openSignupPage(root.provider.id)
            }
        }

        Text {
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeXs
            color: root.state_ === "expired" || root.state_ === "error" ? "#e67e22" : Theme.mutedForeground
            text: root.lastMessage.length > 0 ? root.lastMessage : (root.provider.stateMessage || "")
        }
    }
}
