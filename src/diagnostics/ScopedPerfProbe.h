#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMessageLogger>
#include <QtGlobal>

namespace muffin::diag {

// RAII perf probe: on construction, if `category` has debug enabled, starts a timer; on
// destruction logs "<label> <elapsed-ms> ms" to that category at debug level. When the
// category is off (the default — perf categories are gated to QtWarningMsg), the dtor is a
// single disabled branch and the timer never starts, so the probe is ~zero-cost in
// production builds.
//
// Replaces the byte-identical PerfTimer / UndoPerfTimer copies that lived in
// InputController / EditorView / EditorController / EditorControllerUndo /
// InputControllerEdit and differed only in which QLoggingCategory they targeted. Each file
// now keeps a one-line subclass that binds its category, e.g.:
//   struct PerfTimer : diag::ScopedPerfProbe {
//     explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, inputPerf()) {}
//   };
// (DocumentSession keeps its own PerfTimer — it logs an extra working-set-memory suffix
// that this shared class does not model.)
class ScopedPerfProbe {
public:
  ScopedPerfProbe(const char* label, const QLoggingCategory& category)
      : label_(label),
        categoryName_(category.categoryName()),
        enabled_(category.isDebugEnabled()) {
    if (enabled_) { timer_.start(); }
  }

  ~ScopedPerfProbe() {
    if (enabled_) {
      QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC, categoryName_)
          .debug().nospace() << label_ << ' ' << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  const char* categoryName_;
  bool enabled_;
  QElapsedTimer timer_;
};

}  // namespace muffin::diag
