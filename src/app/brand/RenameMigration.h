#pragma once

namespace dafin {

// The app was called Cadra before it was called Dafin. Its settings, sign-ins, downloaded
// addons and asset library live under "Cadra" paths; this moves them to the "Dafin" ones once,
// the first time Dafin starts. Call it right after the application and organization names are
// set and before anything reads QSettings or the data folders. Safe to call every start.
void migrateFromCadra();

} // namespace dafin
