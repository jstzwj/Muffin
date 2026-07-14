#pragma once

#include <QString>
#include <QStringList>

namespace muffin::mermaid::flowchart {

enum class FlowchartErrorCategory {
  Syntax,
  MissingHeader,
  UnclosedSubgraph,
  UnexpectedEnd,
  InvalidNode,
  InvalidDirective,
  LinkStyleBounds,
  LimitExceeded,
  SecurityViolation,
};

enum class FlowchartErrorStage { Detector, Lexer, Parser, Semantic, Resource, Security };

enum class FlowchartErrorCode {
  Generic,
  UnexpectedCharacter,
  InvalidDirection,
  UnterminatedString,
  UnterminatedShapeData,
  UnterminatedCallbackArguments,
  UnexpectedToken,
  MissingToken,
  MissingValue,
  MissingListItem,
  InvalidNode,
  InvalidMetadata,
  MissingLinkEndpoint,
  MissingHeader,
  UnclosedSubgraph,
  UnexpectedEnd,
  LinkStyleBounds,
  LimitExceeded,
  SecurityViolation,
};

struct FlowchartSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 0;
  int column = 0;
};

struct FlowchartDiagnostic {
  FlowchartErrorCategory category = FlowchartErrorCategory::Syntax;
  FlowchartErrorStage stage = FlowchartErrorStage::Parser;
  FlowchartErrorCode code = FlowchartErrorCode::Generic;
  FlowchartSourceSpan span;
  QString production;
  QString actual;
  QStringList expected;
  QString detail;
};

QString flowchartErrorStageName(FlowchartErrorStage stage);
QString flowchartErrorCodeName(FlowchartErrorCode code);
QString formatFlowchartDiagnostic(const FlowchartDiagnostic& diagnostic);

}  // namespace muffin::mermaid::flowchart
