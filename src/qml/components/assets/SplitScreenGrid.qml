import QtQuick
import QtQuick.Controls.Basic
import Drift
// Dafin: Templates > Split Screen. One 16:9 card per layout, its cells numbered the way Filmora
// numbers them. Clicking a card fills the cells with the selected clips (see
// AppController::applySplitScreen), top track first.
GridView {
    id: root

    readonly property var layouts: EditorState.splitScreenLayouts()
    readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth * 1.5 + Theme.assetCardGap)))
    readonly property real cardWidth: Math.floor(width / columnCount) - Theme.assetCardGap
    readonly property real cardHeight: Math.round(cardWidth * 9 / 16)

    topMargin: 12
    bottomMargin: 12
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar { }
    cellWidth: Math.floor(width / columnCount)
    cellHeight: cardHeight + 4 + labelMetrics.height + Theme.assetCardGap
    model: layouts

    FontMetrics {
        id: labelMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
    }

    delegate: Column {
        id: card
        required property var modelData
        width: root.cardWidth
        spacing: 4

        Rectangle {
            width: root.cardWidth
            height: root.cardHeight
            radius: Theme.c.effectTemplateCard.radius
            color: hover.hovered ? Theme.c.effectTemplateCard.backgroundHover : Theme.c.effectTemplateCard.background
            border.width: Theme.c.effectTemplateCard.borderWidth
            border.color: Qt.rgba(Theme.c.effectTemplateCard.accent.r, Theme.c.effectTemplateCard.accent.g,
                                  Theme.c.effectTemplateCard.accent.b, 0.35)
            clip: true
            scale: hover.hovered ? 1.03 : 1.0

            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            HoverHandler { id: hover }
            TapHandler {
                onTapped: EditorState.applySplitScreen(card.modelData.id)
            }

            ThemedToolTip {
                text: qsTr("%1 — select the clips (or put them on separate tracks at the playhead), then click")
                          .arg(card.modelData.label)
                visible: hover.hovered
            }

            Canvas {
                id: sheet
                anchors.fill: parent
                anchors.margins: 3
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const accent = Theme.c.effectTemplateCard.accent
                    const gap = 1
                    const cells = card.modelData.cells
                    ctx.font = "600 " + Math.round(height * 0.2) + "px sans-serif"
                    ctx.textAlign = "center"
                    ctx.textBaseline = "middle"
                    for (let i = 0; i < cells.length; ++i) {
                        const c = cells[i]
                        const x = c.x * width, y = c.y * height, w = c.w * width, h = c.h * height
                        ctx.beginPath()
                        for (let k = 0; k < c.corners.length; ++k) {
                            const px = x + c.corners[k].x * w
                            const py = y + c.corners[k].y * h
                            if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                        }
                        ctx.closePath()
                        ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, i % 2 === 0 ? 0.30 : 0.18)
                        ctx.fill()
                        ctx.lineWidth = gap * 2
                        ctx.strokeStyle = Theme.c.effectTemplateCard.background
                        ctx.stroke()
                        ctx.fillStyle = Theme.c.effectTemplateCard.foregroundOnMedia
                        ctx.fillText(String(i + 1), x + w / 2, y + h / 2)
                    }
                }
                Connections {
                    target: hover
                    function onHoveredChanged() { sheet.requestPaint() }
                }
            }
        }

        Text {
            width: root.cardWidth
            text: card.modelData.label
            color: Theme.c.effectTemplateCard.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeCard
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }
}
