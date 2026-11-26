#pragma once

#include <QString>
#include <QVector>
#include <QPointF>
#include <QSizeF>

#include <optional>
#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::wardley {

enum class WardleyErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(WardleyErrorKind kind) {
  switch (kind) {
  case WardleyErrorKind::Lexer:
    return u"lexer";
  case WardleyErrorKind::Parser:
    return u"parse";
  case WardleyErrorKind::Runtime:
    return u"runtime";
  }
  return u"parse";
}

class WardleyParseError : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  WardleyParseError(WardleyErrorKind kind, int line, int column, QString token, const QString& message);

  WardleyErrorKind kind;
  int line;
  int column;
  QString token;
};

struct WardleyNode {
  QString id;
  QString label;
  qreal x = 0.0;
  qreal y = 0.0;
  QString className;
  std::optional<qreal> labelOffsetX;
  std::optional<qreal> labelOffsetY;
  bool inPipeline = false;
  bool isPipelineParent = false;
  bool inertia = false;
  QString sourceStrategy;
};

struct WardleyLink {
  QString source;
  QString target;
  bool dashed = false;
  QString label;
  bool hasLabel = false;
  QString flow;
};

struct WardleyTrend {
  QString nodeId;
  qreal targetX = 0.0;
  qreal targetY = 0.0;
};

struct WardleyPipeline {
  QString nodeId;
  QVector<QString> componentIds;
};

struct WardleyAnnotation {
  int number = 0;
  QPointF coordinate;
  QString text;
  bool hasText = false;
};

struct WardleyNote {
  QString text;
  QPointF coordinate;
};

struct WardleyAccelerator {
  QString name;
  QPointF coordinate;
};

struct WardleyAxes {
  QVector<QString> stages;
  QVector<qreal> stageBoundaries;
};

struct WardleyData {
  QString title;
  QString accTitle;
  QString accDescr;
  bool hasTitleDirective = false;
  QVector<WardleyNode> nodes;
  QVector<WardleyLink> links;
  QVector<WardleyTrend> trends;
  QVector<WardleyPipeline> pipelines;
  QVector<WardleyAnnotation> annotations;
  QVector<WardleyNote> notes;
  QVector<WardleyAccelerator> accelerators;
  QVector<WardleyAccelerator> deaccelerators;
  std::optional<QPointF> annotationsBox;
  WardleyAxes axes;
  std::optional<QSizeF> size;
};

class WardleyDiagram {
public:
  static WardleyData parse(const QString& source);
};

} // namespace muffin::mermaid::wardley
