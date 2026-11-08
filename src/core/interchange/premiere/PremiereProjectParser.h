#pragma once

#include "core/interchange/ImportedTimeline.h"

#include <QByteArray>
#include <QString>

#include <optional>

// Dafin: reads the edit out of an Adobe Premiere Pro project (.prproj).
//
// A .prproj is gzip-compressed XML: a flat table of objects under <PremiereData>, each with an
// ObjectID or ObjectUID, pointing at one another with ObjectRef / ObjectURef. The path from a
// sequence to a media file, as Premiere writes it:
//
//   Sequence/TrackGroups/TrackGroup/Second → VideoTrackGroup | AudioTrackGroup
//     TrackGroup/Tracks/Track → VideoClipTrack | AudioClipTrack
//       ClipTrack/ClipItems/TrackItems/TrackItem → VideoClipTrackItem | AudioClipTrackItem
//         ClipTrackItem/TrackItem/Start, End           (timeline, 254016000000 ticks a second)
//         ClipTrackItem/SubClip → SubClip (Name)
//           Clip → VideoClip | AudioClip
//             Clip/InPoint, OutPoint, PlaybackSpeed, PlayBackwards   (source)
//             Clip/Source → VideoMediaSource | AudioMediaSource
//               MediaSource/Media → Media (ActualMediaFilePath, FilePath, RelativePath)
//             or Clip/Source → VideoSequenceSource | AudioSequenceSource (a nested sequence)
//
// This follows the structure py-premiere (MIT, github.com/Pouf/py-premiere) documents and was
// checked against the projects Premiere saved for its test suite. The open sequence is the one
// imported (the last of the project's OpenSequenceGuidList); nested sequences are flattened into
// it. Titles, graphics and generated media (colour mattes, bars) have no file and are skipped and
// counted, as are effects, transitions and keyframes.
namespace drift::interchange::premiere {

bool looksLikePremiereXml(const QByteArray &head);

// Gzip or plain XML bytes in. `sequenceName` picks a sequence by name instead of the open one.
std::optional<ImportedTimeline> parse(const QByteArray &data, QString *error = nullptr,
                                      const QString &sequenceName = {});

// Inflates a gzip stream (or returns plain data unchanged); empty on a corrupt stream.
QByteArray gunzip(const QByteArray &data);

} // namespace drift::interchange::premiere
