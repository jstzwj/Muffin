#pragma once

#include <QString>
#include <QStringView>

#include <QByteArray>
#include <QChar>
#include <QVector>

#include <utility>
#include <vector>

namespace muffin {

// Piece-table text buffer: the editable backing store for a document's text. Logically a flat
// 0..size() sequence of QChars (so the byte-offset cursor / source-range / line-offset models are
// unchanged), but stored as an IMMUTABLE `original_` buffer (the initial text) + an APPEND-ONLY
// `changes_` buffer, threaded together by a list of "pieces" (which buffer + offset + length).
// Editing via `replace` appends the new text to `changes_` and rewires pieces — there is NO memmove
// of the whole document, which is the O(doc)-per-keystroke cost the single-QString model pays.
//
// Phase 0 of the markdownText_ → piece-table migration: a standalone, fuzz-tested class, NOT yet
// wired into MarkdownDocument. The QString-like facade (size/at/mid/toString/toUtf8) is designed to
// drop into markdownText()'s slot later with minimal call-site churn.
//
// Backing is a vector<Piece> + a cumulative-offset index (prefix_) giving O(log n) random access;
// edits are O(pieces). The public API is the contract — the backing can later be swapped for a
// balanced piece-tree (O(log n) edits) without touching callers.
class PieceTable {
public:
  PieceTable() = default;
  explicit PieceTable(QString initial);

  // Stable empty instance: a const PieceTable& fallback for ternary expressions where a session may
  // be null (e.g. `cond ? session->markdownText() : PieceTable::empty()`). Function-local static,
  // so the reference is stable for the program lifetime.
  static const PieceTable& empty();

  qsizetype size() const { return static_cast<qsizetype>(totalLength_); }
  bool isEmpty() const { return totalLength_ == 0; }
  // Number of pieces in the piece list — exposed for tests/diagnostics to assert that consecutive
  // appends coalesce (replace keeps the list small) rather than growing one piece per keystroke.
  qsizetype pieceCount() const { return static_cast<qsizetype>(pieces_.size()); }

  // O(log n) random access. `offset` must be in [0, size); unchecked (mirrors QString::at).
  QChar at(qsizetype offset) const;

  // Materialized substring [start, start+len), clamped like QString::mid. O(len + log n).
  QString mid(qsizetype start, qsizetype len) const;
  // Convenience: substring from `start` to the end, mirroring QString::mid(start).
  QString mid(qsizetype start) const { return mid(start, size() - start); }

  // Whole-text materializations — O(n). Use only where a contiguous buffer is genuinely needed
  // (cmark feed, save, export); NOT on per-keystroke paths.
  QString toString() const;
  QByteArray toUtf8() const;

  // Visit each logical text run without materializing the full document. Views are valid only for
  // the duration of the callback and arrive in document order.
  template <typename Visitor>
  void forEachChunk(Visitor&& visitor) const {
    for (const Piece& piece : pieces_) {
      visitor(QStringView(buffer(piece.fromChanges)).mid(piece.start, piece.length));
    }
  }

  // Replace [start, end) with `text`. start/end clamped to [0, size]; start<=end enforced.
  // O(pieces): appends text to changes_ and rewires pieces — no whole-document memmove.
  void replace(qsizetype start, qsizetype end, QStringView text);

  // Linear scan for `ch` from `from`. O(n log n) — fine for rare build-time callers (footnote/
  // newline lookup); not a hot path. Returns -1 if not found.
  qsizetype indexOf(QChar ch, qsizetype from = 0) const;

  int lineForOffset(qsizetype offset) const;
  qsizetype lineStartOffset(int line) const;
  qsizetype lineEndOffset(int line) const;
  int lineCount() const;
  int wordCount() const;

private:
  struct Piece {
    bool fromChanges = false;  // false → original_, true → changes_
    qsizetype start = 0;       // offset into the buffer
    qsizetype length = 0;
    qsizetype newlineCount = 0;
    qint64 wordCount = 0;
    bool startsWithWord = false;
    bool endsWithWord = false;
  };

  struct WordSummary {
    qint64 count = 0;
    bool startsWithWord = false;
    bool endsWithWord = false;
  };

  const QString& buffer(bool fromChanges) const { return fromChanges ? changes_ : original_; }
  const QVector<qsizetype>& newlines(bool fromChanges) const {
    return fromChanges ? changesNewlines_ : originalNewlines_;
  }

  // Returns {piece index, offset within that piece} for `offset` in [0, size).
  std::pair<qsizetype, qsizetype> locate(qsizetype offset) const;
  void rebuildPrefix();
  qsizetype countNewlines(bool fromChanges, qsizetype start, qsizetype length) const;
  WordSummary wordSummary(bool fromChanges, qsizetype start, qsizetype length) const;
  static bool isWordChar(QChar ch);

  QString original_;                  // immutable initial text
  QString changes_;                   // append-only edit buffer
  std::vector<Piece> pieces_;
  // prefix_[k] = total length of pieces_[0..k); prefix_.size() == pieces_.size()+1; sorted ascending
  // so `locate` binary-searches it for O(log n) offset→piece resolution.
  std::vector<qint64> prefix_;
  std::vector<qint64> prefixNewlines_;
  QVector<qsizetype> originalNewlines_;
  QVector<qsizetype> changesNewlines_;
  QVector<qsizetype> originalWordStarts_;
  QVector<qsizetype> changesWordStarts_;
  qint64 totalLength_ = 0;
  qint64 totalNewlines_ = 0;
  qint64 totalWords_ = 0;
};

}  // namespace muffin
