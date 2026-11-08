#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>

// Dafin: the editable side of a title made from a Motion Graphics Template (.mogrt).
//
// The clip itself is an ordinary video clip that plays a rendered, transparent video of the
// template (Clip::path); that render is a cache and nothing more. What the title *is* lives here:
// the template, the controls it offers and the values the user gave them. Change a value, the
// clip's length or the project's frame size and a new render is made from these; undo, duplicate,
// save and reopen all carry them along. See MogrtTitleController.
namespace drift {

struct MogrtSource
{
    // Content hash of the .mogrt (also its file name in Dafin's template store).
    QString templateId;
    // The managed copy of the template (<AppData>/mogrt/templates/<templateId>.mogrt, or the copy a
    // project bundle carried). Re-pointed with the project's other media when it moves.
    QString templatePath;
    QString name;
    // "aefx" for After Effects, "ppro" for Premiere Pro, empty when the template does not say.
    QString authorApp;

    // The controls the template exposes, as Dafin read them from its definition and the renderer
    // confirmed: [{id, type, label, default, min, max}]. Type is one of the converter's names
    // (text, slider, angle, checkbox, color, point, point3d, scale, dropdown, group, section).
    QJsonArray controls;
    // The user's values, by control id. A control with no value here uses its default.
    QJsonObject values;

    // The template's own composition.
    int width = 0;
    int height = 0;
    double fps = 0;
    int64_t templateDurationUs = 0;

    // The render Clip::path plays: the cache key it was made for and its quality ("preview" or
    // "full"). A key that no longer matches the clip means the render is out of date.
    QString renderKey;
    QString renderQuality;

    // "ok", "warnings" (rendered, with parts the renderer could not draw), "unsupported" (the
    // renderer cannot draw this template), "failed" (the last render failed), "missing" (the
    // template file is gone), or empty before the first render.
    QString status;
    QStringList warnings;
    QString lastError;

    bool isEmpty() const { return templateId.isEmpty(); }

    QJsonObject toJson() const;
    static MogrtSource fromJson(const QJsonObject &o);
};

} // namespace drift
