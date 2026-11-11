#pragma once

#include <QByteArray>

namespace drift {

// Dafin: Lottie JSON as the renderer wants it. Skottie refuses a whole file over a missing
// header field ("v", "fr", "op", "w"/"h") that other players default, and over a UTF-8 BOM;
// exporters and generators often leave those out, and some sites wrap the animation in an object
// of their own ({"animationData": {...}}). This strips the BOM, unwraps such a wrapper and fills
// the missing fields; anything else is returned byte for byte, so documents that already worked
// keep their hash and their render. `rewrite` re-serialises through Qt's JSON writer even when
// nothing was missing, for documents Skottie's stricter parser rejects. QtCore only.
QByteArray normalizeLottieJson(const QByteArray &data, bool rewrite = false);

} // namespace drift
