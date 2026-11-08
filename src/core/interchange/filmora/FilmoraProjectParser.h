#pragma once

#include "core/interchange/ImportedTimeline.h"

#include <QString>

#include <optional>

// Dafin: reads the edit out of a Wondershare Filmora project (.wfp).
//
// A .wfp is a zip of JSON (Filmora 9 and later; the layout below is Filmora 15's and 16's):
//
//   ProjectFolder/project_info.json            timeline_mediaId, name, frame rate, resolution
//   ProjectFolder/Medias/<id>/timeline.wesproj  the timeline:
//     timelineInfos[0]  frameRate {num, den}, resolutionWidth/Height, sampleRate
//       trackInfos[]    trackType (1 video, 2 audio), trackTag (its number), clipList[]
//         clip          type, filename (a file: URI), tlBegin/tlEnd (timeline), inPoint/outPoint
//                       (source), speed {reverse, speedParam}, in 100 ns units
//
// There is no published specification; this follows what WFP Compiler (MIT,
// github.com/ItsQuesty/WFP-Renderer-bypass) and CutConvert worked out from saved projects. Clips
// with a media file come across with their timing, speed and direction. Titles, effects,
// transitions, stickers and Filmora's AI tools are Filmora's own and are skipped and counted.
namespace drift::interchange::filmora {

std::optional<ImportedTimeline> parse(const QString &wfpPath, QString *error = nullptr);

// Local path from what Filmora writes ("file:/C:/Users/…", "file:///home/…", percent-encoded).
QString localPathFromFilmora(const QString &uriOrPath);

} // namespace drift::interchange::filmora
