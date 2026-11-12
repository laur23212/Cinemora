#pragma once

// Counts launches that never reach the first frame and, after two in a row (or with
// --safe-mode), starts in safe mode (core/base/SafeMode.h).
namespace drift::startup {

// For main(), after the application and organization names are set and before anything that
// can crash (graphics API choice, windows). Counts this launch, decides, and sets
// drift::safemode. Returns whether this run is in safe mode.
bool begin(int argc, char *argv[]);

// The first frame reached the screen: this launch counts as successful.
void markStarted();

} // namespace drift::startup
