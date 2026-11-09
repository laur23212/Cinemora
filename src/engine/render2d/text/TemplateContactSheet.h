#pragma once

#include "core/base/Time.h"
#include "core/text/TextStyle.h"
#include "core/text/animation/TextAnimator.h"

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

// Dafin: a title template's motion as one picture, for its author (an AI or a designer) to look
// at without running the editor. The text is drawn by the same pipeline the compositor uses for a
// text clip (layout, animation with the project frame rate, Skia painter), rasterised on the CPU,
// and the whole-block motion (fade, pop, slide) applied as renderTextCard does. Clip effects are
// GPU shaders and are not drawn; the sheet says so.

namespace drift::skia {

struct ContactSheetOptions
{
    QString text;                    // empty = the template's sampleText
    int fps = 30;
    TimeUs durationUs = 3'000'000;   // the clip's length
    QSize cell{640, 360};            // one frame; the text is laid out at 1080 p scale
    QList<TimeUs> times;             // empty = the events: first frame, mid-entrance, readable,
                                     // early hold, two loop phases, mid-exit, last frame
};

struct ContactSheetResult
{
    QImage sheet;                    // a dark-background row over a light-background row
    textanim::FittedTiming timing;   // as played on this clip
    QList<TimeUs> times;             // snapped to frames
    QList<int> frames;
};

ContactSheetResult renderTemplateContactSheet(const TextPreset &preset, const ContactSheetOptions &options);

} // namespace drift::skia
