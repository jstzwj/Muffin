#include "mermaid/wardley/WardleyDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace muffin::mermaid::wardley {
namespace {

bool horizontal(QChar c) {
  return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

QString collapseInline(QString value) {
  value = value.trimmed();
  value.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")),
                QStringLiteral(" "));
  return value;
}

QString normalizeBlock(QString value) {
  QStringList lines = value.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  for (QString &line : lines) {
    line.remove(QRegularExpression(QStringLiteral(R"(^\s*)")));
    line.remove(QRegularExpression(QStringLiteral(R"(\s+$)")));
    line.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")),
                 QStringLiteral(" "));
  }
  QString result = lines.join(QLatin1Char('\n'));
  result.replace(QRegularExpression(QStringLiteral(R"([\n\r]{2,})")),
                 QStringLiteral("\n"));
  return result;
}

QString unquote(const QString &value, bool *ok = nullptr) {
  if (ok) *ok = false;
  if (value.size() < 2 ||
      ((value.front() != QLatin1Char('\'') || value.back() != QLatin1Char('\'')) &&
       (value.front() != QLatin1Char('"') || value.back() != QLatin1Char('"'))))
    return value;
  const QChar quote = value.front();
  QString result;
  result.reserve(value.size() - 2);
  for (qsizetype i = 1; i + 1 < value.size(); ++i) {
    QChar c = value.at(i);
    if (c != QLatin1Char('\\')) {
      result += c;
      continue;
    }
    if (++i + 1 > value.size()) return {};
    c = value.at(i);
    switch (c.unicode()) {
    case 'b': result += QLatin1Char('\b'); break;
    case 'f': result += QLatin1Char('\f'); break;
    case 'n': result += QLatin1Char('\n'); break;
    case 'r': result += QLatin1Char('\r'); break;
    case 't': result += QLatin1Char('\t'); break;
    case 'v': result += QChar(0x000b); break;
    case '0': result += QChar(0); break;
    default: result += c; break;
    }
  }
  Q_UNUSED(quote);
  if (ok) *ok = true;
  return result;
}

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source_.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  WardleyData run() {
    skipHidden();
    if (source_.mid(index_, 12) != QLatin1String("wardley-beta"))
      parser(index_, QStringLiteral("Expected Wardley header"));
    index_ += 12;
    while (true) {
      skipHorizontal();
      if (index_ >= source_.size()) break;
      if (source_.at(index_) == QLatin1Char('\n')) {
        ++index_;
        continue;
      }
      if (source_.mid(index_, 2) == QLatin1String("%%")) {
        skipComment();
        continue;
      }
      const int start = index_;
      const QString line = takeLine();
      parseLine(line, start);
    }
    return data_;
  }

private:
  void skipHorizontal() {
    while (index_ < source_.size() && horizontal(source_.at(index_))) ++index_;
  }

  void skipComment() {
    while (index_ < source_.size() && source_.at(index_) != QLatin1Char('\n')) ++index_;
  }

  void skipHidden() {
    while (index_ < source_.size()) {
      const int before = index_;
      skipHorizontal();
      if (source_.mid(index_, 2) == QLatin1String("%%")) skipComment();
      while (index_ < source_.size() && source_.at(index_) == QLatin1Char('\n')) ++index_;
      if (before == index_) break;
    }
  }

  QString takeLine() {
    const int start = index_;
    bool quoted = false;
    QChar quote;
    bool escaped = false;
    while (index_ < source_.size()) {
      const QChar c = source_.at(index_);
      if (!quoted && c == QLatin1Char('\n')) break;
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == QLatin1Char('\\')) escaped = true;
        else if (c == quote) quoted = false;
      } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
        quoted = true;
        quote = c;
      }
      ++index_;
    }
    const QString result = source_.mid(start, index_ - start);
    if (index_ < source_.size()) ++index_;
    return result;
  }

  QString metadataValue(const QString &raw) const {
    return HtmlSanitizer().sanitizedMermaidText(collapseInline(raw));
  }

  void parseLine(QString line, int offset) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QStringLiteral("%%"))) return;
    if (line.startsWith(QStringLiteral("title")) &&
        (line.size() == 5 || horizontal(line.at(5)))) {
      const QString value = metadataValue(line.mid(5));
      if (!value.isEmpty()) {
        data_.title = value;
        data_.hasTitleDirective = true;
      }
      return;
    }
    if (line.startsWith(QStringLiteral("accTitle"))) {
      const int colon = line.indexOf(QLatin1Char(':'));
      if (colon < 0) lexer(offset, QStringLiteral("Invalid accTitle"));
      const QString value = metadataValue(line.mid(colon + 1));
      if (!value.isEmpty()) data_.accTitle = value;
      return;
    }
    if (line.startsWith(QStringLiteral("accDescr"))) {
      parseAccDescr(line, offset);
      return;
    }
    if (line.startsWith(QStringLiteral("size"))) return parseSize(line, offset);
    if (line.startsWith(QStringLiteral("evolution"))) return parseEvolution(line, offset);
    if (line.startsWith(QStringLiteral("anchor"))) return parseNode(line, offset, true);
    if (line.startsWith(QStringLiteral("component"))) return parseNode(line, offset, false);
    if (line.startsWith(QStringLiteral("evolve"))) return parseEvolve(line, offset);
    if (line.startsWith(QStringLiteral("pipeline"))) return parsePipeline(line, offset);
    if (line.startsWith(QStringLiteral("note"))) return parseNote(line, offset);
    if (line.startsWith(QStringLiteral("annotations"))) return parseAnnotationsBox(line, offset);
    if (line.startsWith(QStringLiteral("annotation"))) return parseAnnotation(line, offset);
    if (line.startsWith(QStringLiteral("accelerator"))) return parseAccelerator(line, offset, false);
    if (line.startsWith(QStringLiteral("deaccelerator"))) return parseAccelerator(line, offset, true);
    parseLink(line, offset);
  }

  void parseAccDescr(const QString &line, int offset) {
    const int colon = line.indexOf(QLatin1Char(':'));
    const int brace = line.indexOf(QLatin1Char('{'));
    QString value;
    if (colon >= 0 && (brace < 0 || colon < brace)) {
      value = collapseInline(line.mid(colon + 1));
    } else if (brace >= 0) {
      QString content = line.mid(brace + 1);
      while (!content.contains(QLatin1Char('}'))) {
        if (index_ >= source_.size()) lexer(offset, QStringLiteral("Unterminated accDescr"));
        const int next = index_;
        content += QLatin1Char('\n') + takeLine();
        Q_UNUSED(next);
      }
      content.truncate(content.indexOf(QLatin1Char('}')));
      value = normalizeBlock(content);
    } else {
      lexer(offset, QStringLiteral("Invalid accDescr"));
    }
    value = HtmlSanitizer().sanitizedMermaidText(value);
    if (!value.isEmpty()) data_.accDescr = value;
  }

  static bool fullMatch(const QString &value, const QString &pattern,
                        QRegularExpressionMatch *match = nullptr) {
    const QRegularExpression re(pattern);
    const auto found = re.match(value);
    if (!found.hasMatch()) return false;
    if (match) *match = found;
    return true;
  }

  void parseSize(const QString &line, int offset) {
    QRegularExpressionMatch match;
    if (!fullMatch(line, QStringLiteral(R"(^size\s*\[(0|[1-9][0-9]*),(0|[1-9][0-9]*)\]\s*$)"), &match))
      parser(offset + 6, QStringLiteral("Invalid Wardley size"));
    data_.size = QSizeF(match.captured(1).toDouble(), match.captured(2).toDouble());
  }

  QString parseName(QString value, int offset) const {
    value = value.trimmed();
    bool ok = false;
    const QString decoded = unquote(value, &ok);
    if (ok) return decoded;
    if (value.isEmpty() ||
        !QRegularExpression(QStringLiteral(R"(^[A-Za-z](?:[A-Za-z0-9_()&]|-(?!>))*(?:[ \t]+[A-Za-z(](?:[A-Za-z0-9_()&]|-(?!>))*)*$)"))
             .match(value).hasMatch())
      parser(offset, QStringLiteral("Invalid Wardley name"));
    return value;
  }

  qreal wardleyNumber(const QString &value, int offset, const QString &context) const {
    if (!QRegularExpression(QStringLiteral(R"(^[0-9]+\.[0-9]+$)"))
             .match(value.trimmed()).hasMatch())
      parser(offset, QStringLiteral("Expected Wardley decimal"));
    const qreal number = value.toDouble();
    const qreal normalized = number <= 1.0 ? number * 100.0 : number;
    if (normalized < 0.0 || normalized > 100.0)
      runtime(QStringLiteral("%1 must be between 0-1 (decimal) or 0-100 (percentage). Received: %2")
                  .arg(context, value));
    return normalized;
  }

  qreal coordinateNumber(const QString &value, int offset,
                         const QString &context) const {
    const QString trimmed = value.trimmed();
    if (!QRegularExpression(QStringLiteral(R"(^(?:[0-9]+\.[0-9]+|0|[1-9][0-9]*)$)"))
             .match(trimmed).hasMatch())
      parser(offset, QStringLiteral("Invalid Wardley coordinate"));
    const qreal number = trimmed.toDouble();
    const qreal normalized = number <= 1.0 ? number * 100.0 : number;
    if (normalized < 0.0 || normalized > 100.0)
      runtime(QStringLiteral("%1 must be between 0-1 (decimal) or 0-100 (percentage). Received: %2")
                  .arg(context, trimmed));
    return normalized;
  }

  void parseEvolution(const QString &line, int offset) {
    const QString body = line.mid(9).trimmed();
    const QStringList parts = body.split(QStringLiteral("->"));
    if (parts.size() < 2) parser(offset + line.size(), QStringLiteral("Evolution requires stages"));
    data_.axes.stages.clear();
    data_.axes.stageBoundaries.clear();
    for (QString part : parts) {
      part = part.trimmed();
      QString second;
      const int slash = part.indexOf(QLatin1Char('/'));
      if (slash >= 0) {
        second = parseName(part.mid(slash + 1), offset);
        part = part.left(slash).trimmed();
      }
      const int at = part.lastIndexOf(QLatin1Char('@'));
      if (at >= 0) {
        const QString boundary = part.mid(at + 1).trimmed();
        if (!QRegularExpression(QStringLiteral(R"(^[0-9]+\.[0-9]+$)"))
                 .match(boundary).hasMatch())
          parser(offset + at + 10, QStringLiteral("Invalid stage boundary"));
        data_.axes.stageBoundaries.append(boundary.toDouble());
        part = part.left(at).trimmed();
      }
      QString name = parseName(part, offset);
      if (!second.isEmpty()) name += QStringLiteral(" / ") + second;
      data_.axes.stages.append(name);
    }
  }

  struct NodeParts {
    QString name;
    QString first;
    QString second;
    QString rest;
  };

  NodeParts bracketParts(const QString &line, const QString &keyword, int offset,
                         bool twoValues) const {
    const int open = line.indexOf(QLatin1Char('['), keyword.size());
    const int close = open < 0 ? -1 : line.indexOf(QLatin1Char(']'), open + 1);
    if (open < 0 || close < 0) parser(offset, QStringLiteral("Missing Wardley coordinates"));
    NodeParts parts;
    parts.name = parseName(line.mid(keyword.size(), open - keyword.size()), offset + keyword.size());
    const QStringList coordinates = line.mid(open + 1, close - open - 1).split(QLatin1Char(','));
    if (coordinates.size() != (twoValues ? 2 : 1))
      parser(offset + open, QStringLiteral("Invalid Wardley coordinates"));
    parts.first = coordinates.at(0).trimmed();
    if (twoValues) parts.second = coordinates.at(1).trimmed();
    parts.rest = line.mid(close + 1).trimmed();
    return parts;
  }

  void addNode(WardleyNode node) {
    const auto it = nodeIndexes_.constFind(node.id);
    if (it == nodeIndexes_.cend()) {
      nodeIndexes_.insert(node.id, data_.nodes.size());
      data_.nodes.append(std::move(node));
      return;
    }
    WardleyNode &existing = data_.nodes[*it];
    const auto x = node.labelOffsetX ? node.labelOffsetX : existing.labelOffsetX;
    const auto y = node.labelOffsetY ? node.labelOffsetY : existing.labelOffsetY;
    existing = std::move(node);
    existing.labelOffsetX = x;
    existing.labelOffsetY = y;
  }

  void parseNode(QString line, int offset, bool anchor) {
    const QString keyword = anchor ? QStringLiteral("anchor") : QStringLiteral("component");
    const NodeParts parts = bracketParts(line, keyword, offset, true);
    WardleyNode node;
    node.id = parts.name;
    node.label = parts.name;
    const int coordinatesOpen = line.indexOf(QLatin1Char('['), keyword.size());
    const int firstOffset = offset + line.indexOf(parts.first, coordinatesOpen + 1);
    const int secondOffset = offset + line.indexOf(parts.second,
                                                    coordinatesOpen + 1 + parts.first.size());
    node.x = wardleyNumber(parts.second, secondOffset,
                           QStringLiteral("%1 \"%2\" evolution")
                               .arg(anchor ? QStringLiteral("Anchor") : QStringLiteral("Component"), parts.name));
    node.y = wardleyNumber(parts.first, firstOffset,
                           QStringLiteral("%1 \"%2\" visibility")
                               .arg(anchor ? QStringLiteral("Anchor") : QStringLiteral("Component"), parts.name));
    node.className = anchor ? QStringLiteral("anchor") : QStringLiteral("component");
    QString rest = parts.rest;
    if (!anchor && rest.startsWith(QStringLiteral("label"))) {
      QRegularExpressionMatch match;
      const QRegularExpression re(QStringLiteral(R"(^label\s*\[(-?)(0|[1-9][0-9]*),(-?)(0|[1-9][0-9]*)\]\s*)"));
      match = re.match(rest);
      if (!match.hasMatch()) parser(offset, QStringLiteral("Invalid Wardley label offset"));
      node.labelOffsetX = (match.captured(1).isEmpty() ? 1.0 : -1.0) * match.captured(2).toDouble();
      node.labelOffsetY = (match.captured(3).isEmpty() ? 1.0 : -1.0) * match.captured(4).toDouble();
      rest = rest.mid(match.capturedLength()).trimmed();
    }
    if (!anchor && rest.startsWith(QLatin1Char('('))) {
      const int close = rest.indexOf(QLatin1Char(')'));
      if (close < 0) parser(offset, QStringLiteral("Invalid Wardley decorator"));
      const QString decorator = rest.mid(1, close - 1).trimmed();
      static const QStringList strategies{QStringLiteral("build"), QStringLiteral("buy"),
                                          QStringLiteral("outsource"), QStringLiteral("market")};
      if (strategies.contains(decorator)) node.sourceStrategy = decorator;
      else if (decorator == QLatin1String("inertia")) node.inertia = true;
      else parser(offset + line.indexOf(decorator), QStringLiteral("Invalid Wardley decorator"));
      rest = rest.mid(close + 1).trimmed();
    }
    if (!anchor && rest == QLatin1String("inertia")) {
      node.inertia = true;
      rest.clear();
    }
    if (!rest.isEmpty()) {
      if (rest.contains(QLatin1Char(';'))) lexer(offset + line.indexOf(QLatin1Char(';')), QStringLiteral("Invalid Wardley token"));
      parser(offset + line.size() - rest.size(), QStringLiteral("Unexpected Wardley node input"));
    }
    addNode(std::move(node));
  }

  int nodeIndex(const QString &id) const { return nodeIndexes_.value(id, -1); }

  QString resolveNodeId(const QString &name) const {
    if (nodeIndexes_.contains(name)) return name;
    for (const WardleyNode &node : data_.nodes)
      if (node.label == name) return node.id;
    return name;
  }

  void parseEvolve(const QString &line, int offset) {
    QRegularExpressionMatch match;
    const QRegularExpression re(QStringLiteral(R"(^evolve\s+(.+?)\s+([0-9]+\.[0-9]+)\s*$)"));
    match = re.match(line);
    if (!match.hasMatch()) parser(offset + 7, QStringLiteral("Invalid evolve statement"));
    const QString component = parseName(match.captured(1), offset + 7);
    const int index = nodeIndex(component);
    if (index < 0) return;
    const qreal target = wardleyNumber(match.captured(2), offset,
                                       QStringLiteral("Evolve target for \"%1\"").arg(component));
    const auto existing = std::find_if(data_.trends.begin(), data_.trends.end(),
                                       [&](const WardleyTrend &trend) { return trend.nodeId == component; });
    WardleyTrend trend{component, target, data_.nodes.at(index).y};
    if (existing == data_.trends.end()) data_.trends.append(trend);
    else *existing = trend;
  }

  void parsePipeline(const QString &line, int offset) {
    const int brace = line.indexOf(QLatin1Char('{'));
    if (brace < 0) parser(offset, QStringLiteral("Invalid pipeline"));
    const QString parent = parseName(line.mid(8, brace - 8), offset + 8);
    const int parentIndex = nodeIndex(parent);
    if (parentIndex < 0)
      runtime(QStringLiteral("Pipeline \"%1\" must reference an existing component with coordinates.").arg(parent));
    WardleyPipeline pipeline;
    pipeline.nodeId = parent;
    data_.nodes[parentIndex].isPipelineParent = true;
    int componentCount = 0;
    int closingOffset = index_;
    while (index_ < source_.size()) {
      skipHorizontal();
      if (index_ < source_.size() && source_.at(index_) == QLatin1Char('\n')) { ++index_; continue; }
      const int childOffset = index_;
      QString child = takeLine().trimmed();
      if (child == QLatin1String("}")) {
        closingOffset = childOffset;
        break;
      }
      if (child.isEmpty() || child.startsWith(QStringLiteral("%%"))) continue;
      if (!child.startsWith(QStringLiteral("component")))
        parser(childOffset, QStringLiteral("Expected pipeline component"));
      NodeParts parts = bracketParts(child, QStringLiteral("component"), childOffset, false);
      WardleyNode node;
      node.id = parent + QLatin1Char('_') + parts.name;
      node.label = parts.name;
      node.x = wardleyNumber(parts.first, childOffset,
                             QStringLiteral("Pipeline component \"%1\" evolution").arg(parts.name));
      node.y = data_.nodes.at(parentIndex).y;
      node.className = QStringLiteral("pipeline-component");
      node.inPipeline = true;
      QString rest = parts.rest;
      if (rest.startsWith(QStringLiteral("label"))) {
        QRegularExpressionMatch lm;
        const QRegularExpression lr(QStringLiteral(R"(^label\s*\[(-?)(0|[1-9][0-9]*),(-?)(0|[1-9][0-9]*)\]\s*$)"));
        lm = lr.match(rest);
        if (!lm.hasMatch()) parser(childOffset, QStringLiteral("Invalid pipeline label"));
        node.labelOffsetX = (lm.captured(1).isEmpty() ? 1.0 : -1.0) * lm.captured(2).toDouble();
        node.labelOffsetY = (lm.captured(3).isEmpty() ? 1.0 : -1.0) * lm.captured(4).toDouble();
      } else if (!rest.isEmpty()) {
        parser(childOffset, QStringLiteral("Unexpected pipeline component input"));
      }
      addNode(node);
      pipeline.componentIds.append(node.id);
      ++componentCount;
    }
    if (!componentCount)
      parser(closingOffset, QStringLiteral("Pipeline requires components"));
    data_.pipelines.append(std::move(pipeline));
  }

  void parseNote(const QString &line, int offset) {
    const int open = line.lastIndexOf(QLatin1Char('['));
    const int close = line.lastIndexOf(QLatin1Char(']'));
    if (open < 0 || close < open) parser(offset, QStringLiteral("Invalid note"));
    bool ok = false;
    const QString text = unquote(line.mid(4, open - 4).trimmed(), &ok);
    if (!ok) parser(offset + 5, QStringLiteral("Expected quoted note"));
    const QStringList coords = line.mid(open + 1, close - open - 1).split(QLatin1Char(','));
    if (coords.size() != 2) parser(offset + open, QStringLiteral("Invalid note coordinates"));
    const qreal visibility = wardleyNumber(coords.at(0), offset, QStringLiteral("Note \"%1\" visibility").arg(text));
    const qreal evolution = wardleyNumber(coords.at(1), offset, QStringLiteral("Note \"%1\" evolution").arg(text));
    data_.notes.append({text, QPointF(evolution, visibility)});
  }

  void parseAnnotationsBox(const QString &line, int offset) {
    QRegularExpressionMatch match;
    const QRegularExpression re(QStringLiteral(R"(^annotations\s*\[([^,]+),([^\]]+)\]\s*$)"));
    match = re.match(line);
    if (!match.hasMatch()) parser(offset, QStringLiteral("Invalid annotations box"));
    const qreal first = coordinateNumber(match.captured(1), offset, QStringLiteral("Annotations box visibility"));
    const qreal second = coordinateNumber(match.captured(2), offset, QStringLiteral("Annotations box evolution"));
    data_.annotationsBox = QPointF(second, first);
  }

  void parseAnnotation(const QString &line, int offset) {
    QRegularExpressionMatch match;
    const QRegularExpression re(QStringLiteral(R"(^annotation\s+(0|[1-9][0-9]*)\s*,\s*\[([^,]+),([^\]]+)\]\s*(.+)\s*$)"));
    match = re.match(line);
    if (!match.hasMatch()) parser(offset, QStringLiteral("Invalid annotation"));
    bool ok = false;
    const QString text = unquote(match.captured(4).trimmed(), &ok);
    if (!ok) parser(offset, QStringLiteral("Expected quoted annotation text"));
    const qreal first = coordinateNumber(match.captured(2), offset,
                                         QStringLiteral("Annotation %1 visibility").arg(match.captured(1)));
    const qreal second = coordinateNumber(match.captured(3), offset,
                                          QStringLiteral("Annotation %1 evolution").arg(match.captured(1)));
    data_.annotations.append({match.captured(1).toInt(), QPointF(second, first), text, true});
  }

  void parseAccelerator(const QString &line, int offset, bool deaccelerator) {
    const QString keyword = deaccelerator ? QStringLiteral("deaccelerator") : QStringLiteral("accelerator");
    const NodeParts parts = bracketParts(line, keyword, offset, true);
    const int open = line.indexOf(QLatin1Char('['), keyword.size());
    const int firstOffset = offset + line.indexOf(parts.first, open + 1);
    const int secondOffset = offset + line.indexOf(parts.second,
                                                    open + 1 + parts.first.size());
    const qreal first = wardleyNumber(parts.first, firstOffset,
                                     QStringLiteral("%1 \"%2\" visibility").arg(keyword, parts.name));
    const qreal second = wardleyNumber(parts.second, secondOffset,
                                      QStringLiteral("%1 \"%2\" evolution").arg(keyword, parts.name));
    WardleyAccelerator accelerator{parts.name, QPointF(second, first)};
    if (deaccelerator) data_.deaccelerators.append(std::move(accelerator));
    else data_.accelerators.append(std::move(accelerator));
  }

  void parseLink(QString line, int offset) {
    QString annotation;
    bool hasAnnotation = false;
    const int semicolon = line.indexOf(QLatin1Char(';'));
    if (semicolon >= 0) {
      annotation = line.mid(semicolon + 1).trimmed();
      hasAnnotation = !annotation.isEmpty();
      line = line.left(semicolon).trimmed();
    }
    struct Separator { QString token; bool dashed; QString flow; };
    QVector<Separator> separators;
    const QRegularExpression flowRe(
        QStringLiteral(R"(\+'([^']*)'(<>|<|>))"));
    auto flowMatch = flowRe.match(line);
    int split = -1;
    int length = 0;
    QString flow;
    QString flowLabel;
    bool dashed = false;
    if (flowMatch.hasMatch()) {
      split = flowMatch.capturedStart();
      length = flowMatch.capturedLength();
      flowLabel = flowMatch.captured(1);
      const QString arrow = flowMatch.captured(2);
      flow = arrow == QLatin1String("<>") ? QStringLiteral("bidirectional")
           : arrow == QLatin1String("<") ? QStringLiteral("backward")
                                           : QStringLiteral("forward");
    } else {
      const QVector<Separator> candidates{
          {QStringLiteral("-.->"), true, {}}, {QStringLiteral("-->"), false, {}},
          {QStringLiteral("->"), false, {}}, {QStringLiteral("+<>"), false, QStringLiteral("bidirectional")},
          {QStringLiteral("+>"), false, QStringLiteral("forward")},
          {QStringLiteral("+<"), false, QStringLiteral("backward")}};
      for (const Separator &candidate : candidates) {
        split = line.indexOf(candidate.token);
        if (split >= 0) {
          length = candidate.token.size();
          dashed = candidate.dashed;
          flow = candidate.flow;
          break;
        }
      }
    }
    if (split < 0) {
      if (line.contains(QLatin1Char(';'))) lexer(offset, QStringLiteral("Invalid Wardley token"));
      parser(offset + line.size(), QStringLiteral("Invalid Wardley statement"));
    }
    const QString from = parseName(line.left(split), offset);
    const QString to = parseName(line.mid(split + length), offset + split + length);
    WardleyLink link;
    link.source = resolveNodeId(from);
    link.target = resolveNodeId(to);
    link.dashed = dashed;
    link.flow = flow;
    link.hasLabel = !flowLabel.isNull() || hasAnnotation;
    link.label = !flowLabel.isNull() ? flowLabel : annotation;
    data_.links.append(std::move(link));
  }

  QPair<int, int> location(int offset) const {
    int line = 1;
    int column = 1;
    for (int i = 0; i < qMin(offset, source_.size()); ++i) {
      if (source_.at(i) == QLatin1Char('\n')) { ++line; column = 1; }
      else ++column;
    }
    return {line, column};
  }

  [[noreturn]] void parser(int offset, const QString &message) const {
    const auto [line, column] = location(offset);
    throw WardleyParseError(WardleyErrorKind::Parser, line, column,
                            offset < source_.size() ? source_.mid(offset, 1) : QStringLiteral("EOF"),
                            message);
  }

  [[noreturn]] void lexer(int offset, const QString &message) const {
    const auto [line, column] = location(offset);
    throw WardleyParseError(WardleyErrorKind::Lexer, line, column,
                            offset < source_.size() ? source_.mid(offset, 1) : QStringLiteral("EOF"),
                            message);
  }

  [[noreturn]] void runtime(const QString &message) const {
    throw WardleyParseError(WardleyErrorKind::Runtime, 0, 0, {}, message);
  }

  QString source_;
  int index_ = 0;
  WardleyData data_;
  QHash<QString, int> nodeIndexes_;
};

} // namespace

WardleyParseError::WardleyParseError(WardleyErrorKind errorKind, int errorLine,
                                     int errorColumn, QString errorToken,
                                     const QString &message)
    : std::runtime_error(message.toStdString()), kind(errorKind), line(errorLine),
      column(errorColumn), token(std::move(errorToken)) {}

WardleyData WardleyDiagram::parse(const QString &source) {
  return Parser(source).run();
}

} // namespace muffin::mermaid::wardley
