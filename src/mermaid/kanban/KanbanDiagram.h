#pragma once

// Native parser/database model for Mermaid 11.16.0 Kanban diagrams.

#include <QJsonValue>
#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::kanban {

enum class KanbanErrorKind { Lexer, Parser, Yaml, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(KanbanErrorKind kind) {
  switch (kind) {
  case KanbanErrorKind::Lexer: return u"lexer";
  case KanbanErrorKind::Parser: return u"parse";
  case KanbanErrorKind::Yaml: return u"yaml";
  case KanbanErrorKind::Runtime: return u"runtime";
  }
  return u"parse";
}

enum class KanbanNodeType {
  Default = 0,
  RoundedRect = 1,
  Rect = 2,
  Circle = 3,
  Cloud = 4,
  Bang = 5,
  Hexagon = 6,
};

struct KanbanParseConfig {
  // These stay raw because kanbanDb observes JavaScript coercion for padding
  // and preserves maxNodeWidth without first converting it to a number.
  QJsonValue mindmapPadding = QJsonValue(10.0);
  QJsonValue mindmapMaxNodeWidth = QJsonValue(200.0);
  QString look = QStringLiteral("classic");
};

struct KanbanNode {
  QString id;
  int level = 0;
  QString label;
  QString parentId;
  QJsonValue width = QJsonValue(QJsonValue::Undefined);
  QJsonValue padding = QJsonValue(QJsonValue::Undefined);
  bool isGroup = false;
  QString shape;
  QString ticket;
  QJsonValue priority = QJsonValue(QJsonValue::Undefined);
  QString assigned;
  QString icon;
  QString cssClasses;
  QString look;
};

struct KanbanData {
  // Mermaid exposes both the parser database's raw sections and getData()'s
  // renderer-facing flattened nodes. Duplicate section ids make the latter
  // observably different, so both collections are retained.
  QVector<KanbanNode> sections;
  QVector<KanbanNode> nodes;
};

struct KanbanParseError : DiagramError {
  DiagramErrorInfo info() const override;
  int line = 0;
  int column = 0;
  KanbanErrorKind kind = KanbanErrorKind::Parser;
  QString token;

  KanbanParseError(const QString& message, int line = 0, int column = 0, KanbanErrorKind kind = KanbanErrorKind::Parser,
                   QString token = {});
};

class KanbanDiagram {
public:
  static KanbanData parse(const QString& source, const KanbanParseConfig& config = {});
};

} // namespace muffin::mermaid::kanban
