#pragma once

#include "core/interchange/ImportedTimeline.h"
#include "core/project/Project.h"

#include <QString>
#include <QStringList>

namespace drift::interchange {

struct ProjectBuildResult
{
    Project project;
    // Media files the project names that are not on this computer (also not next to the project
    // file under the same name). Their clips are kept and show as missing until relinked.
    QStringList missingMedia;
    int clipCount = 0;
};

// The Dafin project for an imported timeline. `projectFile` is the .prproj / .wfp it came from:
// media that moved is looked for beside it (its relative path, then its name in that folder tree).
//
// Video and audio go on tracks of their own kind in the same order; still images go on a picture
// track directly above the video track they were on (Dafin keeps pictures and video on separate
// tracks). A video clip and the audio clip that came with it are linked, and the video's own sound
// stays off when the audio clip carries it. Clips are laid out to fill the canvas; the app fits
// each to its media's shape once it knows it (see ProjectFileController).
ProjectBuildResult toProject(const ImportedTimeline &timeline, const QString &projectFile);

// One line for the editor's status message: what came across and what did not.
QString summary(const ImportedTimeline &timeline, const ProjectBuildResult &result, const QString &appName);

} // namespace drift::interchange
