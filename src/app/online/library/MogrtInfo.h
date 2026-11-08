#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <optional>

// What Dafin can read from an Adobe Motion Graphics Template (.mogrt) without After Effects.
//
// A .mogrt is a zip: definition.json (name, the Essential Graphics controls and their default
// values), a poster image, often a short preview video, and the After Effects or Premiere project
// that actually draws the animation. That project only Adobe's engine can render, so Dafin keeps
// the template's name, poster and preview for the Titles panel and turns its text fields into an
// ordinary editable Dafin title when it is added to the timeline.
namespace dafin::assets::mogrt {

struct TextField
{
    QString label;   // the control's name in the Essential Graphics panel ("Title", "Name")
    QString text;    // its default text
};

struct Info
{
    QString title;
    QList<TextField> texts;
    double durationSeconds = 0;  // 0 when the template does not say
    int width = 0;
    int height = 0;
    QString authorApp;           // "aefx" (After Effects) or "ppro" (Premiere Pro) when known
    QString posterEntry;         // archive path of the poster image, if any
    QString previewEntry;        // archive path of an embedded preview video, if any
};

// Empty with `error` set when the file is not a readable template.
std::optional<Info> read(const QString &path, QString *error = nullptr);

// Copies one archive entry to `destPath`. Refuses entries larger than `maxBytes`.
bool extract(const QString &archivePath, const QString &entryPath, const QString &destPath,
             qint64 maxBytes = 256LL * 1024 * 1024);

// The text the title clip starts with: every field's default on its own line, or the template's
// name when it has no text fields.
QString titleText(const Info &info);

// The built-in text style that suits the template: lower thirds keep their bottom-left look.
QString titlePreset(const Info &info);

} // namespace dafin::assets::mogrt
