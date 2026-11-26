#pragma once

#include "Export.h"
#include <QElapsedTimer>
#include <QString>

#include <chrono>
#include <functional>

class QByteArray;
class QSaveFile;

namespace muffin {

// A file written the way QSaveFile writes it, into a temporary file beside it
// that is renamed over it once complete, and one thing more. On Windows the
// system can refuse the file for a moment while another process looks at it,
// an antivirus scanner, a search indexer, a sync client: it refuses the
// opening of the temporary file, or its rename over the target. The refusal
// passes, so the write starts again, with a fresh temporary file, until the
// budget is over. What will not pass is reported at once, as it always was: a
// read-only file, a name the system does not take, a write that failed, a file
// that changed under the write.
class MUFFIN_CORE_EXPORT AtomicWrite final {
public:
  enum class Result {
    Written,  // the target holds what was written
    Failed,   // `error` says why; the target is what it was
    Refused,  // the system kept refusing the file for the whole budget; `error` says how
    Declined, // the caller turned the write down; the target is what it was
  };

  // The time the writes of one operation may spend waiting out refusals,
  // counted from the first. A folder of files that keeps being refused costs
  // it once, not once a file; a write without one has a budget of its own.
  class Budget {
  public:
    explicit Budget(std::chrono::milliseconds total = std::chrono::milliseconds(1000)) : total_(total) {}

  private:
    friend class AtomicWrite;
    // Sleeps a little before another attempt; false once the budget is over.
    bool waitOut();
    const std::chrono::milliseconds total_;
    QElapsedTimer clock_;
  };

  /// Replaces `path` with `bytes`. A write of them that fails is `Failed` and
  /// says why in `error`.
  static Result write(const QString& path, const QByteArray& bytes, QString* error = nullptr, Budget* budget = nullptr,
                      const std::function<bool()>& stillCurrent = nullptr);

  /// The same, with `writer` filling the open temporary file. It returns false
  /// to turn the write down, saying why in `error` itself. `stillCurrent` runs
  /// once the bytes are staged and before they replace the target, on every
  /// attempt; false turns the write down there. A turned-down write is not
  /// retried. Neither may run an event loop: a nested one would let autosave
  /// or a reload run in the middle of the write.
  static Result write(const QString& path, const std::function<bool(QSaveFile&)>& writer, QString* error = nullptr,
                      Budget* budget = nullptr, const std::function<bool()>& stillCurrent = nullptr);

  AtomicWrite() = delete;
};

} // namespace muffin
