#include "engine/render2d/text/TemplateContactSheet.h"

#include "core/text/animation/TextAnimationPreset.h"
#include "engine/render2d/text/SkiaTextPainter.h"
#include "engine/render2d/text/TextLayout.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace drift::skia {
namespace {

// SkiaRuntime::rasterize without the GL runtime around it: the CPU path, any thread.
QImage rasterize(const VectorPainter &painter)
{
    const QSize size = painter.size();
    if (size.isEmpty())
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
        SkImageInfo::Make(size.width(), size.height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType), image.bits(),
        image.bytesPerLine(), &props);
    if (!surface)
        return {};
    painter.paint(*surface->getCanvas());
    return image;
}

} // namespace

// A dark and a bright, busy frame: the bright one is the hard case for light fills.
QImage templateSheetBackground(const QSize &size, bool light)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    QLinearGradient g(0, 0, size.width(), size.height());
    if (light) {
        g.setColorAt(0.0, QColor(0xbf, 0xe3, 0xff));
        g.setColorAt(0.55, QColor(0xff, 0xf6, 0xdc));
        g.setColorAt(1.0, QColor(0xf2, 0xd9, 0xa0));
    } else {
        g.setColorAt(0.0, QColor(0x10, 0x13, 0x1a));
        g.setColorAt(1.0, QColor(0x2a, 0x2f, 0x3a));
    }
    p.fillRect(img.rect(), g);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, light ? 200 : 25));
    p.drawEllipse(QPointF(size.width() * 0.62, size.height() * 0.45), size.width() * 0.12, size.height() * 0.18);
    return img;
}

// One frame's text as fillTextLayer builds it for a text clip laid out like a sheet cell.
TemplateFrame templateFrameAt(const TextStyle &style, const QString &text, TimeUs durationUs, const QSize &size, int fps,
                              TimeUs t)
{
    TemplateFrame out;
    out.renderScale = size.height() / 1080.0;
    out.layoutRect = QRectF(size.width() * 0.08, size.height() * 0.25, size.width() * 0.84, size.height() * 0.5);
    TextPaintRequest request;
    request.style = style.isAnimated() ? style.resolvedAt(t) : style;
    const ResolvedTextAnimation anim = resolveTextAnimation(request.style.animation);
    request.set = text::fragmentsFor(text, request.style, out.layoutRect.width(), out.layoutRect.height(),
                                     out.renderScale, -1, text::splitFor(anim.resolved, request.style));
    if (!request.set || request.set->frags.isEmpty())
        return out;
    textanim::EvalContext ctx = text::evalContextFor(request.style, out.layoutRect, out.renderScale, 0, durationUs, t, -1);
    ctx.fps = fps;
    request.frame = textanim::evaluateTextAnimation(anim.set, anim.resolved, request.set->infos, request.set->domains, ctx);
    request.envelope = textanim::animationBounds(anim.resolved, ctx);
    request.layoutRect = out.layoutRect;
    request.renderScale = out.renderScale;
    request.timeSec = usToSeconds(t);
    request.frameRate = fps;
    request.anchorGrouping = anim.set.anchorGrouping;
    request.anchorAlignment = anim.set.anchorAlignment;
    const TextPainterResult painted = makeTextPainter(request);
    out.painter = painted.painter;
    out.rect = painted.rect;
    out.block = request.frame.block;
    return out;
}

// That frame over `bg`, with the compositor's block motion applied on the CPU.
QImage renderTemplateFrame(const TextStyle &style, const QString &text, TimeUs durationUs, const QSize &size, int fps,
                           TimeUs t, const QImage &bg)
{
    QImage card = bg.copy();
    const TemplateFrame frame = templateFrameAt(style, text, durationUs, size, fps, t);
    if (!frame.painter)
        return card;
    const textanim::BlockProps &block = frame.block;
    if (block.opacity <= 0.001)
        return card;
    QImage raster = rasterize(*frame.painter);
    if (block.blurPx > 0.5 && !raster.isNull()) {
        // The compositor blurs the layer on the GPU; a down/up-scale stands in, as on cards.
        const double shrink = 1.0 + block.blurPx * frame.renderScale / 2.0;
        const QSize small(qMax(1, int(raster.width() / shrink)), qMax(1, int(raster.height() / shrink)));
        raster = raster.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .scaled(raster.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QPainter p(&card);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(block.opacity);
    const QPointF centre = frame.rect.center() + QPointF(block.dx, block.dy);
    p.translate(centre);
    p.rotate(block.rotation);
    p.scale(block.scale, block.scale);
    p.translate(-frame.rect.center());
    p.drawImage(frame.rect.topLeft(), raster);
    return card;
}

ContactSheetResult renderTemplateContactSheet(const TextPreset &preset, const ContactSheetOptions &o)
{
    ContactSheetResult result;
    const QString text = !o.text.isEmpty() ? o.text : preset.sampleText.isEmpty() ? QStringLiteral("Title") : preset.sampleText;
    const int fps = qMax(1, o.fps);
    const TimeUs duration = qMax<TimeUs>(1, o.durationUs);
    const ResolvedTextAnimation anim = resolveTextAnimation(preset.style.animation);
    result.timing = textanim::fittedTiming(anim.set, anim.resolved, textanim::estimateDomains(text), duration, fps);

    QList<TimeUs> times = o.times;
    if (times.isEmpty()) {
        const TimeUs readable = result.timing.readableAtUs;
        const TimeUs outStart = duration - result.timing.outUs;
        times << 0 << readable / 2 << readable << readable + (outStart - readable) / 4
              << readable + (outStart - readable) / 2 << readable + 3 * (outStart - readable) / 4
              << duration - qMax<TimeUs>(result.timing.outUs / 2, 1) << duration - 1;
    }
    // The frame the compositor shows nearest each instant.
    const double frameUs = 1e6 / fps;
    for (TimeUs t : times) {
        const int f = qBound(0, int(std::llround(double(t) / frameUs)), int(std::floor((duration - 1) / frameUs)));
        if (result.frames.contains(f))
            continue; // no exit, or a clip too short to tell two instants apart
        result.frames << f;
        result.times << TimeUs(std::llround(f * frameUs));
    }

    const QSize cell = o.cell.isValid() ? o.cell : QSize(640, 360);
    const int labelH = 22;
    const int headerH = 30;
    const int cols = int(result.times.size());
    result.sheet = QImage(QSize(cols * cell.width(), headerH + 2 * (cell.height() + labelH)), QImage::Format_ARGB32_Premultiplied);
    result.sheet.fill(QColor(0x0b, 0x0b, 0x0b));
    QPainter p(&result.sheet);
    QFont font = p.font();
    font.setPixelSize(13);
    p.setFont(font);
    p.setPen(QColor(0xdd, 0xdd, 0xdd));
    p.drawText(QRect(8, 0, result.sheet.width() - 16, headerH), Qt::AlignVCenter,
               QStringLiteral("%1 (%2) · \"%3\" · clip %4 ms at %5 fps · readable at %6 ms (entrance %7 of %8 ms, exit %9 of %10 ms)%11 · "
                              "Dafin's text painter on the CPU; clip effects are not drawn")
                   .arg(preset.label, preset.id, text)
                   .arg(duration / 1000)
                   .arg(fps)
                   .arg(result.timing.readableAtUs / 1000.0, 0, 'f', 1)
                   .arg(result.timing.inUs / 1000.0, 0, 'f', 1)
                   .arg(result.timing.inNaturalUs / 1000.0, 0, 'f', 1)
                   .arg(result.timing.outUs / 1000.0, 0, 'f', 1)
                   .arg(result.timing.outNaturalUs / 1000.0, 0, 'f', 1)
                   .arg(result.timing.fitted ? QStringLiteral(", fitted to the clip") : QString()));
    for (int row = 0; row < 2; ++row) {
        const QImage bg = templateSheetBackground(cell, row == 1);
        for (int c = 0; c < cols; ++c) {
            const int x = c * cell.width();
            const int y = headerH + row * (cell.height() + labelH);
            p.drawImage(x, y + labelH, renderTemplateFrame(preset.style, text, duration, cell, fps, result.times.at(c), bg));
            p.setPen(QColor(0xdd, 0xdd, 0xdd));
            p.drawText(QRect(x + 6, y, cell.width() - 12, labelH), Qt::AlignVCenter,
                       QStringLiteral("%1 ms · frame %2").arg(result.times.at(c) / 1000.0, 0, 'f', 1).arg(result.frames.at(c)));
            p.setPen(QColor(0x44, 0x44, 0x44));
            p.drawLine(x, y + labelH, x, y + labelH + cell.height());
        }
    }
    return result;
}

} // namespace drift::skia
