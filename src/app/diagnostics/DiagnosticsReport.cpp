#include "app/diagnostics/DiagnosticsReport.h"

#include <QDir>
#include <QRegularExpression>
#include <QUrlQuery>

// ---- SETTING: the GitHub repository that "Send diagnostics" opens a new issue in. ----
// Default is the Dafin repo. To use a separate reports repo, change this ONE line to "owner/name".
static const char kDiagnosticsRepo[] = "laur23212/Dafin";
// ---------------------------------------------------------------------------------------

namespace dafin::diagnostics {

QString defaultRepo()
{
    return QString::fromLatin1(kDiagnosticsRepo);
}

bool isValidRepo(const QString &repo)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"));
    return re.match(repo).hasMatch();
}

QString scrub(const QString &textIn, const QString &homePath, const QString &userName)
{
    QString text = textIn;
    // Windows profile folders and Unix home folders, whatever the name is.
    static const QRegularExpression winUsers(QStringLiteral(R"((?i)([A-Za-z]:[\\/]+Users[\\/]+)[^\\/\s"':*?<>|]+)"));
    text.replace(winUsers, QStringLiteral("\\1<user>"));
    static const QRegularExpression unixHome(QStringLiteral(R"((/(?:home|Users)/)[^/\s"':]+)"));
    text.replace(unixHome, QStringLiteral("\\1<user>"));
    // The exact home path (any slash style), e.g. a roaming profile outside C:\Users.
    if (!homePath.isEmpty() && homePath.size() > 3) {
        const QString fwd = QDir::fromNativeSeparators(homePath);
        QString back = fwd;
        back.replace(QLatin1Char('/'), QLatin1Char('\\'));
        text.replace(fwd, QStringLiteral("~"), Qt::CaseInsensitive);
        text.replace(back, QStringLiteral("~"), Qt::CaseInsensitive);
    }
    // The user name anywhere else (host names, "user@host"). Whole words only, and at least
    // 3 characters so a short name does not mangle ordinary words.
    if (userName.size() >= 3) {
        const QRegularExpression name(QStringLiteral("(?<![A-Za-z0-9])") + QRegularExpression::escape(userName)
                                          + QStringLiteral("(?![A-Za-z0-9])"),
                                      QRegularExpression::CaseInsensitiveOption);
        text.replace(name, QStringLiteral("<user>"));
    }
    static const QRegularExpression email(QStringLiteral(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"));
    text.replace(email, QStringLiteral("<email>"));
    return text;
}

QString scrubForThisComputer(const QString &text)
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    return scrub(text, QDir::homePath(), user);
}

QString buildReport(const Inputs &in, const QString &homePath, const QString &userName)
{
    QString out;
    out += QStringLiteral("Dafin diagnostics\n=================\n");
    out += QStringLiteral("Dafin version: ") + in.version + QLatin1Char('\n');
    out += QStringLiteral("OS: ") + in.os + QLatin1Char('\n');
    out += QStringLiteral("\n--- System, GPU and driver ---\n") + in.systemText.trimmed() + QLatin1Char('\n');
    out += QStringLiteral("\n--- Last errors and warnings ---\n");
    out += in.errors.isEmpty() ? QStringLiteral("(none this run)\n") : in.errors.join(QLatin1Char('\n')) + QLatin1Char('\n');
    out += QStringLiteral("\n--- Recent log lines ---\n") + in.logTail.trimmed() + QLatin1Char('\n');
    return scrub(out, homePath, userName);
}

namespace {

QUrl issueUrl(const QString &repo, const QString &title, const QString &body)
{
    QUrl url(QStringLiteral("https://github.com/") + repo + QStringLiteral("/issues/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), title);
    q.addQueryItem(QStringLiteral("body"), body);
    q.addQueryItem(QStringLiteral("labels"), QStringLiteral("diagnostics"));
    url.setQuery(q);
    return url;
}

int encodedLength(const QUrl &url)
{
    return url.toEncoded().size();
}

} // namespace

IssueLink makeIssueLink(const QString &repo, const QString &title, const QString &body, int maxUrlLength)
{
    IssueLink link;
    link.url = issueUrl(repo, title, body);
    link.shortBody = body;
    if (encodedLength(link.url) <= maxUrlLength)
        return link;

    link.truncated = true;
    const QString note = QStringLiteral(
        "\n\n[Report cut to fit the link. The FULL report is in the clipboard (paste it here with Ctrl+V) "
        "and saved as a file.]");
    // Binary search for the longest prefix that still fits.
    int lo = 0, hi = int(body.size());
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        QString candidate = body.left(mid);
        // Do not leave half a UTF-16 pair at the cut.
        if (!candidate.isEmpty() && candidate.back().isHighSurrogate())
            candidate.chop(1);
        if (encodedLength(issueUrl(repo, title, candidate + note)) <= maxUrlLength)
            lo = mid;
        else
            hi = mid - 1;
    }
    QString cut = body.left(lo);
    if (!cut.isEmpty() && cut.back().isHighSurrogate())
        cut.chop(1);
    link.shortBody = cut + note;
    link.url = issueUrl(repo, title, link.shortBody);
    return link;
}

} // namespace dafin::diagnostics
