#pragma once

#include <QString>

#include <QtGlobal>

namespace muffin::diag {

// File sink for the "muffin.perf" logging family (muffin.perf, muffin.perf.detail,
// muffin.perf.paint, muffin.perf.css), enabled by MUFFIN_PERF_LOG in main().
//
// Only one Qt message handler can be installed at a time, so this one owns every
// message. Messages outside the family are handed on: to the handler that was
// installed before it, or to Qt's default destination when there was none. Turning
// on the performance log therefore never silences warnings and errors.
class PerfFileLogger {
public:
  // Appends the muffin.perf family to `filePath`. A path that cannot be opened
  // leaves the current handler untouched.
  static void install(const QString& filePath);

  // Restores the handler that was active before install() and closes the file.
  // No-op when the logger is not installed.
  static void uninstall();

  static bool isInstalled();
};

} // namespace muffin::diag
