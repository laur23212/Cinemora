#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QVector>

#include <memory>

class PlaybackEngine;

// Displays the compositor's output. The frame is normally already a GL texture in
// the shared context, so it is wrapped with fromNative — no copy, no upload. Only
// when there is no shared context (Qt Quick on a non-OpenGL API, or an Android driver
// that refused sharing; see GpuFrameTexture) does the compositor publish a QImage
// instead, which is uploaded here per frame.
//
// Frames are pulled from PlaybackEngine in C++ — QML cannot reliably assign QImage
// between properties, which left the Android preview permanently blank.
class PreviewItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(PlaybackEngine *playback READ playback WRITE setPlayback NOTIFY playbackChanged)
    // PreviewStream: 0 is the scene camera's picture, 1 the editor's free viewpoint. Only a
    // Camera item paces the engine to the display.
    Q_PROPERTY(int stream READ stream WRITE setStream NOTIFY streamChanged)
    // Dafin: false for a second picture of the same stream (the Advanced Text Edit window), so
    // only one item paces the engine to the display; two would count every refresh twice.
    Q_PROPERTY(bool pacesDisplay READ pacesDisplay WRITE setPacesDisplay NOTIFY pacesDisplayChanged)
    Q_PROPERTY(int textureId READ textureId WRITE setTextureId NOTIFY frameChanged)
    Q_PROPERTY(QSize textureSize READ textureSize WRITE setTextureSize NOTIFY frameChanged)
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY frameChanged)

public:
    explicit PreviewItem(QQuickItem *parent = nullptr);
    ~PreviewItem() override;

    PlaybackEngine *playback() const { return m_playback; }
    void setPlayback(PlaybackEngine *engine);

    int stream() const { return m_stream; }
    void setStream(int stream);

    bool pacesDisplay() const { return m_pacesDisplay; }
    void setPacesDisplay(bool paces);

    int textureId() const { return m_textureId; }
    void setTextureId(int id);

    QSize textureSize() const { return m_textureSize; }
    void setTextureSize(const QSize &size);

    QImage image() const { return m_image; }
    void setImage(const QImage &image);

signals:
    void frameChanged();
    void playbackChanged();
    void streamChanged();
    void pacesDisplayChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    void pullFrame();
    void onStreamFrameChanged(int stream);
    // (Re)binds the display-cadence signals of whatever window this item is in to the engine,
    // and reports that screen's refresh rate. Both halves have to be re-done on every window
    // and screen change, which is why they live together here.
    void bindDisplayCadence();
    void reportRefreshRate();

    // Dafin: the texture this item holds a claim on (PresentClaims.h) — the frame it pulled
    // last — and whether a sync has bound it, i.e. whether the render thread may be drawing it.
    // A claim superseded before any sync bound it is dropped at once; one that was drawn is
    // handed to the render thread at the next sync and dropped after that frame's swap, with a
    // fence, so the compositor never redraws a texture the GPU is still sampling.
    void bindSwapRelease();
    void releaseAllClaims();
    struct SwapRelease;
    std::shared_ptr<SwapRelease> m_swapRelease;
    QMetaObject::Connection m_swapReleaseConn;
    unsigned m_claimed = 0;
    bool m_claimSynced = false;
    QVector<unsigned> m_pendingRelease;

    QPointer<PlaybackEngine> m_playback;
    // Held so the connections can be dropped when the item moves to another window; the
    // window itself may be gone by then, so the QMetaObject::Connection is what we keep
    // rather than a pointer to disconnect from.
    QMetaObject::Connection m_afterAnimatingConn;
    QMetaObject::Connection m_frameSwappedConn;
    QMetaObject::Connection m_screenChangedConn;
    QMetaObject::Connection m_refreshRateConn;
    QPointer<QQuickWindow> m_cadenceWindow;
    int m_stream = 0;
    bool m_pacesDisplay = true;
    int m_textureId = 0;
    QSize m_textureSize;
    QImage m_image;
    // Last native id / image identity wrapped into the scene-graph node. Reused across
    // geometry updates so fromNative/createTextureFromImage only runs when the compositor
    // publishes a new presentation-ring slot or readback frame.
    int m_boundTextureId = 0;
    QSize m_boundTextureSize;
    qint64 m_boundImageCacheKey = 0;
};
