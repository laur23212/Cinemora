// Dafin title templates in the editor: applying a template (style + its clip effects), the quick
// controls it declares, saving the selected title as a new template, updating a user template,
// the user's template categories, and checking / installing template files made outside Dafin
// (an external author such as ChatGPT). The file format and the rules live in
// core/text/TextTemplate.h; this is the editor's side of it.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/text/TextPresetStore.h"
#include "core/text/TextStyle.h"
#include "core/text/TextTemplate.h"
#include "engine/packages/assets/FontCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

namespace drift::appcontroller_detail {

TemplateEnvironment dafinTemplateEnvironment(bool portable)
{
    TemplateEnvironment env;
    env.effect = [](const QString &id) -> std::optional<EffectPresetMeta> {
        if (const EffectPresetEntry *def = effectDefForId(id))
            return def->meta;
        return std::nullopt;
    };
    env.fontAvailable = [](const QString &family) {
        if (fontFamilyForName(family))
            return true;
        const auto contains = [&](const QStringList &families) {
            return std::any_of(families.cbegin(), families.cend(), [&](const QString &f) {
                return f.compare(family, Qt::CaseInsensitive) == 0;
            });
        };
        // Text clips draw with the font catalog (the Fonts Essentials add-on), the user's own fonts
        // or the system's. Dafin's bundled InterUI files are for the interface only.
        return contains(userFontFamilies()) || contains(systemFontFamilies());
    };
    env.idTaken = [](const QString &id) { return textPresetForId(id).has_value(); };
#ifdef DRIFT_VERSION
    env.appVersion = QStringLiteral(DRIFT_VERSION).section(QLatin1Char('-'), 0, 0); // "1.2.0-nightly.7" → "1.2.0"
#endif
    env.portable = portable;
    return env;
}

// One template effect as a clip effect: the catalog's defaults and fixed params under the
// template's own values, keyed so the template (and its controls) can find it again.
static Effect makeTemplateEffect(const Effect &spec, const EffectPresetEntry &def)
{
    Effect e;
    e.name = def.filterName;
    e.catalogId = def.meta.id;
    for (auto it = def.fixedParams.constBegin(); it != def.fixedParams.constEnd(); ++it)
        e.parameters.insert(it.key(), it.value());
    for (const EffectParamSpec &p : def.meta.parameters)
        p.insertDefault(e.parameters);
    for (auto it = spec.parameters.constBegin(); it != spec.parameters.constEnd(); ++it)
        e.parameters.insert(it.key(), it.value());
    e.paramKeyframes = spec.paramKeyframes;
    e.enabled = spec.enabled;
    e.templateKey = spec.templateKey;
    return e;
}

int applyTitleTemplate(Clip &clip, const TextPreset &preset)
{
    clip.textStyle = preset.style;
    clip.textStyle.packId = preset.id;
    clip.textStyle.templateId = preset.id;
    int missing = 0;
    QList<Effect> added;
    for (const Effect &spec : preset.effects) {
        const EffectPresetEntry *def = effectDefForId(spec.catalogId);
        if (!def) {
            ++missing;
            continue;
        }
        added.append(makeTemplateEffect(spec, *def));
    }
    if (preset.sourceDurationUs > 0)
        rescaleEffectKeyframes(added, preset.sourceDurationUs, clip.timelineDuration);
    // The previous template's effects go; effects the user added themselves stay.
    replaceTemplateEffects(&clip.effects, added);
    return missing;
}

namespace {

// The adjustment carrying a clip's video effects: every edit moves clip effects onto it
// (hoistClipEffectsToAdjustmentLanes, run by pushProjectEdit), template effects included.
bool findEffectsHost(const Project &project, int trackIndex, const QString &clipId, int *lane, int *index)
{
    for (const int l : adjustmentLaneIndexes(project, trackIndex)) {
        const QList<Clip> &clips = project.tracks().at(l).clips;
        for (int i = 0; i < clips.size(); ++i) {
            if (clips.at(i).adjustmentKind == AdjustmentKind::VideoEffects && clips.at(i).linkedClipId == clipId) {
                *lane = l;
                *index = i;
                return true;
            }
        }
    }
    return false;
}

} // namespace

const QList<Effect> &titleEffects(const Project &project, int trackIndex, int clipIndex)
{
    const Clip &clip = project.tracks().at(trackIndex).clips.at(clipIndex);
    int lane = -1, index = -1;
    if (clip.effects.isEmpty() && findEffectsHost(project, trackIndex, clip.id, &lane, &index))
        return project.tracks().at(lane).clips.at(index).effects;
    return clip.effects;
}

QList<Effect> *titleEffects(Project &project, int trackIndex, int clipIndex)
{
    Clip &clip = project.tracks()[trackIndex].clips[clipIndex];
    int lane = -1, index = -1;
    if (clip.effects.isEmpty() && findEffectsHost(project, trackIndex, clip.id, &lane, &index))
        return &project.tracks()[lane].clips[index].effects;
    return &clip.effects;
}

int applyTitleTemplate(Project &project, int trackIndex, int clipIndex, const TextPreset &preset)
{
    const QString clipId = project.tracks().at(trackIndex).clips.at(clipIndex).id;
    int lane = -1, index = -1;
    if (findEffectsHost(project, trackIndex, clipId, &lane, &index)) {
        // The previous template's effects went to the adjustment with the user's own: take them
        // off there. The new ones join it when the edit is pushed.
        QList<Effect> &hosted = project.tracks()[lane].clips[index].effects;
        hosted.erase(std::remove_if(hosted.begin(), hosted.end(), [](const Effect &e) { return !e.templateKey.isEmpty(); }),
                     hosted.end());
    }
    const int missing = applyTitleTemplate(project.tracks()[trackIndex].clips[clipIndex], preset);
    if (lane >= 0) {
        // An adjustment that only ever carried the old template's effects has nothing left to do.
        const Clip &host = project.tracks().at(lane).clips.at(index);
        if (host.effects.isEmpty() && host.audioEffects.isEmpty()
            && project.tracks().at(trackIndex).clips.at(clipIndex).effects.isEmpty()) {
            project.tracks()[lane].clips.removeAt(index);
            if (project.tracks().at(lane).clips.isEmpty())
                project.tracks().removeAt(lane);
        }
    }
    return missing;
}

} // namespace drift::appcontroller_detail

using namespace drift::appcontroller_detail;

namespace {

const drift::Clip *styledTextClip(const drift::Project &project, int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return nullptr;
    const drift::Track &track = project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return nullptr;
    const drift::Clip &clip = track.clips.at(clipIndex);
    return clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle ? &clip : nullptr;
}

QVariantMap issueToMap(const drift::TemplateIssue &issue)
{
    return drift::templateIssueToJson(issue).toVariantMap();
}

QString originLabel(const QString &origin)
{
    if (origin == QLatin1String("user"))
        return QCoreApplication::translate("AppController", "My template");
    if (origin == QLatin1String("installed"))
        return QCoreApplication::translate("AppController", "Installed");
    return QCoreApplication::translate("AppController", "Built-in");
}

const drift::TemplateControl *controlForId(const drift::TextPreset &preset, const QString &id)
{
    for (const drift::TemplateControl &c : preset.controls)
        if (c.id == id)
            return &c;
    return nullptr;
}

QVariantMap templateToMap(const drift::TextPreset &preset)
{
    QVariantMap row{
        {QStringLiteral("id"), preset.id},
        {QStringLiteral("label"), preset.label},
        {QStringLiteral("sampleText"), preset.sampleText},
        {QStringLiteral("style"), textStyleToMap(preset.style, 0)},
        {QStringLiteral("description"), preset.description},
        {QStringLiteral("category"), preset.category},
        {QStringLiteral("tags"), preset.tags},
        {QStringLiteral("kind"), preset.kind.isEmpty() ? QStringLiteral("title") : preset.kind},
        {QStringLiteral("origin"), preset.origin},
        {QStringLiteral("originLabel"), originLabel(preset.origin)},
        {QStringLiteral("effectCount"), preset.effects.size()},
        {QStringLiteral("controlCount"), preset.controls.size()},
        {QStringLiteral("basedOn"), preset.basedOn},
    };
    // How fast the text is readable on a 3 s clip: the browser can sort and badge by it.
    const drift::ResolvedTextAnimation anim = drift::resolveTextAnimation(preset.style.animation);
    const drift::textanim::FittedTiming fit = drift::textanim::fittedTiming(
        anim.set, anim.resolved, drift::textanim::estimateDomains(preset.sampleText), 3'000'000, 30);
    row.insert(QStringLiteral("readableMs"), double(fit.readableAtUs) / 1000.0);
    return row;
}

} // namespace

QVariantList AppController::textTemplates() const
{
    QVariantList out;
    for (const drift::TextPreset &preset : drift::allTextTemplates())
        out.append(templateToMap(preset));
    return out;
}

QVariantMap AppController::textTemplateInfo(int trackIndex, int clipIndex) const
{
    const drift::Clip *clip = styledTextClip(m_project, trackIndex, clipIndex);
    if (!clip || clip->textStyle.templateId.isEmpty())
        return {};
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(clip->textStyle.templateId);
    if (!preset)
        return {{QStringLiteral("id"), clip->textStyle.templateId}, {QStringLiteral("missing"), true}};
    QVariantMap info = templateToMap(*preset);
    info.insert(QStringLiteral("isUser"), drift::isUserTextPresetId(preset->id));
    return info;
}

QVariantList AppController::templateControls(int trackIndex, int clipIndex) const
{
    const drift::Clip *clip = styledTextClip(m_project, trackIndex, clipIndex);
    if (!clip || clip->textStyle.templateId.isEmpty())
        return {};
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(clip->textStyle.templateId);
    if (!preset)
        return {};
    const drift::TemplateEnvironment env = dafinTemplateEnvironment(false);
    // A title's effects sit on its effects adjustment once the editor has moved them there.
    const QList<drift::Effect> &effects = titleEffects(m_project, trackIndex, clipIndex);
    QVariantList out;
    for (const drift::TemplateControl &c : preset->controls) {
        // A caption's words come from its cues, so a text control has nothing to drive there.
        if (c.bind == QLatin1String("text") && clip->type == drift::ClipType::Subtitle)
            continue;
        QStringList values;
        const QString type = drift::templateBindingType(c.bind, clip->textStyle, effects, env, nullptr, &values);
        if (type != c.type)
            continue; // the clip was edited so far that this control no longer has its target
        const QVariant value = drift::readTemplateBinding(c.bind, clip->textStyle, effects, clip->textContent);
        const QVariant fallback = drift::readTemplateBinding(c.bind, preset->style, preset->effects, preset->sampleText);
        QVariantList options;
        for (int i = 0; i < c.options.size(); ++i)
            options.append(QVariantMap{{QStringLiteral("value"), c.options.at(i)},
                                       {QStringLiteral("label"), c.optionLabels.value(i, c.options.at(i))}});
        out.append(QVariantMap{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("label"), c.label},
            {QStringLiteral("type"), c.type},
            {QStringLiteral("bind"), c.bind},
            {QStringLiteral("group"), c.group},
            {QStringLiteral("unit"), c.unit},
            {QStringLiteral("description"), c.description},
            {QStringLiteral("min"), c.min},
            {QStringLiteral("max"), c.max},
            {QStringLiteral("step"), c.step},
            {QStringLiteral("options"), options},
            {QStringLiteral("value"), value},
            {QStringLiteral("defaultValue"), fallback},
            {QStringLiteral("modified"), c.bind != QLatin1String("text") && value != fallback},
        });
    }
    return out;
}

bool AppController::writeTemplateControl(int trackIndex, int clipIndex, const QString &controlId, const QVariant &value)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->textStyle.templateId.isEmpty())
        return false;
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(clip->textStyle.templateId);
    if (!preset)
        return false;
    const drift::TemplateControl *control = controlForId(*preset, controlId);
    if (!control)
        return false;
    QVariant v = value;
    if (control->type == QLatin1String("scalar"))
        v = qBound(control->min, value.toDouble(), control->max);
    QString content = clip->textContent;
    if (!drift::writeTemplateBinding(control->bind, v, &clip->textStyle, titleEffects(m_project, trackIndex, clipIndex),
                                     &content))
        return false;
    if (control->bind == QLatin1String("text") && content != clip->textContent) {
        clip->textContent = content;
        clip->name = content.section(QLatin1Char('\n'), 0, 0).left(32);
    }
    // A hand edit: the picker no longer shows the pack as applied unchanged, but the clip still
    // knows its template (templateId), so the controls and "Update template" stay available.
    clip->textStyle.packId.clear();
    return true;
}

void AppController::setTemplateControl(int trackIndex, int clipIndex, const QString &controlId, const QVariant &value)
{
    const drift::Project before = m_project;
    if (!writeTemplateControl(trackIndex, clipIndex, controlId, value)) {
        m_project = before;
        return;
    }
    pushProjectEdit(before, tr("Edit template"));
    finishEdit(tr("Template updated"));
}

void AppController::previewSetTemplateControl(int trackIndex, int clipIndex, const QString &controlId,
                                              const QVariant &value)
{
    beginImplicitPreviewDrag(tr("Edit template"));
    if (writeTemplateControl(trackIndex, clipIndex, controlId, value))
        emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("style.template"), QStringLiteral("effects")});
}

void AppController::resetTemplateControl(int trackIndex, int clipIndex, const QString &controlId)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->textStyle.templateId.isEmpty())
        return;
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(clip->textStyle.templateId);
    const drift::TemplateControl *control = preset ? controlForId(*preset, controlId) : nullptr;
    if (!control || control->bind == QLatin1String("text"))
        return;
    setTemplateControl(trackIndex, clipIndex, controlId,
                       drift::readTemplateBinding(control->bind, preset->style, preset->effects));
}

void AppController::resetTextTemplate(int trackIndex, int clipIndex)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || clip->textStyle.templateId.isEmpty())
        return;
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(clip->textStyle.templateId);
    if (!preset) {
        setLastMessage(tr("This title's template is no longer installed"), QStringLiteral("warning"));
        return;
    }
    const drift::Project before = m_project;
    // Everything the template defines comes back; the clip's own text, timing and placement stay.
    const int missing = applyTitleTemplate(m_project, trackIndex, clipIndex, *preset);
    pushProjectEdit(before, tr("Reset template"));
    finishEdit(missing > 0 ? tr("Template reset; %n of its effects are not installed", "", missing)
                           : tr("Template reset"));
}

QString AppController::saveTextTemplate(int trackIndex, int clipIndex, const QVariantMap &details)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip)
        return {};
    const QString label = details.value(QStringLiteral("label")).toString().trimmed();
    if (label.isEmpty())
        return {};

    drift::TextPreset preset;
    // What the user sees is what they keep: the whole style (keyframes included) and every
    // effect on the clip. Effects the template did not add get keys of their own here.
    const std::optional<drift::TextPreset> base =
        clip->textStyle.templateId.isEmpty() ? std::nullopt : drift::textPresetForId(clip->textStyle.templateId);
    preset.label = label;
    preset.description = details.value(QStringLiteral("description")).toString().trimmed();
    preset.category = details.value(QStringLiteral("category")).toString();
    preset.style = clip->textStyle;
    preset.kind = clip->type == drift::ClipType::Subtitle ? QStringLiteral("caption")
                  : base && !base->kind.isEmpty() ? base->kind
                                                   : QStringLiteral("title");
    preset.basedOn = base ? base->id : QString();
    preset.controls = base ? base->controls : QList<drift::TemplateControl>{};
    preset.tags = base ? base->tags : QStringList{};
    preset.sourceDurationUs = clip->timelineDuration;
    const QList<drift::Effect> effects = titleEffects(std::as_const(m_project), trackIndex, clipIndex);
    QSet<QString> keys;
    for (const drift::Effect &e : effects)
        if (!e.templateKey.isEmpty())
            keys.insert(e.templateKey);
    int minted = 0;
    for (drift::Effect e : effects) {
        if (e.templateKey.isEmpty()) {
            do
                e.templateKey = QStringLiteral("fx%1").arg(++minted);
            while (keys.contains(e.templateKey));
            keys.insert(e.templateKey);
        }
        preset.effects.append(e);
    }
    if (!preset.style.fontFamily.isEmpty())
        preset.requiredFonts = {preset.style.fontFamily};
    QString sample = clip->type == drift::ClipType::Subtitle && !clip->subtitleCues.isEmpty()
                         ? clip->subtitleCues.first().text
                         : clip->textContent;
    sample = sample.simplified();
    const QStringList words = sample.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    preset.sampleText = words.size() > 4 ? words.mid(0, 4).join(QLatin1Char(' ')) : sample;

    const QString id = drift::TextPresetStore::instance().addTemplate(preset);
    if (id.isEmpty()) {
        setLastMessage(tr("Could not save the template"), QStringLiteral("error"));
        return {};
    }
    emit userTextPresetsChanged();

    // The clip now belongs to the template it just became, so "Update" can follow; its look is
    // unchanged. The template's own effects carry the keys the clip's copies need.
    const drift::Project before = m_project;
    clip->textStyle.templateId = id;
    clip->textStyle.packId = id;
    int i = 0;
    for (drift::Effect &e : *titleEffects(m_project, trackIndex, clipIndex))
        e.templateKey = preset.effects.value(i++).templateKey;
    pushProjectEdit(before, tr("Save as new template"));
    finishEdit(tr("Saved as “%1”").arg(label));
    return id;
}

bool AppController::updateTextTemplate(int trackIndex, int clipIndex)
{
    drift::Clip *clip = textClipAt(trackIndex, clipIndex);
    if (!clip || !drift::isUserTextPresetId(clip->textStyle.templateId))
        return false;
    const std::optional<drift::TextPreset> existing = drift::textPresetForId(clip->textStyle.templateId);
    if (!existing)
        return false;
    drift::TextPreset content = *existing;
    content.style = clip->textStyle;
    content.effects.clear();
    for (const drift::Effect &e : titleEffects(std::as_const(m_project), trackIndex, clipIndex))
        if (!e.templateKey.isEmpty())
            content.effects.append(e);
    content.sourceDurationUs = clip->timelineDuration;
    if (!drift::TextPresetStore::instance().updateTemplate(existing->id, content)) {
        setLastMessage(tr("Could not update the template"), QStringLiteral("error"));
        return false;
    }
    emit userTextPresetsChanged();
    // Other titles made from it keep their own copy: a saved template never reaches into clips.
    setLastMessage(tr("“%1” updated").arg(existing->label), QStringLiteral("success"));
    return true;
}

QString AppController::duplicateUserTextPreset(const QString &presetId)
{
    const std::optional<drift::TextPreset> source = drift::TextPresetStore::instance().presetForId(presetId);
    if (!source)
        return {};
    const QString id = drift::TextPresetStore::instance().duplicate(presetId, tr("%1 copy").arg(source->label));
    if (id.isEmpty()) {
        setLastMessage(tr("Could not duplicate the template"), QStringLiteral("error"));
        return {};
    }
    emit userTextPresetsChanged();
    return id;
}

bool AppController::setUserTextPresetDetails(const QString &presetId, const QVariantMap &details)
{
    drift::TextPresetStore &store = drift::TextPresetStore::instance();
    const std::optional<drift::TextPreset> preset = store.presetForId(presetId);
    if (!preset)
        return false;
    bool ok = true;
    if (details.contains(QStringLiteral("label")) && details.value(QStringLiteral("label")).toString().trimmed() != preset->label)
        ok = store.rename(presetId, details.value(QStringLiteral("label")).toString());
    const QString description = details.value(QStringLiteral("description"), preset->description).toString();
    const QString category = details.value(QStringLiteral("category"), preset->category).toString();
    ok = store.setMetadata(presetId, description, category) && ok;
    emit userTextPresetsChanged();
    if (!ok)
        setLastMessage(tr("Could not change the template"), QStringLiteral("error"));
    return ok;
}

QVariantList AppController::templateCategories() const
{
    QHash<QString, int> counts;
    for (const drift::TextPreset &p : drift::TextPresetStore::instance().presets())
        ++counts[p.category];
    QVariantList out;
    for (const drift::TemplateCategory &c : drift::TemplateCategoryStore::instance().categories())
        out.append(QVariantMap{{QStringLiteral("id"), c.id},
                               {QStringLiteral("name"), c.name},
                               {QStringLiteral("count"), counts.value(c.id)}});
    return out;
}

QString AppController::addTemplateCategory(const QString &name)
{
    const QString id = drift::TemplateCategoryStore::instance().add(name);
    if (!id.isEmpty())
        emit templateCategoriesChanged();
    return id;
}

bool AppController::renameTemplateCategory(const QString &categoryId, const QString &name)
{
    const bool ok = drift::TemplateCategoryStore::instance().rename(categoryId, name);
    if (ok)
        emit templateCategoriesChanged();
    return ok;
}

bool AppController::removeTemplateCategory(const QString &categoryId, const QString &moveTo)
{
    // The templates move first, so a failure part-way never leaves them in a category that is gone.
    drift::TextPresetStore::instance().moveCategory(categoryId, moveTo);
    const bool ok = drift::TemplateCategoryStore::instance().remove(categoryId);
    emit templateCategoriesChanged();
    emit userTextPresetsChanged();
    return ok;
}

QVariantMap AppController::checkTextTemplateFile(const QUrl &fileUrl) const
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QVariantMap out{{QStringLiteral("path"), path}};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("report"), tr("Could not open %1").arg(QFileInfo(path).fileName()));
        return out;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    QList<drift::TemplateIssue> issues;
    drift::TextPreset parsed;
    if (!doc.isObject()) {
        drift::TemplateIssue issue;
        issue.code = QStringLiteral("schema");
        issue.message = QStringLiteral("not valid JSON: %1 at offset %2").arg(parseError.errorString()).arg(parseError.offset);
        issues.append(issue);
    } else {
        issues = drift::validateTitleTemplate(doc.object(), dafinTemplateEnvironment(false), &parsed);
    }
    QVariantList list;
    QStringList lines;
    for (const drift::TemplateIssue &issue : issues) {
        list.append(issueToMap(issue));
        lines.append(drift::templateIssueToText(issue));
    }
    out.insert(QStringLiteral("ok"), !drift::hasTemplateErrors(issues));
    out.insert(QStringLiteral("issues"), list);
    out.insert(QStringLiteral("id"), parsed.id);
    out.insert(QStringLiteral("label"), parsed.label);
    // The block a user pastes back to the template's author, word for word.
    out.insert(QStringLiteral("report"),
               QStringLiteral("Dafin template check for %1 (format %2)\n%3")
                   .arg(QFileInfo(path).fileName())
                   .arg(drift::kTitleTemplateFormat)
                   .arg(lines.isEmpty() ? QStringLiteral("no problems found") : lines.join(QLatin1Char('\n'))));
    return out;
}

QVariantMap AppController::installTextTemplate(const QUrl &fileUrl)
{
    QVariantMap result = checkTextTemplateFile(fileUrl);
    if (!result.value(QStringLiteral("ok")).toBool())
        return result;
    const QString dir = drift::TitleTemplateCatalog::installedDir();
    const QString id = result.value(QStringLiteral("id")).toString();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        result.insert(QStringLiteral("ok"), false);
        return result;
    }
    // One file per id: installing a newer version of a template replaces the older one.
    const QString target = QDir(dir).filePath(QString(id).replace(QLatin1Char(':'), QLatin1Char('_')) + QStringLiteral(".json"));
    QFile source(result.value(QStringLiteral("path")).toString());
    QSaveFile out(target);
    if (!source.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly) || out.write(source.readAll()) < 0
        || !out.commit()) {
        result.insert(QStringLiteral("ok"), false);
        setLastMessage(tr("Could not install the template"), QStringLiteral("error"));
        return result;
    }
    drift::TitleTemplateCatalog::instance().reload();
    emit textTemplatesChanged();
    setLastMessage(tr("Installed “%1”").arg(result.value(QStringLiteral("label")).toString()), QStringLiteral("success"));
    return result;
}

bool AppController::uninstallTextTemplate(const QString &templateId)
{
    const std::optional<drift::TextPreset> preset = drift::TitleTemplateCatalog::instance().templateForId(templateId);
    if (!preset || preset->origin != QLatin1String("installed") || !QFile::remove(preset->sourcePath))
        return false;
    drift::TitleTemplateCatalog::instance().reload();
    emit textTemplatesChanged();
    return true;
}

void AppController::copyPlainTextToClipboard(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}
