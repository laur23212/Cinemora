// Dafin: hover preview of a library preset. Hovering an effect (or a saved effect stack) in the
// library shows it in the preview window on the clip under the playhead, without applying it:
// the effects go into the compositor's render snapshot only (CompositorService::setPreviewTrial),
// so nothing here is an edit, nothing is undoable, and the project is never dirtied.
//
// Only video effects that need nothing but the picture are tried; face, depth and mask effects
// need data the clip may not have yet, so they show no trial rather than a wrong one.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/effects/EffectStackStore.h"
#include "engine/packages/effects/EffectCatalog.h"

using namespace drift::appdetail;
using namespace drift::appcontroller_detail;

namespace {

bool trialableDef(const EffectPresetEntry *def)
{
    return def && !(def->needsFace || def->needsDepth || def->needsMask || def->isFaceSwap
                    || def->isModel3d);
}

drift::Effect trialEffectFromDef(const EffectPresetEntry &def, const QMap<QString, QVariant> &overrides)
{
    drift::Effect effect;
    effect.name = def.filterName;
    effect.catalogId = def.meta.id;
    for (auto it = def.fixedParams.constBegin(); it != def.fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def.meta.parameters) {
        const auto found = overrides.constFind(p.key);
        if (found != overrides.constEnd())
            effect.parameters.insert(p.key, found.value());
        else
            p.insertDefault(effect.parameters);
    }
    return effect;
}

} // namespace

QString AppController::presetPreviewTargetClipId() const
{
    const drift::TimeUs playhead = m_playback.playheadUs();
    const auto usable = [&](const drift::Track &track, const drift::Clip &clip) {
        return track.type != drift::TrackType::Audio && clip.type != drift::ClipType::Audio
               && clip.type != drift::ClipType::Adjustment && playhead >= clip.timelineStart
               && playhead < clip.timelineEnd();
    };
    // The selected clip wins when the playhead is over it; otherwise the topmost picture there.
    if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        const drift::Track &track = m_project.tracks().at(m_selectedTrack);
        const drift::Clip &clip = track.clips.at(m_selectedClip);
        if (usable(track, clip))
            return clip.id;
    }
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (usable(track, clip))
                return clip.id;
        }
    }
    return {};
}

bool AppController::previewPresetOnPlayhead(const QString &kind, const QString &payload)
{
    if (m_playback.isPlaying() || payload.isEmpty()) {
        clearPresetPreview();
        return false;
    }

    QList<drift::Effect> trial;
    if (kind == QLatin1String("effect")) {
        const EffectPresetEntry *def = effectDefForId(payload);
        if (trialableDef(def))
            trial.append(trialEffectFromDef(*def, {}));
    } else if (kind == QLatin1String("effectStack")) {
        const std::optional<drift::EffectStackPreset> preset =
            drift::EffectStackStore::instance().presetForId(payload);
        if (preset) {
            for (const drift::Effect &incoming : preset->effects) {
                const EffectPresetEntry *def = effectDefForId(incoming.catalogId);
                if (!trialableDef(def))
                    continue;
                drift::Effect effect = trialEffectFromDef(*def, incoming.parameters);
                effect.paramKeyframes = incoming.paramKeyframes;
                effect.enabled = incoming.enabled;
                trial.append(effect);
            }
        }
    }

    const QString clipId = trial.isEmpty() ? QString() : presetPreviewTargetClipId();
    if (clipId.isEmpty()) {
        clearPresetPreview();
        return false;
    }
    m_playback.setPreviewTrialEffects(clipId, trial);
    m_presetPreviewActive = true;
    return true;
}

void AppController::clearPresetPreview()
{
    if (!m_presetPreviewActive)
        return;
    m_presetPreviewActive = false;
    m_playback.setPreviewTrialEffects(QString(), {});
}
