#pragma once

namespace drift::platform {

// True when the user asked the operating system for less animation: Windows "Animation effects"
// off (Settings > Accessibility > Visual effects), GNOME "enable-animations" off. The
// environment variable DAFIN_REDUCE_MOTION=1 (or 0) overrides the system either way. Cheap
// enough to call once at start; the answer is not watched afterwards.
bool systemPrefersReducedMotion();

} // namespace drift::platform
