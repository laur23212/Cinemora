#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

// Dafin: a timeline read out of another editor's project file (Premiere Pro .prproj, Filmora .wfp)
// before it becomes a Dafin project. The readers fill this from the file alone — Qt and zlib, no
// project model — so they can be checked against real files on their own; toProject() then builds
// the Dafin project from it.
namespace drift::interchange {

struct ImportedClip
{
    QString name;
    QString path;            // the media file as the project names it (absolute, as stored)
    QString relativePath;    // the same, relative to the project file, when the project keeps one
    int64_t startUs = 0;     // on the timeline
    int64_t durationUs = 0;  // on the timeline
    int64_t srcInUs = 0;     // in the source
    double speed = 1.0;
    bool reverse = false;
    bool still = false;      // the source is a still image
    // Filmora: a video clip with no separate audio record keeps the sound of its file; Premiere
    // always has the sound as clips of its own on audio tracks.
    bool keepsOwnAudio = false;
};

struct ImportedTrack
{
    bool audio = false;
    QString name;
    bool muted = false;      // audio: muted; video: hidden (Premiere's eye)
    bool locked = false;
    QList<ImportedClip> clips;
};

struct ImportedTimeline
{
    QString name;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    int sampleRate = 0;
    // Dafin order: index 0 is the top track. Video tracks first (the highest numbered on top), then
    // audio tracks from A1 down.
    QList<ImportedTrack> tracks;
    // What could not come along, for the message the import ends with.
    int skippedClips = 0;
    QStringList skippedKinds;
    QStringList notes;
};

} // namespace drift::interchange
