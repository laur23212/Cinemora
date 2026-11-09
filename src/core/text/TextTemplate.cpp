#include "core/text/TextTemplate.h"

#include "core/text/TextLook.h"
#include "core/text/animation/TextAnimationPreset.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <limits>

// Static-library resources are only pulled into the link when something references them.
static void initTextStyleResources()
{
    Q_INIT_RESOURCE(drift_text_styles);
}

namespace drift {

// ---------------------------------------------------------------------------------------------
// Controls

QJsonObject templateControlToJson(const TemplateControl &c)
{
    QJsonObject o{{QStringLiteral("id"), c.id},
                  {QStringLiteral("label"), c.label},
                  {QStringLiteral("type"), c.type},
                  {QStringLiteral("bind"), c.bind}};
    if (!c.group.isEmpty())
        o.insert(QStringLiteral("group"), c.group);
    if (!c.unit.isEmpty())
        o.insert(QStringLiteral("unit"), c.unit);
    if (!c.description.isEmpty())
        o.insert(QStringLiteral("description"), c.description);
    if (c.type == QLatin1String("scalar")) {
        o.insert(QStringLiteral("min"), c.min);
        o.insert(QStringLiteral("max"), c.max);
        if (c.step > 0.0)
            o.insert(QStringLiteral("step"), c.step);
    }
    if (!c.options.isEmpty())
        o.insert(QStringLiteral("options"), QJsonArray::fromStringList(c.options));
    if (!c.optionLabels.isEmpty())
        o.insert(QStringLiteral("optionLabels"), QJsonArray::fromStringList(c.optionLabels));
    return o;
}

namespace {

QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray())
        if (item.isString())
            out.append(item.toString());
    return out;
}

} // namespace

TemplateControl templateControlFromJson(const QJsonObject &o)
{
    TemplateControl c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.label = o.value(QStringLiteral("label")).toString();
    c.type = o.value(QStringLiteral("type")).toString();
    c.bind = o.value(QStringLiteral("bind")).toString();
    c.group = o.value(QStringLiteral("group")).toString();
    c.unit = o.value(QStringLiteral("unit")).toString();
    c.description = o.value(QStringLiteral("description")).toString();
    c.min = o.value(QStringLiteral("min")).toDouble(0.0);
    c.max = o.value(QStringLiteral("max")).toDouble(1.0);
    c.step = o.value(QStringLiteral("step")).toDouble(0.0);
    c.options = stringList(o.value(QStringLiteral("options")));
    c.optionLabels = stringList(o.value(QStringLiteral("optionLabels")));
    return c;
}

// ---------------------------------------------------------------------------------------------
// File format

QJsonObject titleTemplateToJson(const TextPreset &p)
{
    QJsonObject o{{QStringLiteral("format"), kTitleTemplateFormat},
                  {QStringLiteral("id"), p.id},
                  {QStringLiteral("label"), p.label}};
    if (!p.description.isEmpty())
        o.insert(QStringLiteral("description"), p.description);
    if (!p.category.isEmpty())
        o.insert(QStringLiteral("category"), p.category);
    if (!p.tags.isEmpty())
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(p.tags));
    if (!p.kind.isEmpty() && p.kind != QLatin1String("title"))
        o.insert(QStringLiteral("kind"), p.kind);
    if (!p.sampleText.isEmpty())
        o.insert(QStringLiteral("sampleText"), p.sampleText);
    if (!p.requiredFonts.isEmpty() || !p.minAppVersion.isEmpty()) {
        QJsonObject req;
        if (!p.requiredFonts.isEmpty())
            req.insert(QStringLiteral("fonts"), QJsonArray::fromStringList(p.requiredFonts));
        if (!p.minAppVersion.isEmpty())
            req.insert(QStringLiteral("minAppVersion"), p.minAppVersion);
        o.insert(QStringLiteral("requires"), req);
    }
    // The style is the template itself: where it was applied from is clip state, not template.
    TextStyle style = p.style;
    style.packId.clear();
    style.templateId.clear();
    o.insert(QStringLiteral("style"), textStyleToJson(style));
    if (!p.effects.isEmpty()) {
        QJsonArray effects;
        const QJsonArray raw = effectsToJson(p.effects);
        for (int i = 0; i < raw.size(); ++i) {
            QJsonObject e = raw.at(i).toObject();
            e.remove(QStringLiteral("name")); // the engine's filter name, resolved from the catalog
            e.remove(QStringLiteral("catalogId"));
            e.remove(QStringLiteral("templateKey"));
            QJsonObject out{{QStringLiteral("key"), p.effects.at(i).templateKey},
                            {QStringLiteral("effect"), p.effects.at(i).catalogId}};
            for (auto it = e.constBegin(); it != e.constEnd(); ++it)
                out.insert(it.key(), it.value());
            effects.append(out);
        }
        o.insert(QStringLiteral("effects"), effects);
        if (p.sourceDurationUs > 0)
            o.insert(QStringLiteral("sourceDurationMs"), static_cast<double>(p.sourceDurationUs) / 1000.0);
    }
    if (!p.controls.isEmpty()) {
        QJsonArray controls;
        for (const TemplateControl &c : p.controls)
            controls.append(templateControlToJson(c));
        o.insert(QStringLiteral("controls"), controls);
    }
    if (!p.basedOn.isEmpty())
        o.insert(QStringLiteral("basedOn"), p.basedOn);
    return o;
}

TextPreset titleTemplateFromJson(const QJsonObject &o, bool keepId)
{
    TextPreset p;
    if (keepId)
        p.id = o.value(QStringLiteral("id")).toString();
    p.label = o.value(QStringLiteral("label")).toString();
    p.description = o.value(QStringLiteral("description")).toString();
    p.category = o.value(QStringLiteral("category")).toString();
    p.tags = stringList(o.value(QStringLiteral("tags")));
    p.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("title"));
    p.sampleText = o.value(QStringLiteral("sampleText")).toString();
    const QJsonObject req = o.value(QStringLiteral("requires")).toObject();
    p.requiredFonts = stringList(req.value(QStringLiteral("fonts")));
    p.minAppVersion = req.value(QStringLiteral("minAppVersion")).toString();
    // A template's style is always the current (layered) form. textStyleFromJson reads a style
    // without "layers" as the old flat v6 form and would drop its "animation", so a style that
    // leaves the stack out gets the default one (a white fill) instead.
    QJsonObject styleJson = o.value(QStringLiteral("style")).toObject();
    if (!styleJson.contains(QStringLiteral("layers")))
        styleJson.insert(QStringLiteral("layers"), textStyleToJson(TextStyle{}).value(QStringLiteral("layers")));
    p.style = textStyleFromJson(styleJson);
    p.style.packId.clear();
    p.style.templateId.clear();

    QSet<QString> keys;
    int minted = 0;
    for (const QJsonValue &v : o.value(QStringLiteral("effects")).toArray()) {
        QJsonObject e = v.toObject();
        const QString id = e.contains(QStringLiteral("effect")) ? e.value(QStringLiteral("effect")).toString()
                                                                 : e.value(QStringLiteral("catalogId")).toString();
        e.insert(QStringLiteral("catalogId"), id);
        const QList<Effect> one = effectsFromJson(QJsonArray{e});
        if (one.isEmpty())
            continue;
        Effect effect = one.first();
        effect.templateKey = e.value(QStringLiteral("key")).toString(effect.templateKey);
        while (effect.templateKey.isEmpty() || keys.contains(effect.templateKey))
            effect.templateKey = QStringLiteral("fx%1").arg(++minted);
        keys.insert(effect.templateKey);
        p.effects.append(effect);
    }
    p.sourceDurationUs = static_cast<TimeUs>(o.value(QStringLiteral("sourceDurationMs")).toDouble(0.0) * 1000.0);
    for (const QJsonValue &v : o.value(QStringLiteral("controls")).toArray())
        p.controls.append(templateControlFromJson(v.toObject()));
    p.basedOn = o.value(QStringLiteral("basedOn")).toString();
    return p;
}

// ---------------------------------------------------------------------------------------------
// Bindings

const QList<TemplateBindingForm> &templateBindingCatalog()
{
    static const QList<TemplateBindingForm> forms{
        {QStringLiteral("text"), QStringLiteral("text"), QStringLiteral("The clip's text")},
        {QStringLiteral("font.family"), QStringLiteral("font"), QStringLiteral("Font family")},
        {QStringLiteral("font.weight"), QStringLiteral("scalar"), QStringLiteral("100..900")},
        {QStringLiteral("font.size"), QStringLiteral("scalar"), QStringLiteral("Text size, px at project height (same as pixelSize)")},
        {QStringLiteral("font.italic"), QStringLiteral("bool"), QString()},
        {QStringLiteral("align"), QStringLiteral("enum"), QStringLiteral("left | center | right")},
        {QStringLiteral("pixelSize"), QStringLiteral("scalar"), QStringLiteral("Text size, px")},
        {QStringLiteral("letterSpacing"), QStringLiteral("scalar"), QStringLiteral("px")},
        {QStringLiteral("lineHeight"), QStringLiteral("scalar"), QStringLiteral("Multiple of the font's line spacing")},
        {QStringLiteral("boxPadding"), QStringLiteral("scalar"), QStringLiteral("px")},
        {QStringLiteral("pathBend"), QStringLiteral("scalar"), QStringLiteral("-100..100")},
        {QStringLiteral("underlineWidth"), QStringLiteral("scalar"), QStringLiteral("px")},
        {QStringLiteral("underlineOffset"), QStringLiteral("scalar"), QStringLiteral("px below the baseline")},
        {QStringLiteral("underlineReveal"), QStringLiteral("scalar"), QStringLiteral("0..1, drawn part of the underline")},
        {QStringLiteral("effectPadding"), QStringLiteral("scalar"), QStringLiteral("px of room around the text for clip effects")},
        {QStringLiteral("layer.<id>.enabled"), QStringLiteral("bool"), QStringLiteral("Show or hide one shading layer")},
        {QStringLiteral("layer.<id>.color"), QStringLiteral("color"), QStringLiteral("A solid, texture or effect paint's colour")},
        {QStringLiteral("layer.<id>.gradient.stop.<n>.color"), QStringLiteral("color"), QStringLiteral("One gradient stop's colour")},
        {QStringLiteral("layer.<id>.<field>"), QStringLiteral("scalar"),
         QStringLiteral("opacity, offsetX, offsetY, blur; width (stroke, extrude); spread (shadow, glow); trimStart, "
                        "trimEnd, dashOffset (stroke); gradient.angle/offset/scale; effect.<param> (shader paint)")},
        {QStringLiteral("box.enabled"), QStringLiteral("bool"), QStringLiteral("Background box")},
        {QStringLiteral("box.color"), QStringLiteral("color"), QString()},
        {QStringLiteral("box.radius"), QStringLiteral("scalar"), QStringLiteral("px")},
        {QStringLiteral("underline.enabled"), QStringLiteral("bool"), QString()},
        {QStringLiteral("underline.color"), QStringLiteral("color"), QString()},
        {QStringLiteral("animation.<in|out|loop>.enabled"), QStringLiteral("bool"), QStringLiteral("Turn the entrance, exit or loop on or off")},
        {QStringLiteral("animation.<in|out|loop>.preset"), QStringLiteral("enum"), QStringLiteral("Animation preset id; options list the choices")},
        {QStringLiteral("animation.<in|out>.duration"), QStringLiteral("scalar"), QStringLiteral("Seconds")},
        {QStringLiteral("animation.loop.duration"), QStringLiteral("scalar"), QStringLiteral("Loop period, seconds (0 = one pass)")},
        {QStringLiteral("animation.<slot>.param.<id>"), QStringLiteral("(param)"), QStringLiteral("A param of the slot's preset, its own type")},
        {QStringLiteral("timing.fit"), QStringLiteral("bool"), QStringLiteral("Fit entrance + exit to half of a short clip")},
        {QStringLiteral("timing.maxReadMs"), QStringLiteral("scalar"), QStringLiteral("Whole text readable by this time, ms (0 = no cap)")},
        {QStringLiteral("effect.<key>.enabled"), QStringLiteral("bool"), QStringLiteral("One of the template's clip effects")},
        {QStringLiteral("effect.<key>.<param>"), QStringLiteral("(param)"), QStringLiteral("An effect parameter, its own type")},
    };
    return forms;
}

namespace {

const QStringList &flatScalarBindings()
{
    static const QStringList keys{QStringLiteral("pixelSize"),       QStringLiteral("letterSpacing"),
                                  QStringLiteral("lineHeight"),      QStringLiteral("boxPadding"),
                                  QStringLiteral("pathBend"),        QStringLiteral("underlineWidth"),
                                  QStringLiteral("underlineOffset"), QStringLiteral("underlineReveal")};
    return keys;
}

TextAnimationSlot *animationSlot(TextStyle &style, const QString &name)
{
    if (name == QLatin1String("in"))
        return &style.animation.in;
    if (name == QLatin1String("out"))
        return &style.animation.out;
    if (name == QLatin1String("loop"))
        return &style.animation.loop;
    return nullptr;
}

const TextAnimationSlot *animationSlot(const TextStyle &style, const QString &name)
{
    return animationSlot(const_cast<TextStyle &>(style), name);
}

TextAnimSlotKind slotKind(const QString &name)
{
    return name == QLatin1String("out") ? TextAnimSlotKind::Out
           : name == QLatin1String("loop") ? TextAnimSlotKind::Loop
                                           : TextAnimSlotKind::In;
}

const Effect *effectForKey(const QList<Effect> &effects, const QString &key)
{
    for (const Effect &e : effects)
        if (e.templateKey == key)
            return &e;
    return nullptr;
}

Effect *effectForKey(QList<Effect> &effects, const QString &key)
{
    for (Effect &e : effects)
        if (e.templateKey == key)
            return &e;
    return nullptr;
}

QStringList layerIds(const TextStyle &style)
{
    QStringList ids;
    for (const TextShadingLayer &l : style.layers)
        ids.append(l.id);
    return ids;
}

QStringList effectKeys(const QList<Effect> &effects)
{
    QStringList keys;
    for (const Effect &e : effects)
        if (!e.templateKey.isEmpty())
            keys.append(e.templateKey);
    return keys;
}

QString listOrNone(const QStringList &items)
{
    return items.isEmpty() ? QStringLiteral("(none)") : items.join(QStringLiteral(", "));
}

QString colorString(const QColor &c)
{
    return c.alpha() < 255 ? c.name(QColor::HexArgb) : c.name(QColor::HexRgb);
}

QString paramTypeName(TextAnimParamSpec::Type t, QStringList *enumValues, const TextAnimParamSpec &spec)
{
    switch (t) {
    case TextAnimParamSpec::Type::Scalar:
        return QStringLiteral("scalar");
    case TextAnimParamSpec::Type::Color:
        return QStringLiteral("color");
    case TextAnimParamSpec::Type::Bool:
        return QStringLiteral("bool");
    case TextAnimParamSpec::Type::Text:
        return QStringLiteral("text");
    case TextAnimParamSpec::Type::Enum:
        if (enumValues)
            *enumValues = spec.enumValues;
        return QStringLiteral("enum");
    case TextAnimParamSpec::Type::Vec2:
        return {};
    }
    return {};
}

std::optional<TextAnimationPreset> slotPreset(const TextAnimationSlot &slot)
{
    if (slot.presetId.isEmpty())
        return std::nullopt;
    return TextAnimationPresetCatalog::instance().presetForId(slot.presetId);
}

// Value of a preset param on a slot: the override, else the preset's default.
std::optional<VectorSlotValue> slotParam(const TextAnimationSlot &slot, const QString &id)
{
    const auto it = slot.params.constFind(id);
    if (it != slot.params.constEnd())
        return *it;
    if (const std::optional<TextAnimationPreset> preset = slotPreset(slot)) {
        const QMap<QString, VectorSlotValue> defaults = preset->defaultParams();
        const auto d = defaults.constFind(id);
        if (d != defaults.constEnd())
            return *d;
    }
    return std::nullopt;
}

QVariant slotValueToVariant(const VectorSlotValue &v, bool asBool)
{
    switch (v.type) {
    case VectorSlotValue::Type::Color:
        return colorString(v.color);
    case VectorSlotValue::Type::Scalar:
        return asBool ? QVariant(v.scalar >= 0.5) : QVariant(v.scalar);
    case VectorSlotValue::Type::Text:
        return v.text;
    default:
        return {};
    }
}

int editDistance(const QString &a, const QString &b)
{
    QList<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

} // namespace

// Whether a loop preset's motion follows the slot's progress (so its period matters) rather than
// absolute time (wiggle: its own speed param) or the spoken word (karaoke).
static bool presetFollowsLoopPeriod(const TextAnimationPreset &preset)
{
    for (const QJsonValue &a : preset.animators) {
        for (const QJsonValue &sel : a.toObject().value(QStringLiteral("selectors")).toArray()) {
            if (sel.isString())
                return true; // the "stagger" shorthand
            const QString driver = sel.toObject().value(QStringLiteral("driver")).toString(QStringLiteral("curves"));
            if (driver != QLatin1String("wiggle") && driver != QLatin1String("karaoke"))
                return true;
        }
        if (a.toObject().value(QStringLiteral("selectors")).toArray().isEmpty())
            return true;
    }
    return false;
}

static QString presetSpeedParams(const TextAnimationPreset &preset)
{
    QStringList ids;
    for (const TextAnimParamSpec &p : preset.params)
        if (p.unit == QLatin1String("Hz") || p.id == QLatin1String("speed") || p.id == QLatin1String("frequency"))
            ids.append(QStringLiteral("animation.loop.param.%1").arg(p.id));
    return ids.isEmpty() ? QStringLiteral("it has none") : ids.join(QStringLiteral(", "));
}

// Every concrete path that resolves on this style and effect list, in a stable order.
static QStringList concreteBindings(const TextStyle &style, const QList<Effect> &effects)
{
    QStringList out{QStringLiteral("text"),        QStringLiteral("font.family"), QStringLiteral("font.weight"),
                    QStringLiteral("font.size"),   QStringLiteral("font.italic"), QStringLiteral("align")};
    out += flatScalarBindings();
    out << QStringLiteral("effectPadding");
    for (const TextShadingLayer &l : style.layers) {
        out << QStringLiteral("layer.%1.enabled").arg(l.id);
        if (l.paint.kind != TextPaintKind::Gradient)
            out << QStringLiteral("layer.%1.color").arg(l.id);
        else
            for (int i = 0; i < l.paint.gradient.stops.size(); ++i)
                out << QStringLiteral("layer.%1.gradient.stop.%2.color").arg(l.id).arg(i);
        for (const QString &f : shadingLayerKeyframeFields(l))
            if (!f.startsWith(QLatin1String("color.")))
                out << QStringLiteral("layer.%1.%2").arg(l.id, f);
    }
    out << QStringLiteral("box.enabled") << QStringLiteral("box.color") << QStringLiteral("box.radius")
        << QStringLiteral("underline.enabled") << QStringLiteral("underline.color");
    for (const char *name : {"in", "out", "loop"}) {
        const QString slot = QLatin1String(name);
        out << QStringLiteral("animation.%1.enabled").arg(slot) << QStringLiteral("animation.%1.preset").arg(slot)
            << QStringLiteral("animation.%1.duration").arg(slot);
        if (const std::optional<TextAnimationPreset> preset = slotPreset(*animationSlot(style, slot)))
            for (const TextAnimParamSpec &p : preset->params)
                if (p.type != TextAnimParamSpec::Type::Vec2)
                    out << QStringLiteral("animation.%1.param.%2").arg(slot, p.id);
    }
    out << QStringLiteral("timing.fit") << QStringLiteral("timing.maxReadMs");
    for (const Effect &e : effects) {
        if (e.templateKey.isEmpty())
            continue;
        out << QStringLiteral("effect.%1.enabled").arg(e.templateKey);
        for (auto it = e.parameters.constBegin(); it != e.parameters.constEnd(); ++it)
            out << QStringLiteral("effect.%1.%2").arg(e.templateKey, it.key());
    }
    return out;
}

QString templateBindingType(const QString &bind, const TextStyle &style, const QList<Effect> &effects,
                            const TemplateEnvironment &env, QString *why, QStringList *enumValues)
{
    const auto fail = [&](const QString &message) {
        if (why)
            *why = message;
        return QString();
    };
    if (enumValues)
        enumValues->clear();
    const QStringList parts = bind.split(QLatin1Char('.'));

    if (bind == QLatin1String("text"))
        return QStringLiteral("text");
    if (bind == QLatin1String("font.family"))
        return QStringLiteral("font");
    if (bind == QLatin1String("font.weight") || bind == QLatin1String("font.size")
        || bind == QLatin1String("effectPadding") || bind == QLatin1String("box.radius")
        || bind == QLatin1String("timing.maxReadMs"))
        return QStringLiteral("scalar");
    if (bind == QLatin1String("font.italic") || bind == QLatin1String("box.enabled")
        || bind == QLatin1String("underline.enabled") || bind == QLatin1String("timing.fit"))
        return QStringLiteral("bool");
    if (bind == QLatin1String("box.color") || bind == QLatin1String("underline.color"))
        return QStringLiteral("color");
    if (bind == QLatin1String("align")) {
        if (enumValues)
            *enumValues = {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")};
        return QStringLiteral("enum");
    }
    if (flatScalarBindings().contains(bind))
        return QStringLiteral("scalar");

    if (parts.first() == QLatin1String("layer")) {
        if (parts.size() < 3)
            return fail(QStringLiteral("\"%1\" is incomplete; expected layer.<id>.<field>").arg(bind));
        const TextShadingLayer *layer = findTextLayer(style.layers, parts.at(1));
        if (!layer)
            return fail(QStringLiteral("no layer \"%1\" in this style; its layers are %2")
                            .arg(parts.at(1), listOrNone(layerIds(style))));
        const QString field = parts.mid(2).join(QLatin1Char('.'));
        if (field == QLatin1String("enabled"))
            return QStringLiteral("bool");
        if (field == QLatin1String("color")) {
            if (layer->paint.kind == TextPaintKind::Gradient)
                return fail(QStringLiteral("layer \"%1\" paints a gradient, which has no single colour; bind "
                                           "layer.%1.gradient.stop.<n>.color instead")
                                .arg(layer->id));
            return QStringLiteral("color");
        }
        static const QRegularExpression stopColor(QStringLiteral("^gradient\\.stop\\.(\\d+)\\.color$"));
        const QRegularExpressionMatch m = stopColor.match(field);
        if (m.hasMatch()) {
            if (layer->paint.kind != TextPaintKind::Gradient)
                return fail(QStringLiteral("layer \"%1\" has no gradient; bind layer.%1.color").arg(layer->id));
            if (m.captured(1).toInt() >= layer->paint.gradient.stops.size())
                return fail(QStringLiteral("layer \"%1\" has %2 gradient stops (0..%3)")
                                .arg(layer->id)
                                .arg(layer->paint.gradient.stops.size())
                                .arg(layer->paint.gradient.stops.size() - 1));
            return QStringLiteral("color");
        }
        double probe = 0.0;
        if (!field.startsWith(QLatin1String("color.")) && shadingLayerScalar(*layer, field, &probe))
            return QStringLiteral("scalar");
        QStringList fields{QStringLiteral("enabled"), QStringLiteral("color")};
        for (const QString &f : shadingLayerKeyframeFields(*layer))
            if (!f.startsWith(QLatin1String("color.")))
                fields.append(f);
        return fail(QStringLiteral("layer \"%1\" (%2) has no field \"%3\"; accepted: %4")
                        .arg(layer->id, textLayerKindToString(layer->kind), field, fields.join(QStringLiteral(", "))));
    }

    if (parts.first() == QLatin1String("animation")) {
        if (parts.size() < 3)
            return fail(QStringLiteral("\"%1\" is incomplete; expected animation.<in|out|loop>.<enabled|preset|duration|param.<id>>")
                            .arg(bind));
        const TextAnimationSlot *slot = animationSlot(style, parts.at(1));
        if (!slot)
            return fail(QStringLiteral("no animation slot \"%1\"; slots are in, out, loop").arg(parts.at(1)));
        const QString field = parts.at(2);
        if (field == QLatin1String("enabled") && parts.size() == 3)
            return QStringLiteral("bool");
        if (field == QLatin1String("duration") && parts.size() == 3) {
            // A control that moves nothing is worse than none: say where the speed really lives.
            if (const std::optional<TextAnimationPreset> preset = slotPreset(*slot)) {
                if (parts.at(1) == QLatin1String("loop") && !presetFollowsLoopPeriod(*preset))
                    return fail(QStringLiteral("loop preset \"%1\" runs on its own clock, so animation.loop.duration has "
                                               "no effect; bind its speed param instead (%2)")
                                    .arg(preset->id, presetSpeedParams(*preset)));
                if (parts.at(1) != QLatin1String("loop") && preset->flag(QStringLiteral("durationLocked")))
                    return fail(QStringLiteral("preset \"%1\" ignores duration (its length comes from stagger and its "
                                               "own params); bind animation.%2.param.stagger or one of its params instead")
                                    .arg(preset->id, parts.at(1)));
            }
            return QStringLiteral("scalar");
        }
        if (field == QLatin1String("preset") && parts.size() == 3) {
            if (enumValues)
                for (const TextAnimationPreset &p : TextAnimationPresetCatalog::instance().presetsFor(slotKind(parts.at(1))))
                    enumValues->append(p.id);
            return QStringLiteral("enum");
        }
        if (field == QLatin1String("param") && parts.size() == 4) {
            const std::optional<TextAnimationPreset> preset = slotPreset(*slot);
            if (!preset)
                return fail(QStringLiteral("animation.%1 has no preset, so it has no params").arg(parts.at(1)));
            const TextAnimParamSpec *spec = preset->param(parts.at(3));
            if (!spec) {
                QStringList ids;
                for (const TextAnimParamSpec &p : preset->params)
                    ids.append(p.id);
                return fail(QStringLiteral("preset \"%1\" has no param \"%2\"; its params are %3")
                                .arg(preset->id, parts.at(3), listOrNone(ids)));
            }
            const QString type = paramTypeName(spec->type, enumValues, *spec);
            if (type.isEmpty())
                return fail(QStringLiteral("param \"%1\" is a 2D point, which quick controls cannot drive").arg(spec->id));
            return type;
        }
        return fail(QStringLiteral("\"%1\": expected animation.%2.enabled, .preset, .duration or .param.<id>")
                        .arg(bind, parts.at(1)));
    }

    if (parts.first() == QLatin1String("effect")) {
        if (parts.size() < 3)
            return fail(QStringLiteral("\"%1\" is incomplete; expected effect.<key>.<enabled|param>").arg(bind));
        const Effect *effect = effectForKey(effects, parts.at(1));
        if (!effect)
            return fail(QStringLiteral("no effect with key \"%1\"; the template's effect keys are %2")
                            .arg(parts.at(1), listOrNone(effectKeys(effects))));
        const QString param = parts.mid(2).join(QLatin1Char('.'));
        if (param == QLatin1String("enabled"))
            return QStringLiteral("bool");
        if (env.effect) {
            const std::optional<EffectPresetMeta> meta = env.effect(effect->catalogId);
            if (!meta)
                return fail(QStringLiteral("effect \"%1\" is not installed").arg(effect->catalogId));
            for (const EffectParamSpec &spec : meta->parameters) {
                if (spec.key != param)
                    continue;
                switch (spec.type) {
                case EffectParamType::Float:
                case EffectParamType::Int:
                case EffectParamType::Seed:
                case EffectParamType::Enum: // the option index
                    return QStringLiteral("scalar");
                case EffectParamType::Bool:
                    return QStringLiteral("bool");
                case EffectParamType::Color:
                    return QStringLiteral("color");
                default:
                    return fail(QStringLiteral("parameter \"%1\" of effect \"%2\" is not a number, switch or colour; "
                                               "quick controls cannot drive it")
                                    .arg(param, effect->catalogId));
                }
            }
            QStringList keys;
            for (const EffectParamSpec &spec : meta->parameters)
                keys.append(spec.key);
            return fail(QStringLiteral("effect \"%1\" has no parameter \"%2\"; its parameters are %3")
                            .arg(effect->catalogId, param, listOrNone(keys)));
        }
        const auto it = effect->parameters.constFind(param);
        if (it == effect->parameters.constEnd())
            return fail(QStringLiteral("effect \"%1\" (key %2) sets no parameter \"%3\"")
                            .arg(effect->catalogId, effect->templateKey, param));
        if (it->typeId() == QMetaType::Bool)
            return QStringLiteral("bool");
        if (it->typeId() == QMetaType::QString && it->toString().startsWith(QLatin1Char('#')))
            return QStringLiteral("color");
        return QStringLiteral("scalar");
    }

    // Anything else: the nearest real path is the most useful hint.
    QString best;
    int bestDistance = std::numeric_limits<int>::max();
    for (const QString &candidate : concreteBindings(style, effects)) {
        const int d = editDistance(bind, candidate);
        if (d < bestDistance) {
            bestDistance = d;
            best = candidate;
        }
    }
    if (!best.isEmpty() && bestDistance <= std::max(3, static_cast<int>(bind.size() / 4)))
        return fail(QStringLiteral("unknown property \"%1\"; did you mean \"%2\"?").arg(bind, best));
    return fail(QStringLiteral("unknown property \"%1\"; see the binding list in the authoring guide").arg(bind));
}

QVariant readTemplateBinding(const QString &bind, const TextStyle &style, const QList<Effect> &effects,
                             const QString &content)
{
    const QStringList parts = bind.split(QLatin1Char('.'));
    if (bind == QLatin1String("text"))
        return content;
    if (bind == QLatin1String("font.family"))
        return style.fontFamily;
    if (bind == QLatin1String("font.weight"))
        return double(style.fontWeight);
    if (bind == QLatin1String("font.size"))
        return double(style.pixelSize);
    if (bind == QLatin1String("font.italic"))
        return style.italic;
    if (bind == QLatin1String("align"))
        return textAlignToString(style.align);
    if (bind == QLatin1String("effectPadding"))
        return style.effectPadding;
    if (bind == QLatin1String("box.enabled"))
        return style.boxEnabled;
    if (bind == QLatin1String("box.color"))
        return colorString(style.boxColor);
    if (bind == QLatin1String("box.radius"))
        return style.boxRadius;
    if (bind == QLatin1String("underline.enabled"))
        return style.underlineEnabled;
    if (bind == QLatin1String("underline.color"))
        return colorString(style.underlineColor);
    if (bind == QLatin1String("timing.fit"))
        return style.animation.timing.fitToWindow;
    if (bind == QLatin1String("timing.maxReadMs"))
        return double(style.animation.timing.maxReadUs) / 1000.0;
    if (flatScalarBindings().contains(bind)) {
        double v = 0.0;
        return textStyleScalar(style, bind, &v) ? QVariant(v) : QVariant();
    }
    if (parts.first() == QLatin1String("layer") && parts.size() >= 3) {
        const TextShadingLayer *layer = findTextLayer(style.layers, parts.at(1));
        if (!layer)
            return {};
        const QString field = parts.mid(2).join(QLatin1Char('.'));
        if (field == QLatin1String("enabled"))
            return layer->enabled;
        if (field == QLatin1String("color"))
            return colorString(layer->paint.color);
        if (parts.size() == 6 && parts.at(2) == QLatin1String("gradient") && parts.at(5) == QLatin1String("color")) {
            const int i = parts.at(4).toInt();
            return i >= 0 && i < layer->paint.gradient.stops.size()
                       ? QVariant(colorString(layer->paint.gradient.stops.at(i).color))
                       : QVariant();
        }
        double v = 0.0;
        return shadingLayerScalar(*layer, field, &v) ? QVariant(v) : QVariant();
    }
    if (parts.first() == QLatin1String("animation") && parts.size() >= 3) {
        const TextAnimationSlot *slot = animationSlot(style, parts.at(1));
        if (!slot)
            return {};
        if (parts.at(2) == QLatin1String("enabled"))
            return slot->enabled && slot->isActive();
        if (parts.at(2) == QLatin1String("preset"))
            return slot->presetId;
        if (parts.at(2) == QLatin1String("duration")) {
            const QString id = parts.at(1) == QLatin1String("loop") ? QStringLiteral("period") : QStringLiteral("duration");
            if (const std::optional<VectorSlotValue> v = slotParam(*slot, id))
                return v->scalar;
            return usToSeconds(parts.at(1) == QLatin1String("loop") ? slot->periodUs : slot->durationUs);
        }
        if (parts.at(2) == QLatin1String("param") && parts.size() == 4) {
            const std::optional<VectorSlotValue> v = slotParam(*slot, parts.at(3));
            if (!v)
                return {};
            bool isBool = false;
            if (const std::optional<TextAnimationPreset> preset = slotPreset(*slot))
                if (const TextAnimParamSpec *spec = preset->param(parts.at(3)))
                    isBool = spec->type == TextAnimParamSpec::Type::Bool;
            return slotValueToVariant(*v, isBool);
        }
        return {};
    }
    if (parts.first() == QLatin1String("effect") && parts.size() >= 3) {
        const Effect *effect = effectForKey(effects, parts.at(1));
        if (!effect)
            return {};
        const QString param = parts.mid(2).join(QLatin1Char('.'));
        if (param == QLatin1String("enabled"))
            return effect->enabled;
        return effect->parameters.value(param);
    }
    return {};
}

bool writeTemplateBinding(const QString &bind, const QVariant &value, TextStyle *style, QList<Effect> *effects,
                          QString *content)
{
    if (!style)
        return false;
    const QStringList parts = bind.split(QLatin1Char('.'));
    const auto colorOf = [&](QColor *out) {
        const QColor c(value.toString());
        if (!c.isValid())
            return false;
        *out = c;
        return true;
    };
    if (bind == QLatin1String("text")) {
        if (!content)
            return false;
        *content = value.toString();
        return true;
    }
    if (bind == QLatin1String("font.family")) {
        if (value.toString().trimmed().isEmpty())
            return false;
        style->fontFamily = value.toString().trimmed();
        return true;
    }
    if (bind == QLatin1String("font.weight")) {
        style->fontWeight = qBound(100, qRound(value.toDouble() / 100.0) * 100, 900);
        return true;
    }
    if (bind == QLatin1String("font.size"))
        return setTextStyleScalar(*style, QStringLiteral("pixelSize"), value.toDouble());
    if (bind == QLatin1String("font.italic")) {
        style->italic = value.toBool();
        return true;
    }
    if (bind == QLatin1String("align")) {
        const QString a = value.toString();
        if (a != QLatin1String("left") && a != QLatin1String("center") && a != QLatin1String("right"))
            return false;
        style->align = textAlignFromString(a);
        return true;
    }
    if (bind == QLatin1String("effectPadding")) {
        style->effectPadding = qBound(0.0, value.toDouble(), 2000.0);
        return true;
    }
    if (bind == QLatin1String("box.enabled")) {
        style->boxEnabled = value.toBool();
        return true;
    }
    if (bind == QLatin1String("box.color"))
        return colorOf(&style->boxColor);
    if (bind == QLatin1String("box.radius")) {
        style->boxRadius = qMax(0.0, value.toDouble());
        return true;
    }
    if (bind == QLatin1String("underline.enabled")) {
        style->underlineEnabled = value.toBool();
        return true;
    }
    if (bind == QLatin1String("underline.color"))
        return colorOf(&style->underlineColor);
    if (bind == QLatin1String("timing.fit")) {
        style->animation.timing.fitToWindow = value.toBool();
        return true;
    }
    if (bind == QLatin1String("timing.maxReadMs")) {
        style->animation.timing.maxReadUs = static_cast<TimeUs>(qBound(0.0, value.toDouble(), 10000.0) * 1000.0);
        return true;
    }
    if (flatScalarBindings().contains(bind))
        return setTextStyleScalar(*style, bind, value.toDouble());

    if (parts.first() == QLatin1String("layer") && parts.size() >= 3) {
        TextShadingLayer *layer = findTextLayer(style->layers, parts.at(1));
        if (!layer)
            return false;
        const QString field = parts.mid(2).join(QLatin1Char('.'));
        bool changed = false;
        if (field == QLatin1String("enabled")) {
            layer->enabled = value.toBool();
            changed = true;
        } else if (field == QLatin1String("color")) {
            if (layer->paint.kind == TextPaintKind::Gradient || !colorOf(&layer->paint.color))
                return false;
            changed = true;
        } else if (parts.size() == 6 && parts.at(2) == QLatin1String("gradient") && parts.at(5) == QLatin1String("color")) {
            const int i = parts.at(4).toInt();
            if (i < 0 || i >= layer->paint.gradient.stops.size() || !colorOf(&layer->paint.gradient.stops[i].color))
                return false;
            changed = true;
        } else {
            changed = !field.startsWith(QLatin1String("color.")) && setShadingLayerScalar(*layer, field, value.toDouble());
        }
        // A quick control is a hand edit of the stack: the look recipe no longer describes it.
        if (changed)
            style->lookId.clear();
        return changed;
    }
    if (parts.first() == QLatin1String("animation") && parts.size() >= 3) {
        TextAnimationSlot *slot = animationSlot(*style, parts.at(1));
        if (!slot)
            return false;
        const QString field = parts.at(2);
        if (field == QLatin1String("enabled")) {
            slot->enabled = value.toBool();
            return true;
        }
        if (field == QLatin1String("preset")) {
            const QString id = value.toString();
            if (!id.isEmpty()) {
                const std::optional<TextAnimationPreset> preset = TextAnimationPresetCatalog::instance().presetForId(id);
                if (!preset || !preset->supportsSlot(slotKind(parts.at(1))))
                    return false;
            }
            if (slot->presetId != id) {
                slot->presetId = id;
                slot->params.clear(); // a different preset starts from its own defaults
                slot->animators.clear();
            }
            return true;
        }
        if (field == QLatin1String("duration")) {
            const QString id = parts.at(1) == QLatin1String("loop") ? QStringLiteral("period") : QStringLiteral("duration");
            const double seconds = qBound(0.0, value.toDouble(), 60.0);
            if (slot->presetId.isEmpty()) {
                (parts.at(1) == QLatin1String("loop") ? slot->periodUs : slot->durationUs) = secondsToUs(seconds);
                return true;
            }
            slot->params.insert(id, VectorSlotValue::fromScalar(seconds));
            return true;
        }
        if (field == QLatin1String("param") && parts.size() == 4) {
            const std::optional<TextAnimationPreset> preset = slotPreset(*slot);
            const TextAnimParamSpec *spec = preset ? preset->param(parts.at(3)) : nullptr;
            if (!spec)
                return false;
            switch (spec->type) {
            case TextAnimParamSpec::Type::Scalar:
                slot->params.insert(spec->id, VectorSlotValue::fromScalar(qBound(spec->min, value.toDouble(), spec->max)));
                return true;
            case TextAnimParamSpec::Type::Bool:
                slot->params.insert(spec->id, VectorSlotValue::fromScalar(value.toBool() ? 1.0 : 0.0));
                return true;
            case TextAnimParamSpec::Type::Color: {
                QColor c;
                if (!colorOf(&c))
                    return false;
                slot->params.insert(spec->id, VectorSlotValue::fromColor(c));
                return true;
            }
            case TextAnimParamSpec::Type::Enum:
                if (!spec->enumValues.contains(value.toString()))
                    return false;
                slot->params.insert(spec->id, VectorSlotValue::fromText(value.toString()));
                return true;
            case TextAnimParamSpec::Type::Text:
                slot->params.insert(spec->id, VectorSlotValue::fromText(value.toString()));
                return true;
            case TextAnimParamSpec::Type::Vec2:
                return false;
            }
            return false;
        }
        return false;
    }
    if (parts.first() == QLatin1String("effect") && parts.size() >= 3 && effects) {
        Effect *effect = effectForKey(*effects, parts.at(1));
        if (!effect)
            return false;
        const QString param = parts.mid(2).join(QLatin1Char('.'));
        if (param == QLatin1String("enabled")) {
            effect->enabled = value.toBool();
            return true;
        }
        const QVariant current = effect->parameters.value(param);
        if (value.typeId() == QMetaType::Bool && current.typeId() != QMetaType::Bool)
            effect->parameters.insert(param, value.toBool() ? 1.0 : 0.0); // switches bind as 0 / 1
        else if (value.typeId() == QMetaType::QString)
            effect->parameters.insert(param, value.toString());
        else if (value.typeId() == QMetaType::Bool)
            effect->parameters.insert(param, value.toBool());
        else
            effect->parameters.insert(param, value.toDouble());
        // A static value replaces the animated one, as the inspector does.
        effect->paramKeyframes.remove(param);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Validation

namespace {

class Issues
{
public:
    void error(const QString &code, const QString &path, const QString &message, const QString &expected = {})
    {
        add(TemplateIssue::Severity::Error, code, path, message, expected);
    }
    void warning(const QString &code, const QString &path, const QString &message, const QString &expected = {})
    {
        add(TemplateIssue::Severity::Warning, code, path, message, expected);
    }
    QList<TemplateIssue> list;

private:
    void add(TemplateIssue::Severity severity, const QString &code, const QString &path, const QString &message,
             const QString &expected)
    {
        TemplateIssue issue;
        issue.severity = severity;
        issue.code = code;
        issue.path = path;
        issue.message = message;
        issue.expected = expected;
        list.append(issue);
    }
};

QString jsonTypeName(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return QStringLiteral("a boolean");
    case QJsonValue::Double:
        return QStringLiteral("a number");
    case QJsonValue::String:
        return QStringLiteral("a string");
    case QJsonValue::Array:
        return QStringLiteral("an array");
    case QJsonValue::Object:
        return QStringLiteral("an object");
    case QJsonValue::Undefined:
        return QStringLiteral("missing");
    }
    return QStringLiteral("unknown");
}

// Version strings compared numerically, part by part ("1.10.0" > "1.9.2").
int compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

const QSet<QString> &knownTopLevelKeys()
{
    static const QSet<QString> keys{QStringLiteral("format"),   QStringLiteral("id"),       QStringLiteral("label"),
                                    QStringLiteral("description"), QStringLiteral("category"), QStringLiteral("tags"),
                                    QStringLiteral("kind"),     QStringLiteral("sampleText"), QStringLiteral("requires"),
                                    QStringLiteral("style"),    QStringLiteral("effects"),  QStringLiteral("controls"),
                                    QStringLiteral("basedOn"),  QStringLiteral("sourceDurationMs"),
                                    QStringLiteral("$schema"),  QStringLiteral("_comment")};
    return keys;
}

// Everything textStyleToJson writes plus the v6 names textStyleFromJson still migrates.
const QSet<QString> &knownStyleKeys()
{
    static const QSet<QString> keys = [] {
        QSet<QString> k;
        const QJsonObject probe = textStyleToJson(TextStyle{});
        for (auto it = probe.constBegin(); it != probe.constEnd(); ++it)
            k.insert(it.key());
        for (const char *extra : {"lookId", "lookParams", "keyframes", "underlineReveal", "effectPadding", "templateId",
                                  "color", "outlineEnabled", "outlineColor", "outlineWidth", "outlineOutside",
                                  "shadowEnabled", "shadowColor", "shadowOffsetX", "shadowOffsetY", "shadowBlur",
                                  "shadowOpacity", "glowEnabled", "glowColor", "glowRadius", "glowOpacity",
                                  "fillKind", "gradientStart", "gradientEnd", "gradientAngle", "animIn", "animOut"})
            k.insert(QLatin1String(extra));
        return k;
    }();
    return keys;
}

void checkStyle(const QJsonObject &json, const TextStyle &style, const TemplateEnvironment &env, Issues &issues)
{
    for (auto it = json.constBegin(); it != json.constEnd(); ++it)
        if (!knownStyleKeys().contains(it.key()))
            issues.warning(QStringLiteral("schema"), QStringLiteral("style.%1").arg(it.key()),
                           QStringLiteral("unknown style field, ignored"));

    if (style.fontFamily.trimmed().isEmpty())
        issues.error(QStringLiteral("schema"), QStringLiteral("style.fontFamily"), QStringLiteral("a font family is required"));
    else if (env.fontAvailable && !env.fontAvailable(style.fontFamily))
        issues.warning(QStringLiteral("missing_dependency"), QStringLiteral("style.fontFamily"),
                       QStringLiteral("font \"%1\" is not installed here; Dafin will draw the text in a fallback font")
                           .arg(style.fontFamily));
    if (json.contains(QStringLiteral("pixelSize"))) {
        const double size = json.value(QStringLiteral("pixelSize")).toDouble(-1);
        if (size < 4 || size > 1000)
            issues.error(QStringLiteral("range"), QStringLiteral("style.pixelSize"),
                         QStringLiteral("%1 is out of range").arg(size), QStringLiteral("4..1000 px"));
    }
    if (json.contains(QStringLiteral("fontWeight"))) {
        const double weight = json.value(QStringLiteral("fontWeight")).toDouble(-1);
        if (weight < 100 || weight > 900)
            issues.error(QStringLiteral("range"), QStringLiteral("style.fontWeight"),
                         QStringLiteral("%1 is out of range").arg(weight), QStringLiteral("100..900"));
    }
    if (json.contains(QStringLiteral("align"))) {
        const QString a = json.value(QStringLiteral("align")).toString();
        if (a != QLatin1String("left") && a != QLatin1String("center") && a != QLatin1String("right"))
            issues.error(QStringLiteral("schema"), QStringLiteral("style.align"), QStringLiteral("\"%1\" is not an alignment").arg(a),
                         QStringLiteral("left | center | right"));
    }

    // Layers: kinds, paints, unique ids, known shader paints.
    const QJsonArray layers = json.value(QStringLiteral("layers")).toArray();
    if (json.contains(QStringLiteral("layers")) && !json.value(QStringLiteral("layers")).isArray())
        issues.error(QStringLiteral("schema"), QStringLiteral("style.layers"),
                     QStringLiteral("must be an array, got %1").arg(jsonTypeName(json.value(QStringLiteral("layers")))));
    QSet<QString> ids;
    for (int i = 0; i < layers.size(); ++i) {
        const QString path = QStringLiteral("style.layers[%1]").arg(i);
        const QJsonObject layer = layers.at(i).toObject();
        const QString id = layer.value(QStringLiteral("id")).toString();
        static const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9_-]*$"));
        if (!idPattern.match(id).hasMatch())
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".id"),
                         QStringLiteral("\"%1\" is not a valid layer id").arg(id),
                         QStringLiteral("lowercase letters, digits, - and _, starting with a letter"));
        else if (ids.contains(id))
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".id"),
                         QStringLiteral("layer id \"%1\" is used twice; ids must be unique").arg(id));
        ids.insert(id);
        const QString kind = layer.value(QStringLiteral("kind")).toString();
        if (textLayerKindToString(textLayerKindFromString(kind)) != kind)
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".kind"),
                         QStringLiteral("\"%1\" is not a layer kind").arg(kind),
                         QStringLiteral("fill | stroke | shadow | glow | extrude"));
        const QJsonObject paint = layer.value(QStringLiteral("paint")).toObject();
        const QString paintKind = paint.value(QStringLiteral("kind")).toString(QStringLiteral("solid"));
        if (textPaintKindToString(textPaintKindFromString(paintKind)) != paintKind)
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".paint.kind"),
                         QStringLiteral("\"%1\" is not a paint kind").arg(paintKind),
                         QStringLiteral("solid | gradient | texture | effect"));
        if (paintKind == QLatin1String("effect")) {
            const QString effectId = paint.value(QStringLiteral("effect")).toObject().value(QStringLiteral("id")).toString();
            if (!textShaderEffectSpec(effectId)) {
                QStringList known;
                for (const TextEffectSpec &spec : textShaderEffectSpecs())
                    known.append(spec.id);
                issues.error(QStringLiteral("unsupported"), path + QStringLiteral(".paint.effect.id"),
                             QStringLiteral("\"%1\" is not a text shader paint").arg(effectId), known.join(QStringLiteral(" | ")));
            }
        }
        if (paintKind == QLatin1String("texture")) {
            const QString file = paint.value(QStringLiteral("texture")).toObject().value(QStringLiteral("path")).toString();
            if (QDir::isAbsolutePath(file) || file.contains(QLatin1String("..")))
                issues.error(QStringLiteral("unsupported"), path + QStringLiteral(".paint.texture.path"),
                             QStringLiteral("textures must be files inside the package, given by a relative path"));
        }
    }
    if (json.contains(QStringLiteral("layers")) && layers.isEmpty())
        issues.warning(QStringLiteral("schema"), QStringLiteral("style.layers"), QStringLiteral("no layers: the text will be invisible"));

    if (!style.lookId.isEmpty() && !textLookForId(style.lookId))
        issues.warning(QStringLiteral("unsupported"), QStringLiteral("style.lookId"),
                       QStringLiteral("unknown look \"%1\"; the stack is kept but its look sliders will not show").arg(style.lookId));

    // Animation slots: the preset exists, plays in that slot, and every param is one it has.
    const QJsonObject animation = json.value(QStringLiteral("animation")).toObject();
    for (const char *name : {"in", "out", "loop"}) {
        const QString slotName = QLatin1String(name);
        const QJsonObject slot = animation.value(slotName).toObject();
        const QString preset = slot.value(QStringLiteral("preset")).toString();
        const QString path = QStringLiteral("style.animation.%1").arg(slotName);
        if (preset.isEmpty())
            continue;
        const std::optional<TextAnimationPreset> found = TextAnimationPresetCatalog::instance().presetForId(preset);
        if (!found) {
            QStringList ids;
            for (const TextAnimationPreset &p : TextAnimationPresetCatalog::instance().presetsFor(slotKind(slotName)))
                ids.append(p.id);
            issues.error(QStringLiteral("unsupported"), path + QStringLiteral(".preset"),
                         QStringLiteral("unknown animation preset \"%1\"").arg(preset), ids.join(QStringLiteral(" | ")));
            continue;
        }
        if (!found->supportsSlot(slotKind(slotName))) {
            issues.error(QStringLiteral("unsupported"), path + QStringLiteral(".preset"),
                         QStringLiteral("preset \"%1\" does not play in the %2 slot").arg(preset, slotName));
            continue;
        }
        const QJsonObject params = slot.value(QStringLiteral("params")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const QString paramPath = QStringLiteral("%1.params.%2").arg(path, it.key());
            const TextAnimParamSpec *spec = found->param(it.key());
            // "period" is the loop slot's own setting (seconds, 0 = one pass over the clip), read for
            // every loop preset though none declares it.
            if (!spec && slotName == QLatin1String("loop") && it.key() == QLatin1String("period")) {
                QJsonValue period = it.value();
                if (period.isObject())
                    period = period.toObject().value(QStringLiteral("value"));
                if (!period.isDouble() || period.toDouble() < 0.0 || period.toDouble() > 600.0)
                    issues.error(QStringLiteral("range"), paramPath, QStringLiteral("period must be a number of seconds"),
                                 QStringLiteral("0..600 (0 = one pass over the clip)"));
                continue;
            }
            if (!spec) {
                QStringList ids;
                for (const TextAnimParamSpec &p : found->params)
                    ids.append(p.id);
                issues.error(QStringLiteral("schema"), paramPath,
                             QStringLiteral("preset \"%1\" has no param \"%2\"").arg(preset, it.key()),
                             ids.join(QStringLiteral(", ")));
                continue;
            }
            // Plain values and the typed {"type", "value"} form are both accepted.
            QJsonValue value = it.value();
            if (value.isObject())
                value = value.toObject().value(QStringLiteral("value"));
            switch (spec->type) {
            case TextAnimParamSpec::Type::Scalar:
                if (!value.isDouble())
                    issues.error(QStringLiteral("schema"), paramPath,
                                 QStringLiteral("expected a number, got %1").arg(jsonTypeName(value)),
                                 QStringLiteral("%1..%2 %3").arg(spec->min).arg(spec->max).arg(spec->unit).trimmed());
                else if (value.toDouble() < spec->min - 1e-9 || value.toDouble() > spec->max + 1e-9)
                    issues.error(QStringLiteral("range"), paramPath, QStringLiteral("%1 is out of range").arg(value.toDouble()),
                                 QStringLiteral("%1..%2 %3").arg(spec->min).arg(spec->max).arg(spec->unit).trimmed());
                break;
            case TextAnimParamSpec::Type::Bool:
                if (!value.isBool() && !value.isDouble())
                    issues.error(QStringLiteral("schema"), paramPath,
                                 QStringLiteral("expected true or false, got %1").arg(jsonTypeName(value)));
                break;
            case TextAnimParamSpec::Type::Color:
                if (!QColor(value.toString()).isValid())
                    issues.error(QStringLiteral("schema"), paramPath, QStringLiteral("expected a colour"),
                                 QStringLiteral("\"#rrggbb\" or \"#aarrggbb\""));
                break;
            case TextAnimParamSpec::Type::Enum:
                if (!spec->enumValues.contains(value.toString()))
                    issues.error(QStringLiteral("schema"), paramPath,
                                 QStringLiteral("\"%1\" is not one of the choices").arg(value.toString()),
                                 spec->enumValues.join(QStringLiteral(" | ")));
                break;
            case TextAnimParamSpec::Type::Text:
            case TextAnimParamSpec::Type::Vec2:
                break;
            }
        }
    }
    const QJsonObject timing = animation.value(QStringLiteral("timing")).toObject();
    if (timing.contains(QStringLiteral("maxReadMs"))) {
        const double ms = timing.value(QStringLiteral("maxReadMs")).toDouble(-1);
        if (ms < 0 || ms > 10000)
            issues.error(QStringLiteral("range"), QStringLiteral("style.animation.timing.maxReadMs"),
                         QStringLiteral("%1 is out of range").arg(ms), QStringLiteral("0..10000 ms (0 = no cap)"));
    }
}

} // namespace

QList<TemplateIssue> validateTitleTemplate(const QJsonObject &o, const TemplateEnvironment &env, TextPreset *parsed)
{
    Issues issues;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!knownTopLevelKeys().contains(it.key()))
            issues.warning(QStringLiteral("schema"), it.key(), QStringLiteral("unknown field, ignored"),
                           QStringList(knownTopLevelKeys().values()).join(QStringLiteral(", ")));

    // Identity
    const QJsonValue format = o.value(QStringLiteral("format"));
    if (!format.isDouble())
        issues.error(QStringLiteral("schema"), QStringLiteral("format"),
                     QStringLiteral("must be the number %1, got %2").arg(kTitleTemplateFormat).arg(jsonTypeName(format)));
    else if (format.toInt() > kTitleTemplateFormat)
        issues.error(QStringLiteral("compatibility"), QStringLiteral("format"),
                     QStringLiteral("format %1 is newer than this Dafin understands (%2)").arg(format.toInt()).arg(kTitleTemplateFormat));
    const QString id = o.value(QStringLiteral("id")).toString();
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{1,63}$"));
    if (!o.value(QStringLiteral("id")).isString() || !idPattern.match(id).hasMatch())
        issues.error(QStringLiteral("schema"), QStringLiteral("id"), QStringLiteral("\"%1\" is not a valid template id").arg(id),
                     QStringLiteral("2-64 lowercase letters, digits, '.', '-', '_', e.g. \"author.neon-pop\""));
    else if (id.startsWith(QLatin1String("user")) && id.contains(QLatin1Char(':')))
        issues.error(QStringLiteral("schema"), QStringLiteral("id"), QStringLiteral("ids starting with \"user:\" are reserved"));
    else if (env.idTaken && env.idTaken(id))
        issues.warning(QStringLiteral("compatibility"), QStringLiteral("id"),
                       QStringLiteral("another template already uses \"%1\"; installing this one replaces it").arg(id));
    if (o.value(QStringLiteral("label")).toString().trimmed().isEmpty())
        issues.error(QStringLiteral("schema"), QStringLiteral("label"), QStringLiteral("a label (the name users see) is required"));
    const QString kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("title"));
    if (kind != QLatin1String("title") && kind != QLatin1String("lower-third") && kind != QLatin1String("caption"))
        issues.error(QStringLiteral("schema"), QStringLiteral("kind"), QStringLiteral("\"%1\" is not a template kind").arg(kind),
                     QStringLiteral("title | lower-third | caption"));
    if (o.contains(QStringLiteral("tags")) && !o.value(QStringLiteral("tags")).isArray())
        issues.error(QStringLiteral("schema"), QStringLiteral("tags"), QStringLiteral("must be an array of strings"));

    // Requirements
    const QJsonObject req = o.value(QStringLiteral("requires")).toObject();
    const QString minApp = req.value(QStringLiteral("minAppVersion")).toString();
    if (!minApp.isEmpty() && !env.appVersion.isEmpty() && compareVersions(env.appVersion, minApp) < 0)
        issues.error(QStringLiteral("compatibility"), QStringLiteral("requires.minAppVersion"),
                     QStringLiteral("needs Dafin %1 or later; this is %2").arg(minApp, env.appVersion));
    const QStringList fonts = stringList(req.value(QStringLiteral("fonts")));
    for (int i = 0; i < fonts.size(); ++i)
        if (env.fontAvailable && !env.fontAvailable(fonts.at(i)))
            issues.warning(QStringLiteral("missing_dependency"), QStringLiteral("requires.fonts[%1]").arg(i),
                           QStringLiteral("font \"%1\" is not installed here").arg(fonts.at(i)));

    // Style
    const QJsonValue styleValue = o.value(QStringLiteral("style"));
    if (!styleValue.isObject()) {
        issues.error(QStringLiteral("schema"), QStringLiteral("style"),
                     QStringLiteral("an object is required (the text style), got %1").arg(jsonTypeName(styleValue)));
    }
    TextPreset preset = titleTemplateFromJson(o, true);
    if (styleValue.isObject()) {
        checkStyle(styleValue.toObject(), preset.style, env, issues);
        if (env.portable && !preset.style.fontFamily.isEmpty() && !fonts.contains(preset.style.fontFamily))
            issues.warning(QStringLiteral("missing_dependency"), QStringLiteral("requires.fonts"),
                           QStringLiteral("the style uses \"%1\" but requires.fonts does not list it; other machines "
                                          "cannot tell they are missing it")
                               .arg(preset.style.fontFamily));
    }

    // Effects
    const QJsonValue effectsValue = o.value(QStringLiteral("effects"));
    if (o.contains(QStringLiteral("effects")) && !effectsValue.isArray())
        issues.error(QStringLiteral("schema"), QStringLiteral("effects"), QStringLiteral("must be an array"));
    const QJsonArray effects = effectsValue.toArray();
    QSet<QString> keys;
    static const QRegularExpression keyPattern(QStringLiteral("^[a-z][a-z0-9_-]{0,31}$"));
    for (int i = 0; i < effects.size(); ++i) {
        const QString path = QStringLiteral("effects[%1]").arg(i);
        const QJsonObject e = effects.at(i).toObject();
        const QString key = e.value(QStringLiteral("key")).toString();
        if (!keyPattern.match(key).hasMatch())
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".key"),
                         QStringLiteral("\"%1\" is not a valid effect key").arg(key),
                         QStringLiteral("lowercase letters, digits, - and _, starting with a letter, e.g. \"sparks\""));
        else if (keys.contains(key))
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".key"), QStringLiteral("key \"%1\" is used twice").arg(key));
        keys.insert(key);
        const QString effectId = e.contains(QStringLiteral("effect")) ? e.value(QStringLiteral("effect")).toString()
                                                                      : e.value(QStringLiteral("catalogId")).toString();
        if (effectId.isEmpty()) {
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".effect"), QStringLiteral("the effect id is required"));
            continue;
        }
        if (!env.effect)
            continue;
        const std::optional<EffectPresetMeta> meta = env.effect(effectId);
        if (!meta) {
            issues.error(QStringLiteral("missing_dependency"), path + QStringLiteral(".effect"),
                         QStringLiteral("effect \"%1\" is not installed; the template cannot add it").arg(effectId));
            continue;
        }
        const QJsonObject params = e.value(QStringLiteral("parameters")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const EffectParamSpec *spec = nullptr;
            for (const EffectParamSpec &s : meta->parameters)
                if (s.key == it.key())
                    spec = &s;
            if (!spec) {
                QStringList known;
                for (const EffectParamSpec &s : meta->parameters)
                    known.append(s.key);
                issues.error(QStringLiteral("schema"), QStringLiteral("%1.parameters.%2").arg(path, it.key()),
                             QStringLiteral("effect \"%1\" has no parameter \"%2\"").arg(effectId, it.key()),
                             known.join(QStringLiteral(", ")));
                continue;
            }
            if (spec->isColor()) {
                if (!QColor(it.value().toString()).isValid())
                    issues.error(QStringLiteral("schema"), QStringLiteral("%1.parameters.%2").arg(path, it.key()),
                                 QStringLiteral("expected a colour string"), QStringLiteral("\"#rrggbb\""));
            } else if (spec->type == EffectParamType::Float || spec->type == EffectParamType::Int
                       || spec->type == EffectParamType::Enum || spec->type == EffectParamType::Bool) {
                if (!it.value().isDouble() && !it.value().isBool()) {
                    issues.error(QStringLiteral("schema"), QStringLiteral("%1.parameters.%2").arg(path, it.key()),
                                 QStringLiteral("expected a number, got %1").arg(jsonTypeName(it.value())));
                } else if (it.value().isDouble()
                           && (it.value().toDouble() < spec->min - 1e-9 || it.value().toDouble() > spec->max + 1e-9)
                           && spec->type != EffectParamType::Enum && spec->type != EffectParamType::Bool) {
                    issues.error(QStringLiteral("range"), QStringLiteral("%1.parameters.%2").arg(path, it.key()),
                                 QStringLiteral("%1 is out of range").arg(it.value().toDouble()),
                                 QStringLiteral("%1..%2").arg(spec->min).arg(spec->max));
                }
            }
        }
    }
    if (!effects.isEmpty() && preset.style.effectPadding <= 0.0)
        issues.warning(QStringLiteral("render"), QStringLiteral("style.effectPadding"),
                       QStringLiteral("clip effects can only draw inside the text's own raster (the text box plus its "
                                      "strokes, shadows and glows); set effectPadding (px) to give them room around the text"));

    // Controls
    const QJsonValue controlsValue = o.value(QStringLiteral("controls"));
    if (o.contains(QStringLiteral("controls")) && !controlsValue.isArray())
        issues.error(QStringLiteral("schema"), QStringLiteral("controls"), QStringLiteral("must be an array"));
    const QJsonArray controls = controlsValue.toArray();
    QSet<QString> controlIds;
    static const QSet<QString> controlTypes{QStringLiteral("text"),   QStringLiteral("font"), QStringLiteral("color"),
                                            QStringLiteral("scalar"), QStringLiteral("bool"), QStringLiteral("enum")};
    for (int i = 0; i < controls.size(); ++i) {
        const QString path = QStringLiteral("controls[%1]").arg(i);
        const QJsonObject json = controls.at(i).toObject();
        const TemplateControl c = templateControlFromJson(json);
        if (c.id.isEmpty())
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".id"), QStringLiteral("an id is required"));
        else if (controlIds.contains(c.id))
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".id"), QStringLiteral("id \"%1\" is used twice").arg(c.id));
        controlIds.insert(c.id);
        if (c.label.trimmed().isEmpty())
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".label"), QStringLiteral("a label is required"));
        if (!controlTypes.contains(c.type)) {
            issues.error(QStringLiteral("schema"), path + QStringLiteral(".type"), QStringLiteral("\"%1\" is not a control type").arg(c.type),
                         QStringLiteral("text | font | color | scalar | bool | enum"));
            continue;
        }
        QString why;
        QStringList values;
        const QString bound = templateBindingType(c.bind, preset.style, preset.effects, env, &why, &values);
        if (bound.isEmpty()) {
            issues.error(QStringLiteral("binding"), path + QStringLiteral(".bind"), why);
            continue;
        }
        if (bound != c.type) {
            issues.error(QStringLiteral("binding"), path + QStringLiteral(".type"),
                         QStringLiteral("\"%1\" is a %2 property, but the control is a %3").arg(c.bind, bound, c.type),
                         bound);
            continue;
        }
        if (c.type == QLatin1String("scalar")) {
            if (!(c.max > c.min))
                issues.error(QStringLiteral("range"), path, QStringLiteral("max (%1) must be greater than min (%2)").arg(c.max).arg(c.min));
            const QVariant current = readTemplateBinding(c.bind, preset.style, preset.effects);
            if (current.isValid() && (current.toDouble() < c.min - 1e-9 || current.toDouble() > c.max + 1e-9))
                issues.warning(QStringLiteral("range"), path,
                               QStringLiteral("the template's own value %1 is outside the control's range %2..%3")
                                   .arg(current.toDouble())
                                   .arg(c.min)
                                   .arg(c.max));
        }
        if (c.type == QLatin1String("enum")) {
            if (c.options.isEmpty()) {
                issues.error(QStringLiteral("schema"), path + QStringLiteral(".options"),
                             QStringLiteral("an enum control lists its options"), values.join(QStringLiteral(" | ")));
            } else {
                for (int k = 0; k < c.options.size(); ++k)
                    if (!values.isEmpty() && !values.contains(c.options.at(k)))
                        issues.error(QStringLiteral("binding"), QStringLiteral("%1.options[%2]").arg(path).arg(k),
                                     QStringLiteral("\"%1\" is not a value of %2").arg(c.options.at(k), c.bind),
                                     values.join(QStringLiteral(" | ")));
            }
            if (!c.optionLabels.isEmpty() && c.optionLabels.size() != c.options.size())
                issues.error(QStringLiteral("schema"), path + QStringLiteral(".optionLabels"),
                             QStringLiteral("has %1 labels for %2 options").arg(c.optionLabels.size()).arg(c.options.size()));
        }
        if (c.bind == QLatin1String("text") && preset.kind == QLatin1String("caption"))
            issues.warning(QStringLiteral("unsupported"), path,
                           QStringLiteral("caption templates take their text from the subtitle cues; a text control has no effect"));
    }

    if (parsed)
        *parsed = preset;
    return issues.list;
}

bool hasTemplateErrors(const QList<TemplateIssue> &issues)
{
    return std::any_of(issues.cbegin(), issues.cend(),
                       [](const TemplateIssue &i) { return i.severity == TemplateIssue::Severity::Error; });
}

QJsonObject templateIssueToJson(const TemplateIssue &i)
{
    QJsonObject o{{QStringLiteral("severity"), i.severity == TemplateIssue::Severity::Error ? QStringLiteral("error")
                                                                                            : QStringLiteral("warning")},
                  {QStringLiteral("code"), i.code},
                  {QStringLiteral("path"), i.path},
                  {QStringLiteral("message"), i.message}};
    if (!i.expected.isEmpty())
        o.insert(QStringLiteral("expected"), i.expected);
    return o;
}

QString templateIssueToText(const TemplateIssue &i)
{
    QString line = QStringLiteral("%1 [%2] %3: %4")
                       .arg(i.severity == TemplateIssue::Severity::Error ? QStringLiteral("error") : QStringLiteral("warning"),
                            i.code, i.path.isEmpty() ? QStringLiteral("(file)") : i.path, i.message);
    if (!i.expected.isEmpty())
        line += QStringLiteral(" — expected: %1").arg(i.expected);
    return line;
}

// ---------------------------------------------------------------------------------------------
// Catalog

TitleTemplateCatalog &TitleTemplateCatalog::instance()
{
    static TitleTemplateCatalog catalog;
    return catalog;
}

QString TitleTemplateCatalog::installedDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("text-styles"));
}

void TitleTemplateCatalog::loadDir(const QString &dir, const QString &origin) const
{
    const QDir d(dir);
    for (const QFileInfo &info : d.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (!doc.isObject()) {
            m_errors.append(QStringLiteral("%1: not a JSON object (%2)").arg(info.fileName(), parseError.errorString()));
            continue;
        }
        TextPreset preset;
        const QList<TemplateIssue> issues = validateTitleTemplate(doc.object(), TemplateEnvironment{}, &preset);
        if (hasTemplateErrors(issues)) {
            for (const TemplateIssue &issue : issues)
                if (issue.severity == TemplateIssue::Severity::Error)
                    m_errors.append(QStringLiteral("%1: %2").arg(info.fileName(), templateIssueToText(issue)));
            continue;
        }
        preset.origin = origin;
        preset.sourcePath = info.absoluteFilePath();
        // A later root overrides an earlier one with the same id (an installed update of a bundled one).
        const auto same = std::find_if(m_templates.begin(), m_templates.end(),
                                       [&](const TextPreset &p) { return p.id == preset.id; });
        if (same != m_templates.end())
            *same = preset;
        else
            m_templates.append(preset);
    }
}

void TitleTemplateCatalog::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_templates.clear();
    m_errors.clear();
    initTextStyleResources();
    loadDir(QStringLiteral(":/text-styles"), QStringLiteral("builtin"));
    const QString extra = qEnvironmentVariable("DAFIN_TEXT_STYLES");
    for (const QString &dir : extra.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        loadDir(dir, QStringLiteral("installed"));
    if (!installedDir().isEmpty())
        loadDir(installedDir(), QStringLiteral("installed"));
}

QList<TextPreset> TitleTemplateCatalog::templates() const
{
    QMutexLocker lock(&m_mutex);
    ensureLoaded();
    return m_templates;
}

std::optional<TextPreset> TitleTemplateCatalog::templateForId(const QString &id) const
{
    QMutexLocker lock(&m_mutex);
    ensureLoaded();
    for (const TextPreset &p : std::as_const(m_templates))
        if (p.id == id)
            return p;
    return std::nullopt;
}

QStringList TitleTemplateCatalog::loadErrors() const
{
    QMutexLocker lock(&m_mutex);
    ensureLoaded();
    return m_errors;
}

void TitleTemplateCatalog::reload()
{
    QMutexLocker lock(&m_mutex);
    m_loaded = false;
    ensureLoaded();
}

QList<TextPreset> allTextTemplates()
{
    QList<TextPreset> out;
    for (TextPreset p : textPresets()) {
        p.origin = QStringLiteral("pack");
        out.append(p);
    }
    const QList<TextPreset> files = TitleTemplateCatalog::instance().templates();
    // Bundled JSON templates first: they are the readable-fast ones made for Dafin.
    QList<TextPreset> builtin, installed;
    for (const TextPreset &p : files)
        (p.origin == QLatin1String("builtin") ? builtin : installed).append(p);
    return builtin + out + installed;
}

} // namespace drift
