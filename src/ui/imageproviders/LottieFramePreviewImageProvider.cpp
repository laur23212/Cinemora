#include "ui/imageproviders/LottieFramePreviewImageProvider.h"

#include "engine/render2d/vector/VectorClipRenderer.h"
#include "engine/render2d/vector/VectorInspect.h"

#include <QUrl>
#include <QUrlQuery>

LottieFramePreviewImageProvider::LottieFramePreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage LottieFramePreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int q = id.indexOf('?');
    const QString path = QUrl::fromPercentEncoding((q < 0 ? id : id.left(q)).toUtf8());
    const double t = q < 0 ? 0.35 : qBound(0.0, QUrlQuery(id.mid(q + 1)).queryItemValue(QStringLiteral("t")).toDouble(), 1.0);

    const QSize target(requestedSize.width() > 0 ? requestedSize.width() : 192,
                       requestedSize.height() > 0 ? requestedSize.height() : 108);

    drift::VectorSource source;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_sources.constFind(path);
        if (it == m_sources.constEnd()) {
            drift::VectorSource probe;
            probe.kind = drift::VectorKind::Lottie;
            probe.path = path;
            probe.fit = drift::VectorFit::Contain;
            probe.loop = drift::VectorLoop::Loop;
            if (!drift::vec::probeVectorSource(probe))
                probe.path.clear(); // remember the failure; draw nothing for it
            it = m_sources.insert(path, probe);
        }
        source = *it;
    }

    QImage image;
    if (!source.path.isEmpty())
        image = drift::vec::renderThumbnail(source, target, t);
    if (image.isNull()) {
        image = QImage(target, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size)
        *size = image.size();
    return image;
}
