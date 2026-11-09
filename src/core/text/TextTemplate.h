#pragma once

#include "core/effects/EffectPreset.h"
#include "core/text/TextStyle.h"

#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>

// Dafin title templates: a text style pack plus the clip effects it needs and the few quick
// controls users usually change, as one JSON file. The same file format serves bundled templates
// (presets/text-styles), installed ones (AppData/text-styles: made by an external author such as
// ChatGPT, or unpacked from a package) and the user's own saves (TextPresetStore).
//
// {
//   "format": 1, "id": "dafin.fast-pop", "label": "Fast Pop", "description": "…",
//   "category": "fast-titles", "tags": ["youtube"], "kind": "title", "sampleText": "LET'S GO",
//   "requires": { "fonts": ["Inter"], "minAppVersion": "1.1.0" },
//   "style":    { …textStyleToJson… },
//   "effects":  [ { "key": "sparks", "effect": "bling_sparkle", "parameters": { "intensity": 0.6 } } ],
//   "controls": [ { "id": "color", "label": "Colour", "type": "color", "bind": "layer.fill.color" } ]
// }

namespace drift {

inline constexpr int kTitleTemplateFormat = 1;

QJsonObject titleTemplateToJson(const TextPreset &preset);
// Parses without validating (validateTitleTemplate reports what is wrong). `keepId` false is for
// imports, which mint their own id. Unknown fields are ignored.
TextPreset titleTemplateFromJson(const QJsonObject &object, bool keepId = true);

// ---------------------------------------------------------------------------------------------
// Bindings: the properties a TemplateControl may drive. Paths, by value type:
//   text                                  the clip's text (text)
//   font.family                           (font)
//   font.weight 100..900, font.size px    (scalar)        font.italic (bool)
//   align left|center|right               (enum)
//   pixelSize letterSpacing lineHeight boxPadding pathBend
//   underlineWidth underlineOffset underlineReveal effectPadding         (scalar)
//   layer.<id>.enabled                    (bool)
//   layer.<id>.color                      (color; solid paint only)
//   layer.<id>.<field>                    (scalar; opacity blur width spread offsetX offsetY,
//                                          trimStart trimEnd, effect.<param>… — the layer's
//                                          keyframe fields)
//   box.enabled (bool)  box.color (color)  box.radius (scalar)
//   underline.enabled (bool)  underline.color (color)
//   animation.<in|out|loop>.enabled (bool)  animation.<slot>.preset (enum of preset ids)
//   animation.<slot>.duration (scalar, seconds; the period for loop)
//   animation.<slot>.param.<id> (the preset param's own type)
//   timing.fit (bool)  timing.maxReadMs (scalar)
//   effect.<key>.enabled (bool)  effect.<key>.<param> (the effect parameter's type)
// Layers are addressed by their id, effects by their template key, so reordering either never
// re-targets a control. Several controls may bind the same path; the last edit wins.

struct TemplateBindingForm
{
    QString pattern;
    QString type;
    QString description;
};
// Every documented form, for the authoring guide and the validator's hints.
const QList<TemplateBindingForm> &templateBindingCatalog();

// What the caller knows about the engine side: the effect catalog and the installed fonts. Core
// cannot see either; the editor and the validator fill these in. Unset functions skip the check.
struct TemplateEnvironment
{
    std::function<std::optional<EffectPresetMeta>(const QString &effectId)> effect;
    std::function<bool(const QString &family)> fontAvailable;
    std::function<bool(const QString &id)> idTaken; // another template already uses this id
    QString appVersion;                               // to check requires.minAppVersion
    bool portable = false; // checking a file meant for other machines: stricter dependencies
};

// The value type `bind` has on this style and effect list ("" when it does not resolve); `why`
// says what is wrong in a sentence that names the accepted alternatives.
QString templateBindingType(const QString &bind, const TextStyle &style, const QList<Effect> &effects,
                            const TemplateEnvironment &env, QString *why = nullptr,
                            QStringList *enumValues = nullptr);
// Reading and writing through a binding. Colours travel as "#rrggbb" / "#aarrggbb", fonts and
// text as strings, scalars as doubles, bools as bools, enums as their value string. `content` is
// the clip's text (the `text` binding); pass null where there is none.
QVariant readTemplateBinding(const QString &bind, const TextStyle &style, const QList<Effect> &effects,
                             const QString &content = {});
bool writeTemplateBinding(const QString &bind, const QVariant &value, TextStyle *style, QList<Effect> *effects,
                          QString *content = nullptr);

// ---------------------------------------------------------------------------------------------
// Validation, written for authors who cannot read the source (an AI, a designer): every issue
// names the JSON path, what is wrong, and what is accepted.

struct TemplateIssue
{
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    // schema · unsupported · missing_dependency · binding · range · shader_compile · render ·
    // compatibility
    QString code;
    QString path;     // JSON path into the file: "controls[3].bind"
    QString message;
    QString expected; // accepted values, when there is a short list
};

QList<TemplateIssue> validateTitleTemplate(const QJsonObject &object, const TemplateEnvironment &env,
                                           TextPreset *parsed = nullptr);
bool hasTemplateErrors(const QList<TemplateIssue> &issues);
QJsonObject templateIssueToJson(const TemplateIssue &issue);
QString templateIssueToText(const TemplateIssue &issue);

// What every quick control of a template must do, checked without the editor (`dafin
// --check-template` and the tests): its binding resolves to the control's type and its value
// reads back; a different value it can reach changes the template (text, style or effects), reads
// back, leaves the other controls alone and survives a save and reload; and writing the
// template's own value back — the reset button — restores it.
struct TemplateControlCheck
{
    QString controlId;
    QString bind;
    QString type;
    QVariant templateValue; // what the template sets
    QVariant testValue;     // the other value written
    bool ok = false;
    QString problem;        // empty when ok
};
QList<TemplateControlCheck> checkTemplateControls(const TextPreset &preset, const TemplateEnvironment &env);

// ---------------------------------------------------------------------------------------------
// The JSON templates on disk: bundled (:/text-styles), $DAFIN_TEXT_STYLES (folders separated as
// in PATH: ';' on Windows, ':' elsewhere) and the installed folder, later roots overriding earlier
// ids. Files that do not validate are skipped and listed in loadErrors(). Effects and fonts are not
// checked here (core cannot see them); applying a template skips an effect the catalog does not have.
class TitleTemplateCatalog
{
public:
    static TitleTemplateCatalog &instance();

    QList<TextPreset> templates() const;
    std::optional<TextPreset> templateForId(const QString &id) const;
    // "<file>: <issue>" for every file that was skipped.
    QStringList loadErrors() const;

    void reload();
    // AppData/text-styles: where installed templates live.
    static QString installedDir();

private:
    TitleTemplateCatalog() = default;
    void ensureLoaded() const; // call with m_mutex held
    void loadDir(const QString &dir, const QString &origin) const;

    mutable QMutex m_mutex;
    mutable bool m_loaded = false;
    mutable QList<TextPreset> m_templates;
    mutable QStringList m_errors;
};

// Built-in packs, then bundled JSON templates, then installed ones: what the template browser
// lists. User saves are separate (TextPresetStore).
QList<TextPreset> allTextTemplates();

// A clip's effects once a template is applied: every effect an earlier template added (one with
// a templateKey) is removed, the user's own effects stay in place, and `templateEffects` (built
// from the catalog by the caller) are appended. Re-applying a template therefore never
// duplicates its effects.
void replaceTemplateEffects(QList<Effect> *clipEffects, const QList<Effect> &templateEffects);

} // namespace drift
