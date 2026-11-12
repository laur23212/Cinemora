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

} // namespace drift
