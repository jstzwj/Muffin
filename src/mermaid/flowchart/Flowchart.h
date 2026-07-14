#pragma once

#include "mermaid/flowchart/FlowchartDiagnostic.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::flowchart {

struct FlowVertex {
  QString id;
  QString labelType = QStringLiteral("text");
  QString domId;
  QStringList styles;
  QStringList classes;
  QString text;
  QString type;
  QJsonObject props;
  QString link;
  QString linkTarget;
  // Muffin-only (milestone H2): set at parse time when `link` fails the URL
  // safety check (muffin::isSafeUrl). The raw link is PRESERVED so the AST stays
  // faithful to upstream; the render/export layer (milestone I) consults this
  // flag + MermaidSecurityPolicy to decide whether to make the link interactive.
  // NOT serialized into toJson (it is not part of the upstream-comparable AST).
  bool linkUnsafe = false;
};

struct FlowEdge {
  QString start;
  QString end;
  QString type;
  QString text;
  QString labelType = QStringLiteral("text");
  QStringList classes;
  bool userDefinedId = false;
  QString stroke;
  int length = 1;
  QString id;
  QStringList style;
  QString interpolate;
  bool hasAnimate = false;
  bool animate = false;
  QString animation;
};

struct FlowClass {
  QString id;
  QStringList styles;
  QStringList textStyles;
};

struct FlowSubgraph {
  QString id;
  QStringList nodes;
  QString title;
  QStringList classes;
  QString dir;
  bool hasExplicitDir = false;
  QString labelType = QStringLiteral("text");
};

struct FlowchartData {
  QString direction;
  QString title;
  QString accTitle;
  QString accDescription;
  QVector<FlowVertex> vertices;
  QVector<FlowEdge> edges;
  // Renderer projection state from linkStyle default. Mermaid keeps these on
  // its edge collection rather than serializing them into each raw DB edge.
  QStringList defaultEdgeStyles;
  QString defaultEdgeInterpolate;
  QVector<FlowClass> classes;
  QVector<FlowSubgraph> subgraphs;
  QMap<QString, QString> tooltips;
};

struct FlowchartParseOptions {
  int maxEdges = 500;
  int maxTextSize = 50000;
};

// Muffin's safety boundary against pathological input (milestone H1). SEPARATE
// from FlowchartParseOptions (which stays semantic, matching upstream): upstream
// mermaid has no such limits (a browser tab just OOMs), but Muffin is a desktop
// editor where unbounded growth crashes the document session — so these bounds
// are intentionally stricter than upstream. A parity harness sets them to
// INT_MAX to compare raw AST faithfulness; the editor uses the Muffin defaults.
// Exceeding any bound throws FlowchartParseError{LimitExceeded}.
struct FlowchartLimits {
  int maxVertices = 2000;
  int maxClasses = 200;
  int maxSubgraphs = 50;
  int maxSubgraphDepth = 10;  // nesting depth of the open-subgraph stack
  int maxTooltips = 2000;
  int maxLineLength = 10000;
  int maxNodeIdLength = 256;
  int maxStylesPerVertex = 64;
};

// Coarse error classifier used by the Level-1 error golden (milestone G1) and by
// the resource-limit / security throws (milestone H). The contract compared
// against upstream is the CATEGORY (+ line/column where determinable), not the
// human message — messages legitimately diverge between a JS grammar and a C++
// hand-written parser. `Syntax` is the default for legacy message-only throws.
class FlowchartParseError final : public std::runtime_error {
public:
  explicit FlowchartParseError(FlowchartDiagnostic diagnostic);

  FlowchartErrorCategory category() const noexcept { return category_; }
  FlowchartErrorStage stage() const noexcept { return diagnostic_.stage; }
  FlowchartErrorCode code() const noexcept { return diagnostic_.code; }
  const FlowchartDiagnostic& diagnostic() const noexcept { return diagnostic_; }
  int line() const noexcept { return diagnostic_.span.line; }
  int column() const noexcept { return diagnostic_.span.column; }
  qsizetype offset() const noexcept { return diagnostic_.span.offset; }
  qsizetype length() const noexcept { return diagnostic_.span.length; }

private:
  FlowchartErrorCategory category_ = FlowchartErrorCategory::Syntax;
  FlowchartDiagnostic diagnostic_;
};

class Flowchart {
public:
  static Flowchart parse(const QString& source, FlowchartParseOptions options = {}, FlowchartLimits limits = {});

  const FlowchartData& data() const;
  QJsonObject toJson() const;

private:
  FlowchartData data_;
};

}  // namespace muffin::mermaid::flowchart
