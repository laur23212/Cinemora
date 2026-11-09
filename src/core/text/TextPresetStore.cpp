#include "core/text/TextPresetStore.h"

#include "core/text/TextTemplate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace drift {

namespace {

const QString kIdPrefix = QStringLiteral("user:");
constexpr int kFormatVersion = 2;

// Dafin: an entry is a whole title template (TextTemplate.h) — style, clip effects, quick
// controls and library metadata. Older files hold only id / label / sampleText / style, which
// parse the same way.
QJsonObject presetToJson(const TextPreset &preset)
{
    return titleTemplateToJson(preset);
}

// `id` is taken from the object only when the caller trusts it (the library file); imports pass
// false so a shared file can never overwrite an existing preset by claiming its id.
TextPreset presetFromJson(const QJsonObject &o, bool keepId)
{
    TextPreset preset = titleTemplateFromJson(o, keepId);
    // A saved pack is the style itself; a stale packId pointing at whatever clip it came from
    // would make the picker claim the wrong parent pack. (titleTemplateFromJson clears it.)
    preset.origin = QStringLiteral("user");
    return preset;
}

QString mintId()
{
    return kIdPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

bool isUserTextPresetId(const QString &id)
{
    return id.startsWith(kIdPrefix);
}

TextPresetStore &TextPresetStore::instance()
{
    static TextPresetStore store;
    return store;
}

QString TextPresetStore::storePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    return QDir(base).filePath(QStringLiteral("text-presets.json"));
}

void TextPresetStore::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_presets.clear();

    const QString path = storePath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return; // no library yet

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray presets = root.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : presets) {
        const TextPreset preset = presetFromJson(value.toObject(), true);
        if (preset.id.isEmpty() || preset.label.isEmpty())
            continue;
        m_presets.append(preset);
    }
}

bool TextPresetStore::save() const
{
    const QString path = storePath();
    if (path.isEmpty())
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray presets;
    for (const TextPreset &preset : m_presets)
        presets.append(presetToJson(preset));

    const QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("presets"), presets},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<TextPreset> TextPresetStore::presets() const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_presets;
}

std::optional<TextPreset> TextPresetStore::presetForId(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const TextPreset &preset : m_presets) {
        if (preset.id == id)
            return preset;
    }
    return std::nullopt;
}

QString TextPresetStore::add(const QString &label, const TextStyle &style,
                             const QString &sampleText)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty())
        return {};

    QMutexLocker locker(&m_mutex);
    ensureLoaded();

    TextPreset preset;
    preset.id = mintId();
    preset.label = trimmed;
    preset.style = style;
    preset.style.packId.clear();
    preset.style.templateId.clear();
    // A pack is a look, not a motion: keys belong to the clip they were authored on.
    preset.style.keyframes.clear();
    preset.sampleText = sampleText.trimmed().isEmpty() ? trimmed : sampleText.trimmed();

    m_presets.prepend(preset);
    if (!save()) {
        m_presets.removeFirst();
        return {};
    }
    return preset.id;
}

QString TextPresetStore::addTemplate(TextPreset preset)
{
    preset.label = preset.label.trimmed();
    if (preset.label.isEmpty())
        return {};

    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    // Always a fresh id: saving as a new template can never overwrite anything, built-in or user.
    preset.id = mintId();
    preset.origin = QStringLiteral("user");
    preset.sourcePath.clear();
    preset.style.packId.clear();
    preset.style.templateId.clear();
    if (preset.sampleText.trimmed().isEmpty())
        preset.sampleText = preset.label;
    m_presets.prepend(preset);
    if (!save()) {
        m_presets.removeFirst();
        return {};
    }
    return preset.id;
}

bool TextPresetStore::updateTemplate(const QString &id, const TextPreset &content)
{
    if (!isUserTextPresetId(id))
        return false; // built-in and installed templates are never written through here
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (TextPreset &preset : m_presets) {
        if (preset.id != id)
            continue;
        const TextPreset previous = preset;
        // Library metadata stays the user's; the look, effects and controls are replaced.
        preset.style = content.style;
        preset.style.packId.clear();
        preset.style.templateId.clear();
        preset.effects = content.effects;
        preset.controls = content.controls;
        preset.sourceDurationUs = content.sourceDurationUs;
        preset.requiredFonts = content.requiredFonts;
        if (!content.sampleText.trimmed().isEmpty())
            preset.sampleText = content.sampleText;
        if (!save()) {
            preset = previous;
            return false;
        }
        return true;
    }
    return false;
}

QString TextPresetStore::duplicate(const QString &id, const QString &label)
{
    std::optional<TextPreset> source = presetForId(id);
    if (!source)
        return {};
    TextPreset copy = *source;
    copy.basedOn = id;
    copy.label = label.trimmed().isEmpty() ? source->label : label.trimmed();
    return addTemplate(copy);
}

bool TextPresetStore::setMetadata(const QString &id, const QString &description, const QString &category)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (TextPreset &preset : m_presets) {
        if (preset.id != id)
            continue;
        const TextPreset previous = preset;
        preset.description = description.trimmed();
        preset.category = category;
        if (!save()) {
            preset = previous;
            return false;
        }
        return true;
    }
    return false;
}

int TextPresetStore::moveCategory(const QString &fromCategory, const QString &toCategory)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    const QList<TextPreset> previous = m_presets;
    int moved = 0;
    for (TextPreset &preset : m_presets) {
        if (preset.category == fromCategory) {
            preset.category = toCategory;
            ++moved;
        }
    }
    if (moved > 0 && !save()) {
        m_presets = previous;
        return 0;
    }
    return moved;
}

bool TextPresetStore::rename(const QString &id, const QString &label)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (TextPreset &preset : m_presets) {
        if (preset.id != id)
            continue;
        const QString previous = preset.label;
        preset.label = trimmed;
        if (!save()) {
            preset.label = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool TextPresetStore::remove(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id != id)
            continue;
        const TextPreset removed = m_presets.takeAt(i);
        if (!save()) {
            m_presets.insert(i, removed);
            return false;
        }
        return true;
    }
    return false;
}

bool TextPresetStore::exportToFile(const QString &id, const QString &path) const
{
    const std::optional<TextPreset> preset = presetForId(id);
    if (!preset)
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(presetToJson(*preset)).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString TextPresetStore::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (!o.contains(QStringLiteral("style")))
        return {};

    TextPreset incoming = presetFromJson(o, false);
    if (incoming.label.isEmpty())
        incoming.label = QFileInfo(path).completeBaseName();
    // The library's categories are this machine's; an imported entry starts uncategorised.
    incoming.category.clear();
    return addTemplate(incoming);
}

void TextPresetStore::reload()
{
    QMutexLocker locker(&m_mutex);
    m_loaded = false;
    m_presets.clear();
}

// ---------------------------------------------------------------------------------------------
// Template categories

TemplateCategoryStore &TemplateCategoryStore::instance()
{
    static TemplateCategoryStore store;
    return store;
}

QString TemplateCategoryStore::storePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("template-categories.json"));
}

void TemplateCategoryStore::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_categories.clear();
    QFile file(storePath());
    if (storePath().isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject o = v.toObject();
        TemplateCategory c{o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("name")).toString()};
        if (!c.id.isEmpty() && !c.name.trimmed().isEmpty())
            m_categories.append(c);
    }
}

bool TemplateCategoryStore::save() const
{
    const QString path = storePath();
    if (path.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray categories;
    for (const TemplateCategory &c : m_categories)
        categories.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name}});
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("categories"), categories}})
                   .toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<TemplateCategory> TemplateCategoryStore::categories() const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_categories;
}

std::optional<TemplateCategory> TemplateCategoryStore::categoryForId(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const TemplateCategory &c : std::as_const(m_categories))
        if (c.id == id)
            return c;
    return std::nullopt;
}

QString TemplateCategoryStore::add(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const TemplateCategory &c : std::as_const(m_categories))
        if (c.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return c.id;
    TemplateCategory c{QStringLiteral("cat-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8), trimmed};
    m_categories.append(c);
    if (!save()) {
        m_categories.removeLast();
        return {};
    }
    return c.id;
}

bool TemplateCategoryStore::rename(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (TemplateCategory &c : m_categories) {
        if (c.id != id)
            continue;
        const QString previous = c.name;
        c.name = trimmed;
        if (!save()) {
            c.name = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool TemplateCategoryStore::remove(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).id != id)
            continue;
        const TemplateCategory removed = m_categories.takeAt(i);
        if (!save()) {
            m_categories.insert(i, removed);
            return false;
        }
        return true;
    }
    return false;
}

bool TemplateCategoryStore::move(const QString &id, int toIndex)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).id != id)
            continue;
        toIndex = qBound(0, toIndex, int(m_categories.size()) - 1);
        if (toIndex == i)
            return true;
        m_categories.move(i, toIndex);
        if (!save()) {
            m_categories.move(toIndex, i);
            return false;
        }
        return true;
    }
    return false;
}

void TemplateCategoryStore::reload()
{
    QMutexLocker locker(&m_mutex);
    m_loaded = false;
    m_categories.clear();
}

} // namespace drift
