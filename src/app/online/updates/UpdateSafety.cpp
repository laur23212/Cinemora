#include "UpdateSafety.h"

#include <QCryptographicHash>
#include <QFile>

namespace drift {

bool fileMatchesSha256(const QString &path, const QString &expectedHex)
{
    const QString expected = expectedHex.trimmed().toLower();
    if (expected.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return false;
    return QString::fromLatin1(hash.result().toHex()) == expected;
}

bool installAllowed(bool exportRunning)
{
    return !exportRunning;
}

namespace {

QString fill(QString text, const InstallScriptArgs &args)
{
    text.replace(QStringLiteral("{PID}"), args.pid);
    text.replace(QStringLiteral("{STAGED}"), args.staged);
    text.replace(QStringLiteral("{TARGET}"), args.target);
    text.replace(QStringLiteral("{TEMP}"), args.temp);
    text.replace(QStringLiteral("{PREVIOUS}"), args.previous);
    text.replace(QStringLiteral("{INSTALLDIR}"), args.installDir);
    text.replace(QStringLiteral("{EXE}"), args.exe);
    text.replace(QStringLiteral("{MARKER}"), args.marker);
    text.replace(QStringLiteral("{TIMEOUT}"), QString::number(kStartedOkTimeoutSeconds));
    return text;
}

} // namespace

QString linuxInstallScript(const InstallScriptArgs &args)
{
    // Copied next to the target first so the final mv is a rename on one filesystem: a launcher
    // that starts the app mid-update gets the old file or the new one, never half of either.
    QString body = QStringLiteral(
            "#!/bin/sh\n"
            "while kill -0 {PID} 2>/dev/null; do\n"
            "  sleep 0.4\n"
            "done\n"
            "if [ -f {TARGET} ]; then cp -f {TARGET} {PREVIOUS} || rm -f {PREVIOUS}; fi\n"
            "if ! { cp -f {STAGED} {TEMP} && chmod 755 {TEMP} && mv -f {TEMP} {TARGET}; }; then\n"
            "  rm -f {TEMP}\n"
            "  exit 1\n"
            "fi\n"
            "rm -f {STAGED}\n");
    if (args.relaunch) {
        body += QStringLiteral(
                "rm -f {MARKER}\n"
                "nohup {TARGET} >/dev/null 2>&1 &\n"
                "newpid=$!\n"
                "waited=0\n"
                "while [ \"$waited\" -lt {TIMEOUT} ]; do\n"
                "  [ -f {MARKER} ] && exit 0\n"
                "  kill -0 \"$newpid\" 2>/dev/null || break\n"
                "  sleep 1\n"
                "  waited=$((waited + 1))\n"
                "done\n"
                "[ -f {MARKER} ] && exit 0\n"
                "# Still running but slow to say so: leave it. Only a copy that died is rolled back.\n"
                "if kill -0 \"$newpid\" 2>/dev/null; then exit 0; fi\n"
                "if [ -f {PREVIOUS} ]; then\n"
                "  if cp -f {PREVIOUS} {TEMP} && chmod 755 {TEMP} && mv -f {TEMP} {TARGET}; then\n"
                "    nohup {TARGET} >/dev/null 2>&1 &\n"
                "  else\n"
                "    rm -f {TEMP}\n"
                "  fi\n"
                "fi\n");
    }
    return fill(body, args);
}

QString windowsInstallScript(const InstallScriptArgs &args)
{
    // /DRIFTUPDATE=1 is what makes a silent install launch the app again, so the relaunch case
    // passes it and an install on close leaves the app closed. -Verb RunAs is the UAC prompt.
    QString body = QStringLiteral(
            "$processId = {PID}\r\n"
            "$installer = {STAGED}\r\n"
            "$installDir = {INSTALLDIR}\r\n"
            "$previous = {PREVIOUS}\r\n"
            "$exe = {EXE}\r\n"
            "$marker = {MARKER}\r\n"
            "$procName = [System.IO.Path]::GetFileNameWithoutExtension($exe)\r\n"
            "while (Get-Process -Id $processId -ErrorAction SilentlyContinue) {\r\n"
            "    Start-Sleep -Milliseconds 400\r\n"
            "}\r\n"
            "# Keep the version being replaced, so a copy that will not start can be put back.\r\n"
            "try {\r\n"
            "    & robocopy.exe $installDir $previous /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null\r\n"
            "} catch {}\r\n"
            "Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue\r\n"
            "try {\r\n"
            "    Start-Process -FilePath $installer -ArgumentList "
            "'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'");
    if (args.relaunch)
        body += QStringLiteral(",'/DRIFTUPDATE=1'");
    body += QStringLiteral(
            " -Verb RunAs -Wait\r\n"
            "} catch {\r\n"
            "    exit 1\r\n"
            "}\r\n");
    if (args.relaunch) {
        body += QStringLiteral(
                "$seen = $false\r\n"
                "for ($i = 0; $i -lt {TIMEOUT}; $i++) {\r\n"
                "    if (Test-Path -LiteralPath $marker) { exit 0 }\r\n"
                "    if (Get-Process -Name $procName -ErrorAction SilentlyContinue) { $seen = $true }\r\n"
                "    elseif ($seen) { break }\r\n"
                "    Start-Sleep -Seconds 1\r\n"
                "}\r\n"
                "if (Test-Path -LiteralPath $marker) { exit 0 }\r\n"
                "# Still running but slow to say so: leave it. Only a copy that is gone is rolled back.\r\n"
                "if (Get-Process -Name $procName -ErrorAction SilentlyContinue) { exit 0 }\r\n"
                "if (Test-Path -LiteralPath $previous) {\r\n"
                "    try {\r\n"
                "        $copyArgs = '\"' + $previous + '\" \"' + $installDir + '\" /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP'\r\n"
                "        Start-Process -FilePath robocopy.exe -ArgumentList $copyArgs -Verb RunAs -Wait -WindowStyle Hidden\r\n"
                "        Start-Process -FilePath $exe\r\n"
                "    } catch {}\r\n"
                "}\r\n");
    }
    return fill(body, args);
}

} // namespace drift
