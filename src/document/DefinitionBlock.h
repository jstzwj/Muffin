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

  enum class DestinationDelimiter {
    Bare,
    Angle
  };

  enum class TitleDelimiter {
    None,
    DoubleQuote,
    SingleQuote,
    Parentheses
  };

  Kind kind = Kind::None;
  QString label;
  QString destination;
  QString title;
  QString note;
  QString sourceText;
  DefinitionFieldRange labelRange;
  DefinitionFieldRange destinationRange;
  DefinitionFieldRange titleRange;
  DefinitionFieldRange noteRange;
  DefinitionFieldRange markerRange;
  DefinitionFieldRange sourceRange;
  DestinationDelimiter destinationDelimiter = DestinationDelimiter::Bare;
  TitleDelimiter titleDelimiter = TitleDelimiter::None;
  bool titleQuoted = false;
  bool virtualTemplate = false;

  bool isValid() const {
    return kind != Kind::None && markerRange.isValid();
  }
};

enum class DefinitionParseClassification {
  Invalid,
  ValidMarkdownDefinition,
  VirtualTemplate
};

struct DefinitionParseResult {
  DefinitionParseClassification classification = DefinitionParseClassification::Invalid;
  DefinitionBlock definition;

  bool isRecognized() const {
    return classification != DefinitionParseClassification::Invalid && definition.isValid();
  }
};

QVector<DefinitionParseResult> scanDefinitionBlocks(QStringView markdown);
DefinitionParseResult parseDefinitionBlockLine(QStringView line, qsizetype sourceStart);

}  // namespace muffin
