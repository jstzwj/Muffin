#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

namespace muffin {

struct DefinitionFieldRange {
  qsizetype start = -1;
  qsizetype end = -1;

  bool isValid() const {
    return start >= 0 && end >= start;
  }

  qsizetype length() const {
    return isValid() ? end - start : 0;
  }
};

struct DefinitionBlock {
  enum class Kind {
    None,
    Link,
    Footnote
  };

  Kind kind = Kind::None;
  QString label;
  QString destination;
  QString title;
  QString note;
  DefinitionFieldRange labelRange;
  DefinitionFieldRange destinationRange;
  DefinitionFieldRange titleRange;
  DefinitionFieldRange noteRange;
  DefinitionFieldRange markerRange;
  DefinitionFieldRange sourceRange;
  bool titleQuoted = false;

  bool isValid() const {
    return kind != Kind::None && markerRange.isValid();
  }
};

QVector<DefinitionBlock> scanDefinitionBlocks(QStringView markdown);
bool parseDefinitionBlockLine(QStringView line, qsizetype sourceStart, DefinitionBlock& definition);

}  // namespace muffin
