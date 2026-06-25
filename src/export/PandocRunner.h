#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QWidget;

namespace muffin {

// Outcome of a Pandoc invocation. `ran` distinguishes "the process executed and
// returned" from "we never managed to start it" (e.g. Pandoc not installed) —
// callers report those two cases differently to the user.
struct PandocResult {
  bool ran = false;
  bool canceled = false;
  int exitCode = -1;
  QByteArray out;  // stdout
  QByteArray err;  // stderr
};

// Thin wrapper around a QProcess that drives the external Pandoc executable.
// The codebase has no other external-process usage, so this is the canonical
// helper for it: it resolves the executable, runs synchronously while keeping a
// modal progress dialog responsive (and cancellable), and returns the captured
// output. It is tr()-free on purpose — callers own all user-facing messages so
// this translation unit stays out of MUFFIN_TRANSLATABLE_SOURCES and may use a
// `namespace muffin { }` wrapper freely (see CLAUDE.md).
class PandocRunner {
public:
  // export/pandocPath if it is set and points to an executable; otherwise the
  // result of searchSystem() (well-known install locations per platform); if
  // that also comes up empty, the bare "pandoc" and QProcess resolves it via
  // PATH at run time.
  static QString resolvedExecutable();

  // Returns the first entry in `candidates` that points to an executable file,
  // or an empty string if none do. Pure (filesystem reads only) so it is
  // unit-testable; searchSystem() builds the candidate list and calls this.
  static QString findFirstExistingExecutable(const QStringList& candidates);

  // Probes well-known per-platform Pandoc install locations (the per-user
  // Windows install, Program Files, Homebrew/MacPorts/TeX paths on macOS, the
  // usual Unix bins, etc.) and returns the first existing executable, or an
  // empty string if none are found. This catches the common case where Pandoc
  // is installed but not on the current process's PATH.
  static QString searchSystem();

  // Runs `where pandoc` (Windows) / `which pandoc` (Unix) and returns the first
  // PATH-resolved executable, or empty. Used only to report status to the user
  // (searchSystem()/resolvedExecutable() already cover execution); synchronous,
  // so call only from the UI thread at user-action time, not on a hot path.
  static QString resolveOnPath();

  // Probes whether Pandoc can actually run (`<exe> --version`), with short
  // timeouts. Synchronous, so call only at user-action time, not from an
  // enabled predicate. Returns false when Pandoc is absent or misconfigured.
  static bool isAvailable();

  // Runs Pandoc with `args` (no leading program name). When `stdinData` is
  // non-null it is written to the process stdin and the write channel is closed
  // (Pandoc then reads its input from stdin). `workDir`, if non-empty, sets the
  // process working directory. A cancellable QProgressDialog is shown over
  // `parent`; cancelling kills the process (PandocResult::canceled becomes
  // true). Blocks until the process finishes or is cancelled. `progressLabel`,
  // if non-empty, is shown as the dialog's status text (Pandoc does not report
  // conversion progress, so the bar is indeterminate — the label is the only
  // textual feedback). This class is tr()-free, so callers pass translated text.
  static PandocResult run(QWidget* parent, const QStringList& args, const QByteArray& stdinData = {},
                          const QString& workDir = {}, const QString& progressLabel = {});
};

}  // namespace muffin
