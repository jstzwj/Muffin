#pragma once

// Native parser/database model for Mermaid 11.16.0 Mindmap diagrams.

#include <QJsonValue>
#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::mindmap {

enum class MindmapErrorKind { Lexer, Parser, Runtime };

enum class MindmapNodeType {
  Default = 0,
  RoundedRect = 1,
  Rect = 2,
  Circle = 3,
  Cloud = 4,
  Bang = 5,
  Hexagon = 6,
};

struct MindmapParseConfig {
  // MindmapDB preserves these values for default nodes and applies JavaScript
  // numeric coercion only when shape padding is doubled.
  QJsonValue padding = QJsonValue(10.0);
  QJsonValue maxNodeWidth = QJsonValue(200.0);
  QJsonValue useMaxWidth = QJsonValue(true);
  QString look = QStringLiteral("classic");
  QString theme = QStringLiteral("default");
  QString layout;
  bool userDefinedLayout = false;
};

struct MindmapAnchor {
  QString href;
  QString label;
  qsizetype start = 0;
  qsizetype length = 0;
};

struct MindmapNode {
  int id = -1;
  QString nodeId;
  int level = 0;
  QString descr;
  MindmapNodeType type = MindmapNodeType::Default;
  QJsonValue width = QJsonValue(QJsonValue::Undefined);
  QJsonValue padding = QJsonValue(QJsonValue::Undefined);
  bool isRoot = false;
  QString icon;
  QString cssClass;
  QVector<MindmapAnchor> anchors;
  int parentId = -1;
  QVector<int> children;

  // getData() projection, calculated after parsing.
  bool hasSection = false;
  int section = -1;
  QString shape;
  QString cssClasses;
  QString look;
};

struct MindmapEdge {
  QString id;
  int start = -1;
  int end = -1;
  QString look;
  QString classes;
  int depth = 0;
  bool hasSection = false;
  int section = -1;
};

struct MindmapData {
  int rootId = -1;
  QVector<MindmapNode> nodes;
  QVector<MindmapEdge> edges;
  MindmapParseConfig config;
  QString effectiveLayout = QStringLiteral("cose-bilkent");
};

struct MindmapParseError : std::runtime_error {
  int line = 0;
  int column = 0;
  MindmapErrorKind kind = MindmapErrorKind::Parser;
  QString token;

  MindmapParseError(const QString& message, int line = 0, int column = 0,
                    MindmapErrorKind kind = MindmapErrorKind::Parser,
                    QString token = {});
};

class MindmapDiagram {
public:
  static MindmapData parse(const QString& source,
                           const MindmapParseConfig& config = {});
};

}  // namespace muffin::mermaid::mindmap
