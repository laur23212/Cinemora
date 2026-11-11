import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Auto captions. Speech to text on this computer: the selected clips' media is
// transcribed once (Whisper, plus exact word timing when an aligner for the language is
// installed), a caption clip is built from the words, and the transcript of the clip in focus is
// listed below for reading and correcting (EditorState.ai). The first use downloads speech
// recognition, and the AI engine if there is none yet.
Item {
    id: root

    property bool whisperReady: Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
    property bool alignerReady: Addons.hasKind("align-model")
    property var languages: EditorState.whisperLanguages()

    // "Recommended" packs by line width like openai-whisper; the numbered entries cap words per
    // caption on top of that. Same choices as the Subtitles tab.
    readonly property var captionLengthOptions: {
        const options = [{ label: qsTr("Recommended caption length"), words: 0 }]
        options.push({ label: qsTr("1 word per caption"), words: 1 })
        for (let n = 2; n <= 8; ++n)
            options.push({ label: qsTr("%1 words per caption").arg(n), words: n })
        return options
    }

    function indexOfValue(rows, key, value) {
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i][key] === value)
                return i
        }
        return 0
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "whisper-model" && kind !== "onnxruntime" && kind !== "align-model")
                return
            root.whisperReady = Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
            root.alignerReady = Addons.hasKind("align-model")
            root.languages = EditorState.whisperLanguages()
        }
    }

    Component.onCompleted: EditorState.ai.transcriptVisible = true
    Component.onDestruction: EditorState.ai.transcriptVisible = false

    readonly property var info: EditorState.ai.transcriptInfo
    readonly property int lineCount: EditorState.ai.transcriptLines.length

    AiTranscriptView {
        id: transcript
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding

        header: Column {
            id: controls
            width: transcript.width
            spacing: Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingMd

            Text {
                width: controls.width
                wrapMode: Text.WordWrap
                text: qsTr("Turns the speech in the selected clips into captions on the timeline, and lists what is said so you can correct it. Runs on this computer.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // --- first use: download speech recognition ----------------------------
            AiModelDownload {
                width: controls.width
                visible: !root.whisperReady
                addonIds: ["whisper.small", "silero.vad"]
                purpose: qsTr("Auto captions need speech recognition.")
                buttonText: qsTr("Download speech recognition")
            }

            // --- settings and the button ----------------------------------------------
            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: root.whisperReady

                Text {
                    text: qsTr("Language")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedComboBox {
                    id: languageBox
                    width: controls.width
                    enabled: !EditorState.ai.captioning
                    textRole: "label"
                    valueRole: "code"
                    model: root.languages
                    currentIndex: root.indexOfValue(root.languages, "code",
                                                    LayoutMemory.layoutString("ai/captions/language", ""))
                    onActivated: LayoutMemory.setLayoutString("ai/captions/language", currentValue || "")
                }

                Text {
                    text: qsTr("Caption length")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedComboBox {
                    id: wordsBox
                    width: controls.width
                    enabled: !EditorState.ai.captioning
                    textRole: "label"
                    valueRole: "words"
                    model: root.captionLengthOptions
                    currentIndex: root.indexOfValue(root.captionLengthOptions, "words",
                                                    LayoutMemory.layoutNumber("ai/captions/words", 0))
                    onActivated: LayoutMemory.setLayoutNumber("ai/captions/words", currentValue || 0)
                }

                Text {
                    width: controls.width
                    visible: !root.alignerReady && (wordsBox.currentValue || 0) > 0
                    wrapMode: Text.WordWrap
                    text: qsTr("Shorter captions are timed by splitting each phrase evenly, so they can drift slightly out of sync with the speech.")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: root.whisperReady

                ThemedButton {
                    width: controls.width
                    visible: !EditorState.ai.captioning
                    text: qsTr("Create captions")
                    variant: "primary"
                    glyph: Theme.icons.captions
                    enabled: EditorState.ai.canCaption && !EditorState.subtitleGenerating
                    tooltip: EditorState.ai.canCaption
                             ? qsTr("Caption the speech in the selected clips. Several clips become one caption clip.")
                             : qsTr("Select a video or audio clip first")
                    onClicked: EditorState.ai.createCaptions(languageBox.currentValue || "", wordsBox.currentValue || 0)
                }

                Text {
                    width: controls.width
                    visible: !EditorState.ai.captioning && !EditorState.ai.canCaption
                    wrapMode: Text.WordWrap
                    text: qsTr("Select a video or audio clip on the timeline first.")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                // A run in progress: what it is doing, how far it got, and a way to stop it.
                Text {
                    width: controls.width
                    visible: EditorState.ai.captioning
                    elide: Text.ElideRight
                    text: EditorState.ai.captionStatus.length > 0
                          ? EditorState.ai.captionStatus
                          : qsTr("Creating captions… %1%").arg(Math.round(EditorState.ai.captionProgress * 100))
                    color: Theme.c.subtitlesTab.statusForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedProgressBar {
                    width: controls.width
                    visible: EditorState.ai.captioning
                    value: EditorState.ai.captionProgress
                }

                ThemedButton {
                    visible: EditorState.ai.captioning
                    text: qsTr("Cancel")
                    variant: "destructive"
                    glyph: Theme.icons.x
                    tooltip: qsTr("Stop creating captions")
                    onClicked: EditorState.ai.cancelCaptions()
                }
            }

            // Optional: words pinned to the moment they are said, for English speech.
            AiModelDownload {
                id: alignDownload
                width: controls.width
                compact: true
                visible: root.whisperReady && !root.alignerReady && (alignDownload.known || alignDownload.busy)
                addonIds: ["align.wav2vec2-en"]
                purpose: qsTr("Optional: exact word timing for English speech.")
            }

            Rectangle {
                width: controls.width
                height: Theme.c.subtitlesTab.borderWidth
                color: Theme.c.subtitlesTab.border
            }

            // --- the transcript ---------------------------------------------------------
            Column {
                width: controls.width
                spacing: Theme.spacingXs

                Text {
                    width: controls.width
                    elide: Text.ElideRight
                    text: root.info.clipName ? qsTr("Transcript · %1").arg(root.info.clipName) : qsTr("Transcript")
                    color: Theme.c.subtitlesTab.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                Text {
                    width: controls.width
                    wrapMode: Text.WordWrap
                    visible: text.length > 0
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    text: {
                        if (!root.info.clipId)
                            return qsTr("Select a video or audio clip to see what is said in it.")
                        if (!root.info.hasTranscript)
                            return qsTr("No transcript for this clip yet. Create captions to make one.")
                        if (root.lineCount === 0)
                            return qsTr("No speech in this part of the clip.")
                        const bits = []
                        if (root.info.language)
                            bits.push(String(root.info.language).toUpperCase())
                        bits.push(qsTr("%1 words").arg(root.info.words || 0))
                        bits.push(root.info.aligned ? qsTr("exact word timing") : qsTr("estimated word timing"))
                        return bits.join(" · ") + "\n"
                               + qsTr("Click a line to jump to it. Double-click to correct it; the captions follow.")
                    }
                }
            }
        }
    }
}
