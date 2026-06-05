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
  qsizetype lineEndOffset(int line) const;
  int lineForOffset(qsizetype offset) const;
  int lineCount() const;

private:
  QVector<qsizetype> lineStarts_;
  qsizetype textSize_ = 0;
};

}  // namespace muffin
