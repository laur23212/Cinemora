#pragma once

#include <QString>

namespace drift {

// True when the file at path hashes (SHA-256) to expectedHex (hex, either case). False for an
// empty expected hash, a missing or unreadable file. Used right before an installer runs, so a
// package changed in the cache after its download was checked is never installed.
bool fileMatchesSha256(const QString &path, const QString &expectedHex);

// An update is never installed (or the app restarted into it) while an export or render is
// running: the install would end the process and lose the output. The caller waits instead.
bool installAllowed(bool exportRunning);

// How long the install helper waits for the new copy to say it started (the marker file the
// running app writes a few seconds after launch) before it decides the start failed.
constexpr int kStartedOkTimeoutSeconds = 60;

// What an install helper script needs. Every path/value is already quoted for its shell
// (shellSingleQuote for sh, powerShellQuote for PowerShell); pid is the process to wait for.
struct InstallScriptArgs
{
    QString pid;
    QString staged;       // the verified installer / AppImage
    QString target;       // Linux: the AppImage being replaced. Windows: unused
    QString temp;         // Linux: scratch file next to target
    QString previous;     // Linux: file keeping the old AppImage. Windows: folder keeping the old install
    QString installDir;   // Windows: the install folder
    QString exe;          // Windows: the program, for the "is it running" check
    QString marker;       // written by the new copy once it has started
    bool relaunch = false;
};

// sh script: waits for pid to exit, keeps the old AppImage as `previous`, swaps in the new one
// with one rename. When relaunch is set it starts the new copy and, if that copy dies before
// writing `marker`, puts the old one back and starts it.
QString linuxInstallScript(const InstallScriptArgs &args);

// PowerShell script: waits for pid, copies the install folder to `previous`, runs the Inno
// installer (UAC). When relaunch is set (the installer starts the new copy itself) it waits for
// `marker`; if the new copy is gone without writing it, the old folder is copied back (UAC) and
// started.
QString windowsInstallScript(const InstallScriptArgs &args);

} // namespace drift
