#pragma once

// Cheap process working-set query for muffin.perf probes, so each parse/layout phase can log its
// memory footprint alongside timing. No allocation; safe to call from perf destructors.
// Returns bytes, or 0 if unavailable on the platform.

#include <QtGlobal>  // Q_OS_WIN, qint64

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#  pragma comment(lib, "psapi.lib")
#else
#  include <sys/resource.h>
#endif

namespace muffin::diag {

inline qint64 workingSetBytes() {
#if defined(Q_OS_WIN)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return static_cast<qint64>(pmc.WorkingSetSize);
  }
  return 0;
#else
  // NOTE: getrusage(RUSAGE_SELF).ru_maxrss is the HIGH-WATER-MARK resident set (monotonic — it never
  // decreases), NOT the current working set. So on Linux/macOS the per-phase "ws=" deltas printed by
  // the perf timers show the running peak, not the footprint of the phase that just ran. Only the
  // Windows branch above (pmc.WorkingSetSize) is a true point-in-time working set; treat Linux/macOS
  // numbers as a ceiling, not a phase delta. Fine for this Windows-first project — documented so the
  // cross-platform numbers aren't misread.
  rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
#  if defined(Q_OS_DARWIN)
    return static_cast<qint64>(ru.ru_maxrss);        // bytes on macOS
#  else
    return static_cast<qint64>(ru.ru_maxrss) * 1024; // KB -> bytes on Linux
#  endif
  }
  return 0;
#endif
}

}  // namespace muffin::diag
