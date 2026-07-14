#include "mermaid/flowchart/Flowchart.h"

#include "blocks/html/HtmlUrlSafety.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace muffin::mermaid::flowchart {
namespace {

using Vertex = FlowVertex;
using Edge = FlowEdge;
using Subgraph = FlowSubgraph;

QJsonArray strings(const QStringList& values) {
  QJsonArray result;
  for (const QString& value : values) result.push_back(value);
  return result;
}

QJsonObject vertexJson(const Vertex& vertex) {
  QJsonObject result;
  result.insert(QStringLiteral("id"), vertex.id);
  result.insert(QStringLiteral("labelType"), vertex.labelType);
  result.insert(QStringLiteral("domId"), vertex.domId);
  result.insert(QStringLiteral("styles"), strings(vertex.styles));
  result.insert(QStringLiteral("classes"), strings(vertex.classes));
  result.insert(QStringLiteral("text"), vertex.text);
  if (!vertex.type.isEmpty()) result.insert(QStringLiteral("type"), vertex.type);
  result.insert(QStringLiteral("props"), vertex.props);
  if (!vertex.link.isEmpty()) result.insert(QStringLiteral("link"), vertex.link);
  if (!vertex.linkTarget.isEmpty()) result.insert(QStringLiteral("linkTarget"), vertex.linkTarget);
  return result;
}

QJsonObject edgeJson(const Edge& edge) {
  QJsonObject result;
  result.insert(QStringLiteral("start"), edge.start);
  result.insert(QStringLiteral("end"), edge.end);
  result.insert(QStringLiteral("type"), edge.type);
  result.insert(QStringLiteral("text"), edge.text);
  result.insert(QStringLiteral("labelType"), edge.labelType);
  result.insert(QStringLiteral("classes"), strings(edge.classes));
  result.insert(QStringLiteral("isUserDefinedId"), edge.userDefinedId);
  result.insert(QStringLiteral("stroke"), edge.stroke);
  result.insert(QStringLiteral("length"), edge.length);
  result.insert(QStringLiteral("id"), edge.id);
  if (!edge.style.isEmpty()) result.insert(QStringLiteral("style"), strings(edge.style));
  if (!edge.interpolate.isEmpty()) result.insert(QStringLiteral("interpolate"), edge.interpolate);
  if (edge.hasAnimate) result.insert(QStringLiteral("animate"), edge.animate);
  if (!edge.animation.isEmpty()) result.insert(QStringLiteral("animation"), edge.animation);
  return result;
}

QStringList splitStyles(QString source) {
  const QString escapedComma = QStringLiteral("__MERMAID_ESCAPED_COMMA__");
  source.replace(QStringLiteral("\\,"), escapedComma);
  source.replace(QLatin1Char(','), QLatin1Char(';'));
  source.replace(escapedComma, QStringLiteral(","));
  QStringList result;
  for (QString value : source.split(QLatin1Char(';'))) {
    value = value.trimmed();
    if (!value.isEmpty()) result.push_back(value);
  }
  return result;
}

QString normalizedDirection(QString direction) {
  direction = direction.trimmed();
  if (direction.contains(QLatin1Char('<'))) return QStringLiteral("RL");
  if (direction.contains(QLatin1Char('^'))) return QStringLiteral("BT");
  if (direction.contains(QLatin1Char('>'))) return QStringLiteral("LR");
  if (direction.contains(QLatin1Char('v'))) return QStringLiteral("TB");
  return direction == QLatin1String("TD") ? QStringLiteral("TB") : direction;
}

struct ParsedNode {
  QString id;
  QString text;
  QString labelType = QStringLiteral("text");
  QString type;
  QStringList classes;
  bool metadata = false;
};

QMap<QString, QString> parseMetadataFields(const QString& source) {
  QMap<QString, QString> result;
  QStringList fields;
  QString current;
  bool quoted = false;
  for (const QChar ch : source) {
    if (ch == QLatin1Char('"')) quoted = !quoted;
    if (ch == QLatin1Char(',') && !quoted) {
      fields.push_back(current);
      current.clear();
    } else {
      current += ch;
    }
  }
  if (!current.trimmed().isEmpty()) fields.push_back(current);
  for (const QString& field : fields) {
    const qsizetype colon = field.indexOf(QLatin1Char(':'));
    if (colon < 0) continue;
    const QString key = field.left(colon).trimmed();
    QString value = field.mid(colon + 1).trimmed();
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')) {
      value = value.mid(1, value.size() - 2);
    }
    result.insert(key, value);
  }
  return result;
}

QString unquoteLabel(QString text, QString& labelType) {
  text = text.trimmed();
  if (text.size() >= 2 && text.front() == QLatin1Char('"') && text.back() == QLatin1Char('"')) {
    text = text.mid(1, text.size() - 2);
    labelType = QStringLiteral("string");
    if (text.size() >= 2 && text.front() == QLatin1Char('`') && text.back() == QLatin1Char('`')) {
      text = text.mid(1, text.size() - 2);
      labelType = QStringLiteral("markdown");
    }
  }
  return text;
}

ParsedNode parseNode(QString source, int line = 0) {
  source = source.trimmed();
  ParsedNode node;
  const qsizetype classAt = source.lastIndexOf(QStringLiteral(":::"));
  if (classAt >= 0) {
    node.classes = source.mid(classAt + 3).split(QLatin1Char(','), Qt::SkipEmptyParts);
    source = source.left(classAt).trimmed();
  }

  const qsizetype metadataAt = source.indexOf(QStringLiteral("@{"));
  if (metadataAt > 0 && source.endsWith(QLatin1Char('}'))) {
    const QMap<QString, QString> fields = parseMetadataFields(source.mid(metadataAt + 2, source.size() - metadataAt - 3));
    // The part before @{...} is a normal node spec (e.g. `A[Alpha]` or `A`) —
    // parse it for id/label/bracket-type, then let metadata override shape/label.
    node = parseNode(source.left(metadataAt).trimmed(), line);
    node.metadata = true;
    if (fields.contains(QStringLiteral("label"))) {
      node.text = fields.value(QStringLiteral("label"));
      node.labelType = QStringLiteral("markdown");
    }
    if (fields.contains(QStringLiteral("shape"))) node.type = fields.value(QStringLiteral("shape"));
    return node;
  }

  static const QVector<QPair<QRegularExpression, QString>> shapes = {
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\(\((.*)\)\)$)")), QStringLiteral("circle")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\(\[(.*)\]\)$)")), QStringLiteral("stadium")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[\[(.*)\]\]$)")), QStringLiteral("subroutine")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[\((.*)\)\]$)")), QStringLiteral("cylinder")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\{\{(.*)\}\}$)")), QStringLiteral("hexagon")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\{(.*)\}$)")), QStringLiteral("diamond")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)>(.*)\]$)")), QStringLiteral("odd")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[/(.*)\\\]$)")), QStringLiteral("trapezoid")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[\\(.*)/\]$)")), QStringLiteral("inv_trapezoid")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[/(.*)/\]$)")), QStringLiteral("lean_right")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[\\(.*)\\\]$)")), QStringLiteral("lean_left")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\((.*)\)$)")), QStringLiteral("round")},
      {QRegularExpression(QStringLiteral(R"(^([^\s\[\](){}>]+)\[(.*)\]$)")), QStringLiteral("square")},
  };
  for (const auto& [expression, type] : shapes) {
    const QRegularExpressionMatch match = expression.match(source);
    if (match.hasMatch()) {
      node.id = match.captured(1);
      node.text = unquoteLabel(match.captured(2), node.labelType);
      node.type = type;
      return node;
    }
  }

  static const QRegularExpression idExpression(QStringLiteral(R"(^([^\s]+)$)"));
  const QRegularExpressionMatch idMatch = idExpression.match(source);
  if (!idMatch.hasMatch())
    throw FlowchartParseError(QStringLiteral("Invalid flowchart node: %1").arg(source), FlowchartErrorCategory::InvalidNode, line);
  node.id = idMatch.captured(1);
  node.text = node.id;
  return node;
}

QStringList splitGroups(const QString& source) {
  QStringList result;
  QString current;
  int depth = 0;
  bool quoted = false;
  for (qsizetype i = 0; i < source.size(); ++i) {
    const QChar ch = source.at(i);
    if (ch == QLatin1Char('"')) quoted = !quoted;
    if (!quoted && QStringLiteral("[({").contains(ch)) ++depth;
    if (!quoted && QStringLiteral("])}").contains(ch)) --depth;
    if (!quoted && depth == 0 && ch == QLatin1Char('&')) {
      result.push_back(current.trimmed());
      current.clear();
    } else {
      current += ch;
    }
  }
  if (!current.trimmed().isEmpty()) result.push_back(current.trimmed());
  return result;
}

struct LinkToken {
  qsizetype start = 0;
  qsizetype end = 0;
  QString raw;
  QString text;
  QString id;
  QString type = QStringLiteral("arrow_point");
  QString stroke = QStringLiteral("normal");
  int length = 1;
};

QVector<LinkToken> findLinks(const QString& line) {
  QVector<LinkToken> result;
  int depth = 0;
  bool quoted = false;
  for (qsizetype i = 0; i < line.size();) {
    const QChar ch = line.at(i);
    if (ch == QLatin1Char('"')) quoted = !quoted;
    if (!quoted && QStringLiteral("[({").contains(ch)) {
      ++depth;
      ++i;
      continue;
    }
    if (!quoted && QStringLiteral("])}").contains(ch)) {
      --depth;
      ++i;
      continue;
    }
    if (quoted || depth != 0) {
      ++i;
      continue;
    }

    LinkToken token;
    token.start = i;
    if (line.mid(i, 4) == QLatin1String("<-->")) {
      token.end = i + 4;
      token.raw = QStringLiteral("<-->");
      token.type = QStringLiteral("double_arrow_point");
    } else if (line.mid(i, 3) == QLatin1String("--x") || line.mid(i, 3) == QLatin1String("--o")) {
      token.end = i + 3;
      token.raw = line.mid(i, 3);
    } else if (line.mid(i, 2) == QLatin1String("-.")) {
      const qsizetype labelledEnd = line.indexOf(QStringLiteral(".->"), i + 2);
      if (labelledEnd > i + 2 && !line.mid(i + 2, labelledEnd - i - 2).trimmed().isEmpty()) {
        token.end = labelledEnd + 3;
        token.raw = line.mid(i, token.end - i);
        token.text = line.mid(i + 2, labelledEnd - i - 2).trimmed();
      } else {
        qsizetype p = i + 1;
        while (p < line.size() && line.at(p) == QLatin1Char('.')) ++p;
        if (line.mid(p, 2) != QLatin1String("->")) {
          ++i;
          continue;
        }
        token.end = p + 2;
        token.raw = line.mid(i, token.end - i);
      }
    } else if (line.mid(i, 3) == QLatin1String("-- ")) {
      // Labeled long edge: `-- text -->` (point) or `-- text ---` (open). Find
      // the earliest closing arrow after the label text (both are 4 chars:
      // " -->" / " ---"). Previously only " ---" was recognized, so `A -- x --> B`
      // (the common form) mis-parsed as a node → InvalidNode.
      const qsizetype arrowEnd = line.indexOf(QStringLiteral(" -->"), i + 3);
      const qsizetype openEnd = line.indexOf(QStringLiteral(" ---"), i + 3);
      qsizetype end = -1;
      if (arrowEnd >= 0 && openEnd >= 0) end = std::min(arrowEnd, openEnd);
      else if (arrowEnd >= 0) end = arrowEnd;
      else if (openEnd >= 0) end = openEnd;
      if (end < 0) {
        ++i;
        continue;
      }
      token.end = end + 4;
      token.raw = line.mid(i, token.end - i);
      token.text = line.mid(i + 3, end - i - 3).trimmed();
    } else if (ch == QLatin1Char('o') || ch == QLatin1Char('x')) {
      // Bidirectional edges o--o / x--x (and o==o / x==x thick, longer runs).
      // Upstream destructLink yields double_arrow_circle / double_arrow_cross.
      qsizetype p = i + 1;
      while (p < line.size() && (line.at(p) == QLatin1Char('-') || line.at(p) == QLatin1Char('='))) ++p;
      if (p <= i + 1 || p >= line.size() || line.at(p) != ch) {
        ++i;
        continue;
      }
      token.end = p + 1;
      token.raw = line.mid(i, token.end - i);
      token.type = (ch == QLatin1Char('o')) ? QStringLiteral("double_arrow_circle")
                                            : QStringLiteral("double_arrow_cross");
    } else if (ch == QLatin1Char('~')) {
      // Invisible edge ~~~ (min 3 tildes): arrow_open + stroke "invisible".
      qsizetype p = i;
      while (p < line.size() && line.at(p) == QLatin1Char('~')) ++p;
      if (p - i < 3) {
        ++i;
        continue;
      }
      token.end = p;
      token.raw = line.mid(i, token.end - i);
      token.type = QStringLiteral("arrow_open");
      token.stroke = QStringLiteral("invisible");
      token.length = std::max<qsizetype>(1, token.raw.count(QLatin1Char('~')) - 2);
    } else if (ch == QLatin1Char('-')) {
      qsizetype p = i;
      while (p < line.size() && line.at(p) == QLatin1Char('-')) ++p;
      if (p - i >= 2 && p < line.size() && line.at(p) == QLatin1Char('>')) {
        token.end = p + 1;
        token.raw = line.mid(i, token.end - i);
      } else if (p - i >= 3) {
        token.end = p;
        token.raw = line.mid(i, token.end - i);
        token.type = QStringLiteral("arrow_open");
      } else {
        ++i;
        continue;
      }
    } else if (ch == QLatin1Char('=')) {
      qsizetype p = i;
      while (p < line.size() && line.at(p) == QLatin1Char('=')) ++p;
      if (p - i < 2 || p >= line.size() || line.at(p) != QLatin1Char('>')) {
        ++i;
        continue;
      }
      token.end = p + 1;
      token.raw = line.mid(i, token.end - i);
    } else {
      ++i;
      continue;
    }

    if (token.raw.startsWith(QStringLiteral("-."))) {
      token.stroke = QStringLiteral("dotted");
      token.length = token.text.isEmpty() ? token.raw.count(QLatin1Char('.')) : 1;
    } else if (token.raw.startsWith(QLatin1Char('='))) {
      token.stroke = QStringLiteral("thick");
      token.length = token.raw.count(QLatin1Char('=')) - 1;
    } else if (token.raw.endsWith(QLatin1Char('>'))) {
      token.length = std::max<qsizetype>(1, token.raw.count(QLatin1Char('-')) - 1);
    } else if (token.raw.startsWith(QLatin1Char('o')) || token.raw.startsWith(QLatin1Char('x'))) {
      // Bidirectional: thick if the middle run is '=', else normal dashed.
      if (token.raw.contains(QLatin1Char('='))) {
        token.stroke = QStringLiteral("thick");
        token.length = token.raw.count(QLatin1Char('=')) - 1;
      } else {
        token.length = std::max<qsizetype>(1, token.raw.count(QLatin1Char('-')) - 1);
      }
    }
    result.push_back(std::move(token));
    i = result.last().end;
  }
  return result;
}

class Parser {
public:
  explicit Parser(FlowchartParseOptions options, FlowchartLimits limits) : options_(options), limits_(limits) {}

  FlowchartData parse(const QString& source) {
    if (source.size() > options_.maxTextSize) {
      throw FlowchartParseError(QStringLiteral("Maximum flowchart text size exceeded"), FlowchartErrorCategory::LimitExceeded);
    }
    QString normalized = source;
    normalized.replace(QRegularExpression(QStringLiteral("\\r\\n?")), QStringLiteral("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    bool header = false;
    for (const QString& rawLine : lines) {
      ++currentLine_;  // 1-based, counts every source line (incl. blank/%%) so positions match the input
      if (rawLine.size() > limits_.maxLineLength)
        throw FlowchartParseError(QStringLiteral("Maximum line length exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      const QString line = rawLine.trimmed();
      if (line.isEmpty() || line.startsWith(QStringLiteral("%%"))) continue;
      if (!header) {
        static const QRegularExpression graph(QStringLiteral(R"(^(?:flowchart|graph)\s+([^\s]+))"));
        const QRegularExpressionMatch match = graph.match(line);
        if (!match.hasMatch())
          throw FlowchartParseError(QStringLiteral("Expected flowchart header"), FlowchartErrorCategory::MissingHeader, currentLine_);
        direction_ = normalizedDirection(match.captured(1));
        header = true;
        continue;
      }
      parseStatement(line);
    }
    if (!contexts_.isEmpty())
      throw FlowchartParseError(QStringLiteral("Unclosed subgraph"), FlowchartErrorCategory::UnclosedSubgraph);
    FlowchartData result;
    result.direction = direction_;
    result.vertices = vertices_;
    result.edges = edges_;
    result.classes = classes_;
    result.subgraphs = subgraphs_;
    result.accTitle = accTitle_;
    result.accDescription = accDescription_;
    result.tooltips = tooltips_;
    return result;
  }

private:
  struct Context {
    QString id;
    QString title;
    QString labelType = QStringLiteral("text");
    QStringList nodes;
    QString dir;
    bool explicitDir = false;
  };

  Vertex& addVertex(const ParsedNode& parsed, bool applyDefinition = true) {
    auto found = vertexLookup_.find(parsed.id);
    const bool newlyCreated = found == vertexLookup_.end();
    if (newlyCreated) {
      if (parsed.id.size() > limits_.maxNodeIdLength)
        throw FlowchartParseError(QStringLiteral("Maximum node id length exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      if (vertices_.size() >= limits_.maxVertices)
        throw FlowchartParseError(QStringLiteral("Maximum vertex count exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      Vertex vertex;
      vertex.id = parsed.id;
      vertex.domId = QStringLiteral("flowchart-%1-%2").arg(parsed.id).arg(vertexCounter_);
      vertex.text = parsed.id;
      vertices_.push_back(std::move(vertex));
      found = vertexLookup_.insert(parsed.id, vertices_.size() - 1);
    }
    Vertex& vertex = vertices_[found.value()];
    ++vertexCounter_;
    const bool bareReference = !parsed.metadata && parsed.type.isEmpty() &&
                               parsed.classes.isEmpty() && parsed.text == parsed.id;
    if (applyDefinition && (newlyCreated || !bareReference)) {
      vertex.text = parsed.text;
      vertex.labelType = parsed.labelType;
      if (!parsed.type.isEmpty()) vertex.type = parsed.type;
      for (const QString& value : parsed.classes) {
        if (!vertex.classes.contains(value)) vertex.classes.push_back(value);
      }
    }
    return vertex;
  }

  QVector<ParsedNode> parseNodeGroup(const QString& source) {
    QVector<ParsedNode> result;
    for (const QString& item : splitGroups(source)) {
      ParsedNode node = parseNode(item, currentLine_);
      if (node.metadata) {
        ParsedNode bare;
        bare.id = node.id;
        bare.text = node.id;
        addVertex(bare);
      }
      addVertex(node);
      result.push_back(std::move(node));
    }
    return result;
  }

  void addLink(const ParsedNode& start, const ParsedNode& end, const LinkToken& token) {
    if (edges_.size() >= options_.maxEdges) {
      throw FlowchartParseError(QStringLiteral("Edge limit exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
    }
    Edge edge;
    edge.start = start.id;
    edge.end = end.id;
    edge.text = token.text;
    edge.stroke = token.stroke;
    edge.type = token.type;
    edge.length = token.length;
    if (token.raw == QLatin1String("---") || token.raw.endsWith(QStringLiteral("---"))) edge.type = QStringLiteral("arrow_open");
    if (token.raw == QLatin1String("--x")) edge.type = QStringLiteral("arrow_cross");
    if (token.raw == QLatin1String("--o")) edge.type = QStringLiteral("arrow_circle");
    if (token.raw == QLatin1String("<-->")) edge.type = QStringLiteral("double_arrow_point");
    if (!token.id.isEmpty()) {
      edge.id = token.id;
      edge.userDefinedId = true;
      edges_.push_back(std::move(edge));
      return;
    }
    const QString base = QStringLiteral("L_%1_%2_").arg(edge.start, edge.end);
    int count = 0;
    for (const Edge& existing : edges_) {
      if (existing.start == edge.start && existing.end == edge.end) ++count;
    }
    edge.id = base + QString::number(count == 0 ? 0 : count + 1);
    edges_.push_back(std::move(edge));
  }

  QStringList parseGraphStatement(const QString& line) {
    QVector<LinkToken> links = findLinks(line);
    if (links.isEmpty()) {
      const QVector<ParsedNode> nodes = parseNodeGroup(line);
      QStringList ids;
      for (const ParsedNode& node : nodes) ids.push_back(node.id);
      return ids;
    }

    QVector<QString> segments;
    qsizetype position = 0;
    for (const LinkToken& link : links) {
      segments.push_back(line.mid(position, link.start - position).trimmed());
      position = link.end;
    }
    segments.push_back(line.mid(position).trimmed());
    if (segments.size() != links.size() + 1)
      throw FlowchartParseError(QStringLiteral("Invalid link chain"), FlowchartErrorCategory::Syntax, currentLine_);
    for (qsizetype i = 0; i < links.size(); ++i) {
      static const QRegularExpression edgeId(QStringLiteral(R"((?:^|\s)([\w-]+)@$)"));
      const QRegularExpressionMatch idMatch = edgeId.match(segments[i]);
      if (idMatch.hasMatch()) {
        links[i].id = idMatch.captured(1);
        segments[i].chop(idMatch.capturedLength());
        segments[i] = segments[i].trimmed();
      }
    }

    QVector<QVector<ParsedNode>> groups;
    for (QString segment : segments) {
      if (segment.startsWith(QLatin1Char('|'))) {
        const qsizetype close = segment.indexOf(QLatin1Char('|'), 1);
        if (close > 0) segment = segment.mid(close + 1).trimmed();
      }
      groups.push_back(parseNodeGroup(segment));
    }
    for (qsizetype i = 0; i < links.size(); ++i) {
      LinkToken token = links.at(i);
      QString rightSource = segments.at(i + 1);
      if (rightSource.startsWith(QLatin1Char('|'))) {
        const qsizetype close = rightSource.indexOf(QLatin1Char('|'), 1);
        token.text = rightSource.mid(1, close - 1);
      }
      for (const ParsedNode& start : groups.at(i)) {
        for (const ParsedNode& end : groups.at(i + 1)) addLink(start, end, token);
      }
    }
    QStringList result;
    for (auto it = groups.crbegin(); it != groups.crend(); ++it) {
      for (const ParsedNode& node : *it) result.push_back(node.id);
    }
    return result;
  }

  void parseStatement(const QString& line) {
    if (line.startsWith(QStringLiteral("accTitle:"))) {
      accTitle_ = line.mid(9).trimmed();
      return;
    }
    if (line.startsWith(QStringLiteral("accDescr:"))) {
      accDescription_ = line.mid(9).trimmed();
      return;
    }
    const qsizetype edgeMetadataAt = line.indexOf(QStringLiteral("@{"));
    if (edgeMetadataAt > 0 && line.endsWith(QLatin1Char('}')) && findLinks(line).isEmpty()) {
      const QString id = line.left(edgeMetadataAt).trimmed();
      const QMap<QString, QString> fields =
          parseMetadataFields(line.mid(edgeMetadataAt + 2, line.size() - edgeMetadataAt - 3));
      for (FlowEdge& edge : edges_) {
        if (edge.id != id) continue;
        if (fields.contains(QStringLiteral("animate"))) {
          edge.hasAnimate = true;
          edge.animate = fields.value(QStringLiteral("animate")) == QLatin1String("true");
        }
        edge.animation = fields.value(QStringLiteral("animation"));
        edge.interpolate = fields.value(QStringLiteral("curve"));
        return;
      }
      // id is not an edge — fall through to node parsing. A line like
      // `A[Alpha]@{ shape: tri }` is a node with metadata, not edge metadata.
    }
    if (line.startsWith(QStringLiteral("subgraph "))) {
      QString definition = line.mid(9).trimmed();
      ParsedNode node = parseNode(definition, currentLine_);
      Context context;
      context.id = node.id;
      context.title = node.text;
      context.labelType = node.labelType;
      if (contexts_.size() >= limits_.maxSubgraphDepth)
        throw FlowchartParseError(QStringLiteral("Maximum subgraph nesting depth exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      contexts_.push_back(std::move(context));
      return;
    }
    if (line == QLatin1String("end")) {
      if (contexts_.isEmpty())
        throw FlowchartParseError(QStringLiteral("Unexpected end"), FlowchartErrorCategory::UnexpectedEnd, currentLine_);
      Context context = contexts_.takeLast();
      Subgraph subgraph;
      subgraph.id = context.id;
      subgraph.title = context.title;
      subgraph.nodes = context.nodes;
      subgraph.dir = context.dir;
      subgraph.hasExplicitDir = context.explicitDir;
      subgraph.labelType = context.labelType;
      if (subgraphs_.size() >= limits_.maxSubgraphs)
        throw FlowchartParseError(QStringLiteral("Maximum subgraph count exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      subgraphs_.push_back(std::move(subgraph));
      if (!contexts_.isEmpty()) contexts_.last().nodes.push_back(context.id);
      return;
    }
    if (line.startsWith(QStringLiteral("direction "))) {
      if (contexts_.isEmpty())
        throw FlowchartParseError(QStringLiteral("direction is only valid in a subgraph body"), FlowchartErrorCategory::InvalidDirective, currentLine_);
      contexts_.last().dir = line.mid(10).trimmed();
      contexts_.last().explicitDir = true;
      return;
    }
    if (line.startsWith(QStringLiteral("classDef "))) {
      const QString body = line.mid(9).trimmed();
      const qsizetype space = body.indexOf(QLatin1Char(' '));
      if (space < 0)
        throw FlowchartParseError(QStringLiteral("Invalid classDef"), FlowchartErrorCategory::InvalidDirective, currentLine_);
      FlowClass definition;
      definition.id = body.left(space);
      definition.styles = splitStyles(body.mid(space + 1));
      for (const QString& style : definition.styles) {
        if (style.contains(QStringLiteral("color"))) definition.textStyles.push_back(style);
      }
      if (classes_.size() >= limits_.maxClasses)
        throw FlowchartParseError(QStringLiteral("Maximum class count exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      classLookup_.insert(definition.id, classes_.size());
      classes_.push_back(std::move(definition));
      return;
    }
    if (line.startsWith(QStringLiteral("class "))) {
      const QString body = line.mid(6).trimmed();
      const qsizetype space = body.lastIndexOf(QLatin1Char(' '));
      if (space < 0)
        throw FlowchartParseError(QStringLiteral("Invalid class statement"), FlowchartErrorCategory::InvalidDirective, currentLine_);
      const QStringList ids = body.left(space).split(QLatin1Char(','), Qt::SkipEmptyParts);
      const QStringList names = body.mid(space + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
      for (const QString& id : ids) {
        ParsedNode node;
        node.id = id;
        node.text = id;
        Vertex& vertex = addVertex(node, false);
        for (const QString& name : names) if (!vertex.classes.contains(name)) vertex.classes.push_back(name);
      }
      return;
    }
    if (line.startsWith(QStringLiteral("style "))) {
      const QString body = line.mid(6).trimmed();
      const qsizetype space = body.indexOf(QLatin1Char(' '));
      ParsedNode node;
      node.id = body.left(space);
      node.text = node.id;
      Vertex& vertex = addVertex(node, false);
      const QStringList newStyles = splitStyles(body.mid(space + 1));
      if (vertex.styles.size() + newStyles.size() > limits_.maxStylesPerVertex)
        throw FlowchartParseError(QStringLiteral("Maximum styles per vertex exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
      vertex.styles += newStyles;
      return;
    }
    if (line.startsWith(QStringLiteral("linkStyle "))) {
      const QString body = line.mid(10).trimmed();
      const qsizetype space = body.indexOf(QLatin1Char(' '));
      const QStringList indices = body.left(space).split(QLatin1Char(','), Qt::SkipEmptyParts);
      // mermaid grammar: `linkStyle LIST interpolate:ID styles` — the optional
      // interpolate prefix sets the per-edge curve (updateLinkInterpolate);
      // the remainder is the style list (updateLink).
      QString styleSpec = body.mid(space + 1).trimmed();
      QString interpolate;
      // mermaid grammar: `linkStyle LIST interpolate ID styles` — the INTERPOLATE
      // keyword (bare word) followed by a space and the curve id, then optional
      // styles. Sets the per-edge curve (updateLinkInterpolate).
      if (styleSpec.startsWith(QStringLiteral("interpolate "))) {
        const QString after = styleSpec.mid(12);  // past "interpolate "
        const qsizetype gap = after.indexOf(QLatin1Char(' '));
        if (gap > 0) {
          interpolate = after.left(gap);
          styleSpec = after.mid(gap + 1).trimmed();
        } else {
          interpolate = after;
          styleSpec.clear();
        }
      }
      const QStringList styles = splitStyles(styleSpec);
      if (body.left(space) == QLatin1String("default")) return;
      for (const QString& indexText : indices) {
        bool ok = false;
        const int index = indexText.toInt(&ok);
        if (!ok || index < 0 || index >= edges_.size())
          throw FlowchartParseError(QStringLiteral("linkStyle index out of bounds"), FlowchartErrorCategory::LinkStyleBounds, currentLine_);
        if (!interpolate.isEmpty()) edges_[index].interpolate = interpolate;
        edges_[index].style = styles;
        if (!styles.isEmpty()) {
          bool hasFill = false;
          for (const QString& style : styles) if (style.startsWith(QStringLiteral("fill"))) hasFill = true;
          if (!hasFill) edges_[index].style.push_back(QStringLiteral("fill:none"));
        }
      }
      return;
    }
    if (line.startsWith(QStringLiteral("click "))) {
      const QString body = line.mid(6).trimmed();
      static const QRegularExpression href(
          QStringLiteral(R"regex(^([^\s]+)\s+href\s+"([^"]*)"(?:\s+"([^"]*)")?(?:\s+([^\s]+))?$)regex"));
      static const QRegularExpression callback(
          QStringLiteral(R"regex(^([^\s]+)\s+[^\s]+(?:\s+"([^"]*)")?$)regex"));
      QRegularExpressionMatch match = href.match(body);
      QString id;
      if (match.hasMatch()) {
        id = match.captured(1);
        auto found = vertexLookup_.constFind(id);
        if (found != vertexLookup_.constEnd()) {
          FlowVertex& vertex = vertices_[found.value()];
          QUrl url(match.captured(2));
          QString formatted = url.toString();
          if (!url.scheme().isEmpty() && !url.host().isEmpty() && url.path().isEmpty()) formatted += QLatin1Char('/');
          vertex.link = formatted;
          vertex.linkTarget = match.captured(4);
          // Flag (don't drop) unsafe URLs — the raw link stays for AST fidelity;
          // the render/export layer enforces via MermaidSecurityPolicy.
          vertex.linkUnsafe = !muffin::isSafeUrl(vertex.link, false);
          if (!vertex.classes.contains(QStringLiteral("clickable"))) vertex.classes.push_back(QStringLiteral("clickable"));
        }
        if (!match.captured(3).isEmpty()) {
          if (tooltips_.size() >= limits_.maxTooltips)
            throw FlowchartParseError(QStringLiteral("Maximum tooltip count exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
          tooltips_.insert(id, match.captured(3));
        }
        return;
      }
      match = callback.match(body);
      if (match.hasMatch()) {
        id = match.captured(1);
        auto found = vertexLookup_.constFind(id);
        if (found != vertexLookup_.constEnd() && !vertices_[found.value()].classes.contains(QStringLiteral("clickable"))) {
          vertices_[found.value()].classes.push_back(QStringLiteral("clickable"));
        }
        if (!match.captured(2).isEmpty()) {
          if (tooltips_.size() >= limits_.maxTooltips)
            throw FlowchartParseError(QStringLiteral("Maximum tooltip count exceeded"), FlowchartErrorCategory::LimitExceeded, currentLine_);
          tooltips_.insert(id, match.captured(2));
        }
        return;
      }
      throw FlowchartParseError(QStringLiteral("Invalid click statement"), FlowchartErrorCategory::InvalidDirective, currentLine_);
    }

    const QStringList nodes = parseGraphStatement(line);
    if (!contexts_.isEmpty()) {
      for (const QString& node : nodes) {
        if (!contexts_.last().nodes.contains(node)) contexts_.last().nodes.push_back(node);
      }
    }
  }

  QString direction_;
  QString accTitle_;
  QString accDescription_;
  FlowchartParseOptions options_;
  FlowchartLimits limits_;
  int vertexCounter_ = 0;
  int currentLine_ = 0;
  QVector<Vertex> vertices_;
  QMap<QString, qsizetype> vertexLookup_;
  QVector<Edge> edges_;
  QVector<FlowClass> classes_;
  QMap<QString, qsizetype> classLookup_;
  QVector<Subgraph> subgraphs_;
  QVector<Context> contexts_;
  QMap<QString, QString> tooltips_;
};

}  // namespace

namespace {
// Strip C0 control chars + DEL and truncate so a malicious source snippet
// embedded in an error message cannot inject ANSI escapes / forged log lines /
// terminal control sequences into logs or the UI (milestone H5 log-injection
// hardening). Applied at the FlowchartParseError constructors — the single
// chokepoint for every error message.
QString sanitizeErrorMessage(const QString& message) {
  QString out;
  out.reserve(message.size());
  for (const QChar c : message) {
    const ushort code = c.unicode();
    if (code < 0x20 || code == 0x7F) continue;  // drop C0 controls + DEL
    out += c;
  }
  constexpr qsizetype max = 200;
  return out.size() > max ? out.left(max) + QStringLiteral("…") : out;
}
}  // namespace

FlowchartParseError::FlowchartParseError(const QString& message)
    : std::runtime_error(sanitizeErrorMessage(message).toUtf8().constData()) {}

FlowchartParseError::FlowchartParseError(const QString& message, FlowchartErrorCategory category, int line, int column)
    : std::runtime_error(sanitizeErrorMessage(message).toUtf8().constData()), category_(category), line_(line), column_(column) {}

Flowchart Flowchart::parse(const QString& source, FlowchartParseOptions options, FlowchartLimits limits) {
  Flowchart result;
  result.data_ = Parser(options, limits).parse(source);
  return result;
}

const FlowchartData& Flowchart::data() const {
  return data_;
}

QJsonObject Flowchart::toJson() const {
  QJsonObject result;
  result.insert(QStringLiteral("direction"), data_.direction.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(data_.direction));
  result.insert(QStringLiteral("title"), data_.title);
  result.insert(QStringLiteral("accTitle"), data_.accTitle);
  result.insert(QStringLiteral("accDescription"), data_.accDescription);
  QJsonArray vertices;
  for (const FlowVertex& vertex : data_.vertices) vertices.push_back(vertexJson(vertex));
  result.insert(QStringLiteral("vertices"), vertices);
  QJsonArray edges;
  for (const FlowEdge& edge : data_.edges) edges.push_back(edgeJson(edge));
  result.insert(QStringLiteral("edges"), edges);
  QJsonArray classes;
  for (const FlowClass& value : data_.classes) {
    QJsonObject item;
    item.insert(QStringLiteral("id"), value.id);
    item.insert(QStringLiteral("styles"), strings(value.styles));
    item.insert(QStringLiteral("textStyles"), strings(value.textStyles));
    classes.push_back(item);
  }
  result.insert(QStringLiteral("classes"), classes);
  QJsonArray subgraphs;
  for (const FlowSubgraph& value : data_.subgraphs) {
    QJsonObject item;
    item.insert(QStringLiteral("id"), value.id);
    item.insert(QStringLiteral("nodes"), strings(value.nodes));
    item.insert(QStringLiteral("title"), value.title);
    item.insert(QStringLiteral("classes"), strings(value.classes));
    if (!value.dir.isEmpty()) item.insert(QStringLiteral("dir"), value.dir);
    item.insert(QStringLiteral("hasExplicitDir"), value.hasExplicitDir);
    item.insert(QStringLiteral("labelType"), value.labelType);
    subgraphs.push_back(item);
  }
  result.insert(QStringLiteral("subgraphs"), subgraphs);
  QJsonObject tooltips;
  for (auto it = data_.tooltips.constBegin(); it != data_.tooltips.constEnd(); ++it) tooltips.insert(it.key(), it.value());
  result.insert(QStringLiteral("tooltips"), tooltips);
  return result;
}

}  // namespace muffin::mermaid::flowchart
