#pragma once

#include <QStringView>
#include <QVector>

namespace muffin {

class LineStartOffsetCache {
public:
  LineStartOffsetCache();
  explicit LineStartOffsetCache(QStringView text);

  void rebuild(QStringView text);
  // Patch lineStarts_ for an in-place text edit without rescanning the whole document.
  // fullPostEditText is the document text AFTER the replace; sourceStart/removedLen describe the
  // pre-edit removed range and insertedLen the length of its replacement.
  void applyEdit(qsizetype sourceStart, qsizetype removedLen, qsizetype insertedLen, QStringView fullPostEditText);
  qsizetype offsetForLineColumn(int line, int column) const;
  // text is the document the cache indexes; the cache no longer owns a copy (see applyEdit/rebuild).
  qsizetype offsetForLineByteColumn(QStringView text, int line, int column) const;
  // Byte offset where the 1-indexed `line` begins (the cache's lineStarts_ entry). -1 if out of
  // range. Cheap O(1); the symmetric counterpart to lineEndOffset.
  qsizetype lineStartOffset(int line) const;
  // Content of the 1-indexed `line` as a view into `doc`, exclusive of the terminating newline (a
  // trailing '\r' is kept, matching QString::split('\n') so trimmed()/isEmpty() callers behave the
  // same). Returns a null view for an out-of-range line, an empty (non-null) view for an in-range
  // blank line. Lets line-oriented passes index lines without splitting the whole document.
  QStringView lineText(QStringView doc, int line) const;
  qsizetype lineEndOffset(int line) const;
  int lineForOffset(qsizetype offset) const;
  int lineCount() const;

  // Perf instrumentation for offsetForLineByteColumn (the byte-column walk).
  static void setByteColPerfEnabled(bool enabled);
  static void resetByteColPerf();
  static qreal byteColPerfMs();

private:
  QVector<qsizetype> lineStarts_;
  // Per-line "all code points <= 0x7F" flag, computed for free during rebuild()'s '\n' scan. Lets
  // offsetForLineByteColumn take an O(1) ASCII fast path (byte column == code-unit column) instead
  // of scanning the line on every call. Populated by rebuild(), cleared by applyEdit() — the only
  // caller of offsetForLineByteColumn is the parse path (a freshly rebuilt cache), so an
  // applyEdit-maintained cache simply leaves this empty and the fast path falls back to a scan.
  QVector<quint8> lineIsAscii_;
  qsizetype textSize_ = 0;
};

}  // namespace muffin
