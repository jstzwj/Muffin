#pragma once

#include "mermaid/flowchart/FlowchartDiagnostic.h"

#include <QString>
#include <QVector>

#include <functional>

namespace muffin::mermaid::flowchart {

enum class FlowTokenKind {
  Eof, Semi, Newline, Space, Graph, NoDir, Dir, Subgraph, End,
  AccTitle, AccTitleValue, AccDescr, AccDescrValue, AccDescrMultilineValue,
  ShapeData, Amp, StyleSeparator, DoubleCircleStart, DoubleCircleEnd,
  Sqs, Sqe, Ps, Pe, EllipseStart, EllipseEnd, StadiumStart, StadiumEnd,
  SubroutineStart, SubroutineEnd, VertexWithPropsStart, Colon, Pipe,
  CylinderStart, CylinderEnd, DiamondStart, DiamondStop, TagStart, TagEnd,
  TrapStart, TrapEnd, InvTrapStart, InvTrapEnd, StartLink, Link, LinkId,
  Str, MarkdownStr, Style, LinkStyle, ClassDef, Class, Click, Down, Up,
  CallbackName, CallbackArgs, Href, LinkTarget, Default, Interpolate,
  Number, Comma, NodeString, Unit, Bracket, Percent, Minus, Multiply,
  UnicodeText, Text, EdgeText, DirectionTb, DirectionBt, DirectionRl,
  DirectionLr, DirectionTd, Quote, Invalid, Unknown
};

struct FlowToken {
  FlowTokenKind kind = FlowTokenKind::Unknown;
  QString text;
  int line = 1;
  int column = 1;
  qsizetype offset = 0;
  FlowchartErrorCode diagnosticCode = FlowchartErrorCode::Generic;
};

QString flowTokenName(FlowTokenKind kind);

class FlowchartTokenizer {
public:
  explicit FlowchartTokenizer(QString source, bool expectGraphHeader = true);

  FlowToken next();
  QVector<FlowToken> tokenize();

private:
  enum class Mode {
    Initial, Dir, Text, TrapText, EdgeText, ThickEdgeText, DottedEdgeText,
    AccTitleValue, AccDescrValue, CallbackName, CallbackArgs
  };

  bool atEnd() const;
  QChar peek(qsizetype ahead = 0) const;
  bool startsWith(QStringView value, Qt::CaseSensitivity cs = Qt::CaseSensitive) const;
  FlowToken take(FlowTokenKind kind, qsizetype length, QString text = {});
  FlowToken takeInvalid(FlowchartErrorCode code, qsizetype length);
  FlowToken takeWhile(FlowTokenKind kind, const std::function<bool(QChar)>& predicate);
  FlowToken lexInitial();
  FlowToken lexDir();
  FlowToken lexText();
  FlowToken lexTrapText();
  FlowToken lexEdgeText(QChar delimiter, Mode mode);
  FlowToken lexAccessibilityValue(FlowTokenKind kind);
  FlowToken lexCallbackName();
  FlowToken lexCallbackArgs();
  FlowToken lexQuoted();
  FlowToken lexShapeData();
  FlowToken tryKeyword();
  FlowToken tryLink();
  void advance(const QString& consumed);

  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 1;
  QVector<Mode> modes_{Mode::Initial};
  bool firstGraph_ = true;
};

}  // namespace muffin::mermaid::flowchart
