#pragma once

#include "core/text/TextTemplate.h"

#include <QString>
#include <QStringList>

// Dafin: a template author's whole round in one call (`dafin --check-template`): the validator in
// normal and portable mode, the quick-control checks, the timing at common frame rates and clip
// lengths, and contact sheets to look at — written to a folder with a report that says what was
// checked and what still needs a person's eyes. It never approves a template: passing means the
// file installs and its controls work, not that it looks right.

namespace drift::skia {

struct TemplateCheckOptions
{
    QString outDir; // created when missing
    QString text;   // empty = the template's sampleText
};

struct TemplateCheckResult
{
    bool automatedChecksPassed = false;
    QString reportPath; // <outDir>/<file name>-report.md
    QStringList sheetPaths;
    QString report;     // the report's Markdown
};

TemplateCheckResult runTemplateCheck(const QString &templateFile, const TemplateEnvironment &environment,
                                     const TemplateEnvironment &portableEnvironment,
                                     const TemplateCheckOptions &options);

} // namespace drift::skia
