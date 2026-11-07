#pragma once

#include <QString>

// The temporary folder online downloads go to. It is deleted when Dafin quits; a folder left by a
// crash is swept a day later (kept that long so crash recovery can still find its media).
// Separate from DownloadManager so the project save path can ask isSessionFile() without linking
// the whole online asset system.
namespace dafin::assets::session {

QString root();                       // created and locked on first use
bool isSessionFile(const QString &path);
void remove();                        // this process's folder
void sweepStale();                    // folders of processes that are gone, older than a day

} // namespace dafin::assets::session
