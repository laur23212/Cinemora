#include "app/online/library/AssetProvider.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

namespace cadra::assets {

// ─── AssetTypes helpers ──────────────────────────────────────────────────────────────────────

QString kindToString(AssetKind kind)
{
    switch (kind) {
    case AssetKind::Video: return QStringLiteral("video");
    case AssetKind::Music: return QStringLiteral("music");
    case AssetKind::SoundEffect: return QStringLiteral("sfx");
    case AssetKind::Sticker: return QStringLiteral("sticker");
    case AssetKind::Title: return QStringLiteral("title");
    }
    return QStringLiteral("video");
}

AssetKind kindFromString(const QString &text, bool *ok)
{
    static const QMap<QString, AssetKind> map{
        {QStringLiteral("video"), AssetKind::Video},     {QStringLiteral("music"), AssetKind::Music},
        {QStringLiteral("sfx"), AssetKind::SoundEffect}, {QStringLiteral("audio"), AssetKind::SoundEffect},
        {QStringLiteral("sticker"), AssetKind::Sticker}, {QStringLiteral("lottie"), AssetKind::Sticker},
        {QStringLiteral("title"), AssetKind::Title},
    };
    const auto it = map.constFind(text.trimmed().toLower());
    if (ok)
        *ok = it != map.constEnd();
    return it != map.constEnd() ? *it : AssetKind::Video;
}

QList<AssetKind> kindsFromStrings(const QStringList &list)
{
    QList<AssetKind> out;
    for (const QString &s : list) {
        bool ok = false;
        const AssetKind k = kindFromString(s, &ok);
        if (ok && !out.contains(k))
            out.append(k);
    }
    return out;
}

QString connectionStateToString(ConnectionState state)
{
    switch (state) {
    case ConnectionState::NotRequired: return QStringLiteral("notRequired");
    case ConnectionState::NotConnected: return QStringLiteral("notConnected");
    case ConnectionState::Connected: return QStringLiteral("connected");
    case ConnectionState::Expired: return QStringLiteral("expired");
    case ConnectionState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QVariantMap AssetItem::toVariant() const
{
    return {
        {QStringLiteral("key"), key()},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), kindToString(kind)},
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("authorUrl"), authorUrl.toString()},
        {QStringLiteral("pageUrl"), pageUrl.toString()},
        {QStringLiteral("thumbnailUrl"), thumbnailUrl.toString()},
        {QStringLiteral("previewUrl"), previewUrl.toString()},
        {QStringLiteral("downloadUrl"), downloadUrl.toString()},
        {QStringLiteral("license"), license},
        {QStringLiteral("licenseUrl"), licenseUrl.toString()},
        {QStringLiteral("attribution"), attribution},
        {QStringLiteral("duration"), durationSeconds},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("downloadable"), downloadable},
        {QStringLiteral("downloadBlockedReason"), downloadBlockedReason},
        {QStringLiteral("extra"), extra},
    };
}

AssetItem AssetItem::fromVariant(const QVariantMap &m)
{
    AssetItem a;
    a.providerId = m.value(QStringLiteral("providerId")).toString();
    a.id = m.value(QStringLiteral("id")).toString();
    a.kind = kindFromString(m.value(QStringLiteral("kind")).toString());
    a.title = m.value(QStringLiteral("title")).toString();
    a.author = m.value(QStringLiteral("author")).toString();
    a.authorUrl = QUrl(m.value(QStringLiteral("authorUrl")).toString());
    a.pageUrl = QUrl(m.value(QStringLiteral("pageUrl")).toString());
    a.thumbnailUrl = QUrl(m.value(QStringLiteral("thumbnailUrl")).toString());
    a.previewUrl = QUrl(m.value(QStringLiteral("previewUrl")).toString());
    a.downloadUrl = QUrl(m.value(QStringLiteral("downloadUrl")).toString());
    a.license = m.value(QStringLiteral("license")).toString();
    a.licenseUrl = QUrl(m.value(QStringLiteral("licenseUrl")).toString());
    a.attribution = m.value(QStringLiteral("attribution")).toString();
    a.durationSeconds = m.value(QStringLiteral("duration")).toDouble();
    a.width = m.value(QStringLiteral("width")).toInt();
    a.height = m.value(QStringLiteral("height")).toInt();
    a.downloadable = m.value(QStringLiteral("downloadable"), true).toBool();
    a.downloadBlockedReason = m.value(QStringLiteral("downloadBlockedReason")).toString();
    a.extra = m.value(QStringLiteral("extra")).toMap();
    return a;
}

ProviderError ProviderError::make(Code code, QString message, int httpStatus, int retryAfter)
{
    ProviderError e;
    e.code = code;
    e.message = std::move(message);
    e.httpStatus = httpStatus;
    e.retryAfterSeconds = retryAfter;
    return e;
}

QString ProviderError::codeName() const
{
    switch (code) {
    case Code::None: return QStringLiteral("none");
    case Code::Offline: return QStringLiteral("offline");
    case Code::Timeout: return QStringLiteral("timeout");
    case Code::RateLimited: return QStringLiteral("rateLimited");
    case Code::AuthRequired: return QStringLiteral("authRequired");
    case Code::AuthExpired: return QStringLiteral("authExpired");
    case Code::Forbidden: return QStringLiteral("forbidden");
    case Code::NotFound: return QStringLiteral("notFound");
    case Code::ServiceUnavailable: return QStringLiteral("serviceUnavailable");
    case Code::BadResponse: return QStringLiteral("badResponse");
    case Code::Cancelled: return QStringLiteral("cancelled");
    case Code::Storage: return QStringLiteral("storage");
    case Code::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QVariantMap ProviderError::toVariant() const
{
    return {{QStringLiteral("code"), codeName()},
            {QStringLiteral("message"), message},
            {QStringLiteral("httpStatus"), httpStatus},
            {QStringLiteral("retryAfter"), retryAfterSeconds}};
}

// ─── RequestHandle ───────────────────────────────────────────────────────────────────────────

void RequestHandle::attach(QNetworkReply *reply)
{
    m_reply = reply;
}

void RequestHandle::cancel()
{
    *m_cancelled = true;
    if (m_reply)
        m_reply->abort();
}

// ─── AssetProvider defaults ──────────────────────────────────────────────────────────────────

QNetworkRequest AssetProvider::makeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", m_context->userAgent());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    return request;
}

RequestHandle AssetProvider::authenticate(const QVariantMap &input, AuthCallback callback)
{
    // Store what was typed, then prove it works with a one-item search.
    Credentials creds;
    for (const AuthField &f : info().fields) {
        const QString v = input.value(f.id).toString().trimmed();
        if (v.isEmpty() && !f.optional) {
            callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                             QObject::tr("%1 is required.").arg(f.label)));
            return {};
        }
        if (!v.isEmpty())
            creds.insert(f.id, v);
    }
    const QString id = info().id;
    const Credentials previous = m_context->credentials(id);
    m_context->updateCredentials(id, creds); // search() reads credentials from the context
    SearchRequest probe;
    probe.kind = info().kinds.value(0, AssetKind::Video);
    probe.pageSize = 3;
    return search(probe, [this, id, creds, previous, callback](const SearchPage &, const ProviderError &error) {
        if (!error.ok() && (error.code == ProviderError::Code::AuthExpired
                            || error.code == ProviderError::Code::AuthRequired)) {
            m_context->updateCredentials(id, previous);
            callback({}, ProviderError::make(ProviderError::Code::AuthExpired,
                                             QObject::tr("%1 did not accept these details.").arg(info().name),
                                             error.httpStatus));
            return;
        }
        // Offline or rate-limited: the key may be fine; keep it and say so.
        callback(creds, error.ok() ? ProviderError::none() : error);
    });
}

RequestHandle AssetProvider::getAssetDetails(const AssetItem &item, DetailsCallback callback)
{
    callback(item, ProviderError::none());
    return {};
}

RequestHandle AssetProvider::download(const AssetItem &item, DownloadSpecCallback callback)
{
    if (!item.downloadable) {
        callback({}, ProviderError::make(ProviderError::Code::Forbidden,
                                         item.downloadBlockedReason.isEmpty()
                                             ? QObject::tr("The author does not allow downloading this item.")
                                             : item.downloadBlockedReason));
        return {};
    }
    const QUrl url = item.downloadUrl.isValid() ? item.downloadUrl : item.previewUrl;
    if (!url.isValid()) {
        callback({}, ProviderError::make(ProviderError::Code::NotFound, QObject::tr("This item has no downloadable file.")));
        return {};
    }
    DownloadSpec spec;
    spec.request = makeRequest(url);
    spec.request.setRawHeader("Accept", "*/*");
    spec.request.setTransferTimeout(0); // large files: the download manager watches for stalls
    spec.suggestedFileName = util::safeFileName(item.title) + QStringLiteral(" [") + util::safeFileName(item.id)
                           + QStringLiteral("].") + util::fileExtensionFor(url, item.kind);
    callback(spec, ProviderError::none());
    return {};
}

QNetworkRequest AssetProvider::getThumbnail(const AssetItem &item) const
{
    QNetworkRequest request = makeRequest(item.thumbnailUrl);
    request.setRawHeader("Accept", "image/*");
    return request;
}

ProviderError AssetProvider::errorFor(QNetworkReply *reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString name = info().name;
    using C = ProviderError::Code;
    if (status == 401 || status == 403) {
        const bool haveCreds = !credentials().isEmpty();
        ProviderError e = ProviderError::make(haveCreds ? C::AuthExpired : C::AuthRequired,
                                              haveCreds ? QObject::tr("%1 rejected your saved sign-in. Reconnect it in Settings.").arg(name)
                                                        : QObject::tr("%1 needs a free account. Connect it in Settings.").arg(name),
                                              status);
        if (haveCreds)
            m_context->reportAuthFailure(info().id, e);
        return e;
    }
    if (status == 429) {
        const int wait = util::retryAfterSeconds(reply);
        return ProviderError::make(C::RateLimited,
                                   wait > 0 ? QObject::tr("%1 asked us to slow down. Try again in %2.")
                                                  .arg(name, wait >= 120 ? QObject::tr("%1 minutes").arg(wait / 60)
                                                                         : QObject::tr("%1 seconds").arg(wait))
                                            : QObject::tr("%1 asked us to slow down. Try again in a minute.").arg(name),
                                   status, wait > 0 ? wait : 60);
    }
    if (status == 400) {
        // Pixabay and others answer a bad key with 400 + a plain-text reason.
        const QByteArray body = reply->peek(512).toLower();
        if (body.contains("api key") || body.contains("apikey") || body.contains("client_id")
            || body.contains("token")) {
            const bool haveCreds = !credentials().isEmpty();
            ProviderError e = ProviderError::make(haveCreds ? C::AuthExpired : C::AuthRequired,
                                                  QObject::tr("%1 did not accept the saved key. Check it in Settings.").arg(name),
                                                  status);
            if (haveCreds)
                m_context->reportAuthFailure(info().id, e);
            return e;
        }
    }
    if (status == 404)
        return ProviderError::make(C::NotFound, QObject::tr("%1 no longer has this item.").arg(name), status);
    if (status >= 500)
        return ProviderError::make(C::ServiceUnavailable, QObject::tr("%1 is having problems right now. Try again later.").arg(name), status);

    switch (reply->error()) {
    case QNetworkReply::OperationCanceledError:
        return ProviderError::make(C::Cancelled, QObject::tr("Cancelled"));
    case QNetworkReply::TimeoutError:
        return ProviderError::make(C::Timeout, QObject::tr("%1 took too long to answer.").arg(name));
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
        return ProviderError::make(C::Offline, QObject::tr("Can't reach %1. Check your internet connection.").arg(name));
    default:
        break;
    }
    if (status >= 400)
        return ProviderError::make(C::Unknown, QObject::tr("%1 returned an error (%2).").arg(name).arg(status), status);
    return ProviderError::make(C::Unknown, reply->errorString());
}

RequestHandle AssetProvider::getJson(const QNetworkRequest &request,
                                     std::function<void(const QJsonDocument &, const ProviderError &)> done)
{
    RequestHandle handle;
    QNetworkReply *reply = m_context->network()->get(request);
    handle.attach(reply);
    const std::shared_ptr<bool> cancelled = handle.flag();
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, cancelled, done]() {
        reply->deleteLater();
        if (*cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            done({}, errorFor(reply));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            done({}, ProviderError::make(ProviderError::Code::BadResponse,
                                         QObject::tr("%1 sent a response Cadra could not read.").arg(info().name)));
            return;
        }
        done(doc, ProviderError::none());
    });
    return handle;
}

// ─── util ────────────────────────────────────────────────────────────────────────────────────

namespace util {

QString prettyTitle(QString title)
{
    if (title.startsWith(QLatin1String("http"))) {
        const QStringList parts = QUrl(title).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        title = parts.isEmpty() ? QString() : parts.last();
        title.remove(QRegularExpression(QStringLiteral("-?\\d+$")));
        title.replace(QLatin1Char('-'), QLatin1Char(' '));
    }
    if (title.count(QLatin1Char(',')) >= 2)
        title = title.section(QLatin1Char(','), 0, 2);
    title = title.simplified();
    if (!title.isEmpty())
        title[0] = title.at(0).toUpper();
    return title;
}

QString fileExtensionFor(const QUrl &url, AssetKind kind)
{
    static const QStringList known = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("webm"),
                                      QStringLiteral("mkv"), QStringLiteral("m4v"), QStringLiteral("mp3"),
                                      QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("flac"),
                                      QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("aif"),
                                      QStringLiteral("aiff"), QStringLiteral("json"), QStringLiteral("lottie"),
                                      QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("webp")};
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (known.contains(suffix))
        return suffix;
    switch (kind) {
    case AssetKind::Video: return QStringLiteral("mp4");
    case AssetKind::Music:
    case AssetKind::SoundEffect: return QStringLiteral("mp3");
    case AssetKind::Sticker:
    case AssetKind::Title: return QStringLiteral("json");
    }
    return QStringLiteral("bin");
}

QString safeFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]+")), QStringLiteral("_"));
    name.replace(QRegularExpression(QStringLiteral("\\.{2,}")), QStringLiteral("."));
    name.replace(QRegularExpression(QStringLiteral("_[._]+")), QStringLiteral("_"));
    name = name.simplified();
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    if (name.size() > 80)
        name = name.left(80).trimmed();
    return name.isEmpty() ? QStringLiteral("asset") : name;
}

int retryAfterSeconds(QNetworkReply *reply)
{
    const QByteArray retry = reply->rawHeader("Retry-After");
    if (!retry.isEmpty()) {
        bool ok = false;
        const int s = retry.toInt(&ok);
        if (ok)
            return qBound(1, s, 24 * 3600);
        const QDateTime when = QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date);
        if (when.isValid())
            return qBound(1, int(QDateTime::currentDateTimeUtc().secsTo(when)), 24 * 3600);
    }
    // Pixabay: X-RateLimit-Reset = seconds until the window resets.
    const QByteArray reset = reply->rawHeader("X-RateLimit-Reset");
    if (!reset.isEmpty()) {
        const qint64 v = reset.toLongLong();
        constexpr qint64 kDay = 24 * 3600;
        if (v > 1000000000) // Pexels-style UNIX timestamp
            return int(qBound<qint64>(1, v - QDateTime::currentSecsSinceEpoch(), kDay));
        if (v > 0)
            return int(qBound<qint64>(1, v, kDay));
    }
    return 0;
}

} // namespace util
} // namespace cadra::assets
