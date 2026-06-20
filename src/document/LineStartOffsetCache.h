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
  qsizetype lineEndOffset(int line) const;
  int lineForOffset(qsizetype offset) const;
  int lineCount() const;

  // Perf instrumentation for offsetForLineByteColumn (the byte-column walk).
  static void setByteColPerfEnabled(bool enabled);
  static void resetByteColPerf();
  static qreal byteColPerfMs();

private:
  QVector<qsizetype> lineStarts_;
  qsizetype textSize_ = 0;
};

}  // namespace muffin
