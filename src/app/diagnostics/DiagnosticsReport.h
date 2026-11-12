#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

// Dafin: the "Send diagnostics" report. Pure text work (no GUI, no network), so it is testable.
// Nothing here ever holds a GitHub token or any secret: the report goes out through a prefilled
// "new issue" page that the user submits from their own browser, signed in as themselves.

namespace dafin::diagnostics {

struct Inputs
{
    QString version;       // Dafin version
    QString os;            // OS name and version
    QString systemText;    // DebugReport plain text (GPU, driver, codecs...)
    QStringList errors;    // the last warnings/errors of this run
    QString logTail;       // the end of the log file
};

// Removes personal details: the user's home folder, user name, e-mail addresses.
// `homePath` and `userName` are what to hide (empty = skip). Exposed for tests.
QString scrub(const QString &text, const QString &homePath, const QString &userName);
// scrub() with the real home folder and user name of this computer.
QString scrubForThisComputer(const QString &text);

// The whole report, scrubbed, as plain text. Order is chosen so a cut-off keeps the most useful
// part: version, OS, GPU/driver first, then the errors, then the log.
QString buildReport(const Inputs &in, const QString &homePath, const QString &userName);

struct IssueLink
{
    QUrl url;
    QString shortBody;   // what is in the URL
    bool truncated = false;
};

// "https://github.com/<repo>/issues/new?title=...&body=...&labels=diagnostics". If the whole
// thing is longer than maxUrlLength, the body is cut (and says so) until it fits.
IssueLink makeIssueLink(const QString &repo, const QString &title, const QString &body,
                        int maxUrlLength = 7000);

// True for "owner/name" made of letters, digits, '-', '_' and '.'.
bool isValidRepo(const QString &repo);

// The one place the target repo is set.
QString defaultRepo();

} // namespace dafin::diagnostics
