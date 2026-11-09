#pragma once

namespace dafin {

// Windows: writes a report with the call stack to %LOCALAPPDATA%\Dafin\crash when Dafin crashes,
// and keeps this run's Qt messages in dafin-log.txt there (CrashReport_win.cpp). Elsewhere a no-op.
#ifdef _WIN32
void installCrashReport();
#else
inline void installCrashReport() {}
#endif

} // namespace dafin
