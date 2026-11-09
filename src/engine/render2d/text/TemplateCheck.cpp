#include "engine/render2d/text/TemplateCheck.h"

#include "core/text/animation/TextAnimationPreset.h"
#include "core/text/animation/TextAnimator.h"
#include "engine/render2d/text/TemplateContactSheet.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>
#include <cstdlib>

namespace drift::skia {
namespace {

// The words split onto two lines at the space nearest the middle.
QString twoLines(const QString &text)
{
    const int middle = text.size() / 2;
    int best = -1;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char(' ') && (best < 0 || std::abs(i - middle) < std::abs(best - middle)))
            best = i;
    }
    if (best < 0)
        return text + QStringLiteral("\nSECOND LINE");
    QString out = text;
    out[best] = QLatin1Char('\n');
    return out;
}

QString ms(TimeUs us)
{
    return QString::number(us / 1000.0, 'f', 1);
}

QString cell(const QString &s)
{
    QString out = s;
    return out.replace(QLatin1Char('|'), QStringLiteral("\\|")).replace(QLatin1Char('\n'), QLatin1Char(' '));
}

void writeIssues(QTextStream &s, const QString &mode, const QList<TemplateIssue> &issues)
{
    s << "- **" << mode << "**: " << (hasTemplateErrors(issues) ? "NOT INSTALLABLE" : "OK");
    if (issues.isEmpty()) {
        s << ", no issues\n";
        return;
    }
    s << "\n";
    for (const TemplateIssue &i : issues)
        s << "  - " << cell(templateIssueToText(i)) << "\n";
}

} // namespace

TemplateCheckResult runTemplateCheck(const QString &templateFile, const TemplateEnvironment &environment,
                                     const TemplateEnvironment &portableEnvironment,
                                     const TemplateCheckOptions &options)
{
    TemplateCheckResult result;
    const QString name = QFileInfo(templateFile).completeBaseName();
    QDir().mkpath(options.outDir);
    const QDir out(options.outDir);
    result.reportPath = out.filePath(name + QStringLiteral("-report.md"));

    QList<TemplateIssue> issues;
    QList<TemplateIssue> portableIssues;
    QJsonObject json;
    QFile file(templateFile);
    if (!file.open(QIODevice::ReadOnly)) {
        TemplateIssue issue;
        issue.code = QStringLiteral("schema");
        issue.message = QStringLiteral("cannot open the file");
        issues << issue;
        portableIssues << issue;
    } else {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (!doc.isObject()) {
            TemplateIssue issue;
            issue.code = QStringLiteral("schema");
            issue.message = QStringLiteral("not valid JSON: %1 at offset %2").arg(parseError.errorString()).arg(parseError.offset);
            issues << issue;
            portableIssues << issue;
        } else {
            json = doc.object();
            issues = validateTitleTemplate(json, environment);
            portableIssues = validateTitleTemplate(json, portableEnvironment);
        }
    }
    const bool installable = !hasTemplateErrors(issues) && !hasTemplateErrors(portableIssues);
    const TextPreset preset = installable ? titleTemplateFromJson(json) : TextPreset{};
    const QList<TemplateControlCheck> controls = installable ? checkTemplateControls(preset, environment)
                                                             : QList<TemplateControlCheck>{};
    const bool controlsOk = std::all_of(controls.cbegin(), controls.cend(), [](const TemplateControlCheck &c) { return c.ok; });
    result.automatedChecksPassed = installable && controlsOk;

    QString report;
    QTextStream s(&report);
    const QString label = preset.label.isEmpty() ? json.value(QStringLiteral("label")).toString() : preset.label;
    const QString id = preset.id.isEmpty() ? json.value(QStringLiteral("id")).toString() : preset.id;
    s << "# Template check: " << (label.isEmpty() ? name : label) << (id.isEmpty() ? QString() : QStringLiteral(" (`%1`)").arg(id))
      << "\n\n";
    s << "File: `" << QFileInfo(templateFile).fileName() << "`";
#ifdef DRIFT_VERSION
    s << " · checked by Dafin " << DRIFT_VERSION;
#endif
    s << "\n\n## Result\n\n";
    s << "- Automated checks: **" << (result.automatedChecksPassed ? "passed" : "FAILED") << "**";
    if (!installable)
        s << " — the validator found errors (section 1); nothing else was run";
    else if (!controlsOk)
        s << " — a quick control does not work (section 2)";
    s << "\n- Visual review: **not done**. Look at every contact sheet in section 4 before approving: this command "
         "checks that the file installs and its controls work, never that it looks right.\n\n";

    s << "## 1. Validator\n\n";
    writeIssues(s, QStringLiteral("This installation"), issues);
    writeIssues(s, QStringLiteral("Portable (for other machines)"), portableIssues);
    s << "\n";
    if (!installable) {
        s << "Fix the errors above and run the check again.\n";
        result.report = report;
        QFile f(result.reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(report.toUtf8());
        return result;
    }

    s << "## 2. Quick controls\n\n";
    if (controls.isEmpty()) {
        s << "The template has no quick controls.\n\n";
    } else {
        s << "Each control: its binding resolves, a different value changes the template, reads back, leaves the "
             "other controls alone and survives a save and reopen, and reset restores the template's value.\n\n";
        s << "| control | type | bind | template value | tried | result |\n|---|---|---|---|---|---|\n";
        for (const TemplateControlCheck &c : controls)
            s << "| " << cell(c.controlId) << " | " << c.type << " | `" << cell(c.bind) << "` | " << cell(c.templateValue.toString())
              << " | " << cell(c.testValue.toString()) << " | " << (c.ok ? QStringLiteral("works") : QStringLiteral("**") + cell(c.problem) + QStringLiteral("**"))
              << " |\n";
        s << "\n";
    }

    const QString text = !options.text.isEmpty() ? options.text
                         : !preset.sampleText.isEmpty() ? preset.sampleText
                                                        : QStringLiteral("Title");
    s << "## 3. Timing\n\n";
    s << "Milliseconds until the whole text is at rest (\"" << cell(text) << "\"), then the entrance and exit as played. "
         "Fitted: on a short clip the entrance and exit share half of it.\n\n";
    s << "| clip | 24 fps | 30 fps | 60 fps |\n|---|---|---|---|\n";
    const ResolvedTextAnimation anim = resolveTextAnimation(preset.style.animation);
    const textanim::Domains domains = textanim::estimateDomains(text);
    for (const TimeUs clip : {TimeUs(400'000), TimeUs(1'000'000), TimeUs(3'000'000)}) {
        s << "| " << clip / 1000 << " ms";
        for (const int fps : {24, 30, 60}) {
            const textanim::FittedTiming t = textanim::fittedTiming(anim.set, anim.resolved, domains, clip, fps);
            s << " | " << ms(t.readableAtUs) << " (in " << ms(t.inUs) << ", out " << ms(t.outUs) << (t.fitted ? ", fitted" : "") << ")";
        }
        s << " |\n";
    }
    s << "\n";

    struct Sheet
    {
        QString suffix;
        QString what;
        QString text;
        int fps;
        TimeUs duration;
    };
    const QList<Sheet> sheets{
        {QStringLiteral("30fps"), QStringLiteral("A 3 s clip at 30 fps: first frame, mid-entrance, readable, the hold with any loop, mid-exit, last frame"), text, 30, 3'000'000},
        {QStringLiteral("60fps"), QStringLiteral("The same at 60 fps"), text, 60, 3'000'000},
        {QStringLiteral("24fps-short"), QStringLiteral("A 0.4 s clip at 24 fps, the shortest common case: entrance and exit share half of it"), text, 24, 400'000},
        {QStringLiteral("two-lines"), QStringLiteral("The text on two lines (30 fps, 3 s)"), twoLines(text), 30, 3'000'000},
        {QStringLiteral("long"), QStringLiteral("A long text that has to wrap (30 fps, 3 s)"),
         text + QStringLiteral(" and a much longer line that has to wrap"), 30, 3'000'000},
    };
    s << "## 4. Contact sheets\n\n";
    s << "Top row on a dark background, bottom row on a bright, busy one; each frame is labelled with its time and "
         "frame number. Drawn with Dafin's own text painter on the CPU, which matches the editor's GPU preview and "
         "export up to antialiasing.";
    if (!preset.effects.isEmpty()) {
        QStringList ids;
        for (const Effect &e : preset.effects)
            ids << e.catalogId;
        s << " **Clip effects are not drawn** (" << cell(ids.join(QStringLiteral(", "))) << "): judge them in the editor.";
    }
    s << "\n\n| file | what it shows |\n|---|---|\n";
    for (const Sheet &sheet : sheets) {
        ContactSheetOptions o;
        o.text = sheet.text;
        o.fps = sheet.fps;
        o.durationUs = sheet.duration;
        const ContactSheetResult r = renderTemplateContactSheet(preset, o);
        const QString fileName = name + QLatin1Char('-') + sheet.suffix + QStringLiteral(".png");
        const QString path = out.filePath(fileName);
        if (r.sheet.save(path)) {
            result.sheetPaths << path;
            s << "| `" << fileName << "` | " << cell(sheet.what) << " |\n";
        } else {
            s << "| `" << fileName << "` | **could not be written** |\n";
        }
    }

    s << "\n## 5. Sending it for review\n\n"
         "Send this report and the PNG files. After a correction, send the whole corrected JSON file, not a fragment, "
         "and run this check again.\n";

    result.report = report;
    QFile f(result.reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(report.toUtf8());
    return result;
}

} // namespace drift::skia
