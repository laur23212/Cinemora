#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include "core/project/sources/VectorSource.h"

// One frame of a Lottie file on disk, drawn by Drift's own vector renderer, for the Stock Media
// tab's hover previews. Requested as image://lottieframe/<percent-encoded path>?t=<0..1>; the
// card asks for frames on a timer to animate. Probed documents are cached per path.
class LottieFramePreviewImageProvider : public QQuickImageProvider
{
public:
    LottieFramePreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QHash<QString, drift::VectorSource> m_sources;
};
