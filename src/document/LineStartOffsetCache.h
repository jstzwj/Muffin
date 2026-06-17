#pragma once

#include <QStringView>
#include <QVector>

namespace muffin {

class LineStartOffsetCache {
public:
  LineStartOffsetCache();
  explicit LineStartOffsetCache(QStringView text);

  void rebuild(QStringView text);
  qsizetype offsetForLineColumn(int line, int column) const;
  qsizetype offsetForLineByteColumn(int line, int column) const;
  qsizetype lineEndOffset(int line) const;
  int lineForOffset(qsizetype offset) const;
  int lineCount() const;

  // Perf instrumentation for offsetForLineByteColumn (the byte-column walk).
  static void setByteColPerfEnabled(bool enabled);
  static void resetByteColPerf();
  static qreal byteColPerfMs();

private:
  QString text_;
  QVector<qsizetype> lineStarts_;
  qsizetype textSize_ = 0;
};

}  // namespace muffin
