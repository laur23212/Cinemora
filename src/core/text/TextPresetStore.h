#pragma once

#include "core/text/TextStyle.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <optional>

namespace drift {

// Ids of user-saved presets carry this prefix so they can never collide with a built-in pack id,
// and so resolving one is a prefix test rather than a scan of both catalogs.
bool isUserTextPresetId(const QString &id);

// Text style packs the user saved from the inspector. Persisted per-user (not per-project) as a
// single JSON file, because the whole library is a handful of small objects and every mutation
// rewrites it anyway.
class TextPresetStore
{
public:
    static TextPresetStore &instance();

    // By value, and newest first: the preview provider resolves ids on the image-loading thread
    // while the GUI thread saves and deletes, so nothing may hand out a pointer into the library.
    QList<TextPreset> presets() const;
    std::optional<TextPreset> presetForId(const QString &id) const;

    // Returns the minted id, or an empty string when the label is blank.
    QString add(const QString &label, const TextStyle &style, const QString &sampleText);
    // Dafin: a whole title template (style, clip effects, controls, metadata). Always mints a new
    // "user:" id, so it never overwrites an existing entry. Empty on a blank label or a failed write.
    QString addTemplate(TextPreset preset);
    // Replaces the look, effects and controls of a user template, keeping its name, description
    // and category. False for any id that is not a user template.
    bool updateTemplate(const QString &id, const TextPreset &content);
    // A copy under a new id ("basedOn" the original). Empty label keeps the original's.
    QString duplicate(const QString &id, const QString &label = {});
    bool setMetadata(const QString &id, const QString &description, const QString &category);
    // Every template in `fromCategory` moves to `toCategory` ("" = uncategorised). Returns how many.
    int moveCategory(const QString &fromCategory, const QString &toCategory);
    bool rename(const QString &id, const QString &label);
    bool remove(const QString &id);

    bool exportToFile(const QString &id, const QString &path) const;
    // Always mints a fresh id, so re-importing your own export adds a copy instead of clobbering.
    QString importFromFile(const QString &path);

    // Test seam: drops the in-memory copy so the next read re-reads the file.
    void reload();

    static QString storePath();

private:
    TextPresetStore() = default;

    void ensureLoaded() const;  // call with m_mutex held
    bool save() const;          // call with m_mutex held

    mutable QMutex m_mutex;
    mutable QList<TextPreset> m_presets;
    mutable bool m_loaded = false;
};

// Dafin: the user's own categories for their saved templates ("YouTube Titles", "My
// Subtitles"). Stable ids, so renaming touches nothing else; a template names its category by
// id (TextPreset::category). Persisted next to the template library.
struct TemplateCategory
{
    QString id;   // "cat-…"
    QString name;
};

class TemplateCategoryStore
{
public:
    static TemplateCategoryStore &instance();

    QList<TemplateCategory> categories() const; // in the user's order
    std::optional<TemplateCategory> categoryForId(const QString &id) const;
    // Returns the minted id; empty on a blank name. A name that already exists returns that
    // category's id instead of making a twin.
    QString add(const QString &name);
    bool rename(const QString &id, const QString &name);
    // Removes the category only. The caller moves its templates first (to another category or to
    // uncategorised); deleting a category never deletes templates.
    bool remove(const QString &id);
    bool move(const QString &id, int toIndex);

    void reload(); // test seam
    static QString storePath();

private:
    TemplateCategoryStore() = default;
    void ensureLoaded() const; // call with m_mutex held
    bool save() const;         // call with m_mutex held

    mutable QMutex m_mutex;
    mutable QList<TemplateCategory> m_categories;
    mutable bool m_loaded = false;
};

} // namespace drift
