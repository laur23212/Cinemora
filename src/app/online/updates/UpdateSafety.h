#pragma once

#include <QString>

namespace drift {

// True when the file at path hashes (SHA-256) to expectedHex (hex, either case). False for an
// empty expected hash, a missing or unreadable file. Used right before an installer runs, so a
// package changed in the cache after its download was checked is never installed.
bool fileMatchesSha256(const QString &path, const QString &expectedHex);

} // namespace drift
