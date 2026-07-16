#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartTokenizer.h"

#include "blocks/html/HtmlUrlSafety.h"

#include <QJsonArray>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <optional>

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
  QJsonObject props;
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

QString sanitizeDbLabel(QString text) {
  static const QStringList allowed = {
      QStringLiteral("<b>"), QStringLiteral("</b>"),
      QStringLiteral("<strong>"), QStringLiteral("</strong>"),
      QStringLiteral("<i>"), QStringLiteral("</i>"),
      QStringLiteral("<em>"), QStringLiteral("</em>"),
      QStringLiteral("<code>"), QStringLiteral("</code>"),
      QStringLiteral("<br>"), QStringLiteral("<br/>"), QStringLiteral("<br />")};
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) != QLatin1Char('<')) continue;
    const qsizetype close = text.indexOf(QLatin1Char('>'), i + 1);
    const QString candidate = close < 0 ? QString() : text.mid(i, close - i + 1).toLower();
    if (allowed.contains(candidate)) {
      i = close;
      continue;
    }
    text.replace(i, 1, QStringLiteral("&lt;"));
    i += 3;
  }
  return text;
}

QString tokenSource(const FlowToken& token) {
  switch (token.kind) {
    case FlowTokenKind::Str: return QLatin1Char('"') + token.text + QLatin1Char('"');
    case FlowTokenKind::MarkdownStr:
      return QStringLiteral("\"`") + token.text + QStringLiteral("`\"");
    case FlowTokenKind::ShapeData:
      return QStringLiteral("@{") + token.text + QLatin1Char('}');
    default: return token.text;
  }
}

QString tokenText(const QVector<FlowToken>& tokens, qsizetype first, qsizetype last) {
  QString result;
  for (qsizetype i = first; i < last; ++i) result += tokenSource(tokens.at(i));
  return result;
}

QString diagnosticToken(const FlowToken& token) {
  if (token.kind == FlowTokenKind::Eof) return QStringLiteral("end of input");
  QString text = token.text;
  text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
  text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
  text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
  if (text.size() > 32) text = text.left(32) + QStringLiteral("...");
  return text.isEmpty() ? flowTokenName(token.kind)
                        : QStringLiteral("%1 \"%2\"").arg(flowTokenName(token.kind), text);
}

FlowchartSourceSpan tokenSpan(const FlowToken& token) {
  return {token.offset, std::max<qsizetype>(1, token.text.size()), token.line, token.column};
}

FlowchartParseError tokenError(const FlowToken& token, FlowchartErrorCategory category,
                               FlowchartErrorStage stage, FlowchartErrorCode code,
                               QStringView production = {}, QStringList expected = {}) {
  FlowchartDiagnostic diagnostic;
  diagnostic.category = category;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.span = tokenSpan(token);
  diagnostic.production = production.toString();
  diagnostic.actual = diagnosticToken(token);
  diagnostic.expected = std::move(expected);
  return FlowchartParseError(std::move(diagnostic));
}

class TokenCursor {
public:
  explicit TokenCursor(const QString& source, int startingLine = 1,
                       int startingColumn = 1, qsizetype startingOffset = 0)
      : tokens_(FlowchartTokenizer(source, false).tokenize()) {
    for (FlowToken& token : tokens_) {
      token.offset += startingOffset;
      if (token.line == 1) token.column += startingColumn - 1;
      token.line += startingLine - 1;
    }
  }

  const FlowToken& peek(qsizetype lookahead = 0) const {
    const qsizetype index = position_ + lookahead;
    return tokens_.at(std::min(index, tokens_.size() - 1));
  }

  bool atEnd() const { return peek().kind == FlowTokenKind::Eof; }

  bool consume(FlowTokenKind kind, FlowToken* token = nullptr) {
    if (peek().kind != kind) return false;
    if (token) *token = peek();
    ++position_;
    return true;
  }

  FlowToken consume() {
    if (atEnd()) return peek();
    return tokens_.at(position_++);
  }

  FlowToken expect(FlowTokenKind kind, QStringView production) {
    FlowToken token;
    if (consume(kind, &token)) return token;
    const FlowToken& actual = peek();
    throw tokenError(actual, FlowchartErrorCategory::InvalidDirective,
                     FlowchartErrorStage::Parser,
                     actual.kind == FlowTokenKind::Eof ? FlowchartErrorCode::MissingToken
                                                       : FlowchartErrorCode::UnexpectedToken,
                     production, {flowTokenName(kind)});
  }

  bool skipSpace() {
    bool consumed = false;
    while (consume(FlowTokenKind::Space)) consumed = true;
    return consumed;
  }

  void expectEnd(QStringView production) {
    if (atEnd()) return;
    const FlowToken& actual = peek();
    throw tokenError(actual, FlowchartErrorCategory::InvalidDirective,
                     FlowchartErrorStage::Parser, FlowchartErrorCode::UnexpectedToken,
                     production, {QStringLiteral("end of input")});
  }

  QString consumeUntil(std::initializer_list<FlowTokenKind> stops) {
    QString result;
    while (!atEnd() && std::find(stops.begin(), stops.end(), peek().kind) == stops.end())
      result += tokenSource(tokens_.at(position_++));
    return result;
  }

  QStringList parseList(FlowTokenKind separator,
                        std::initializer_list<FlowTokenKind> stops = {FlowTokenKind::Eof}) {
    QStringList result;
    while (true) {
      const QString value = consumeUntil([&] {
        QVector<FlowTokenKind> delimiters(stops.begin(), stops.end());
        delimiters.push_back(separator);
        return delimiters;
      }());
      if (value.isEmpty())
        throw tokenError(peek(), FlowchartErrorCategory::InvalidDirective,
                         FlowchartErrorStage::Parser, FlowchartErrorCode::MissingListItem);
      result.push_back(value);
      if (!consume(separator)) break;
    }
    return result;
  }

private:
  QString consumeUntil(const QVector<FlowTokenKind>& stops) {
    QString result;
    while (!atEnd() && !stops.contains(peek().kind)) result += tokenSource(tokens_.at(position_++));
    return result;
  }

  QVector<FlowToken> tokens_;
  qsizetype position_ = 0;
};

FlowTokenKind firstSignificantKind(const QString& source) {
  FlowchartTokenizer tokenizer(source, false);
  for (;;) {
    const FlowToken token = tokenizer.next();
    if (token.kind != FlowTokenKind::Space) return token.kind;
  }
}

struct TokenShape {
  FlowTokenKind start;
  QVector<FlowTokenKind> prefix;
  QVector<FlowTokenKind> suffix;
  QString type;
};

bool isIdStringToken(FlowTokenKind kind) {
  switch (kind) {
    case FlowTokenKind::Amp:
    case FlowTokenKind::Colon:
    case FlowTokenKind::Down:
    case FlowTokenKind::Default:
    case FlowTokenKind::Number:
    case FlowTokenKind::Comma:
    case FlowTokenKind::NodeString:
    case FlowTokenKind::Bracket:
    case FlowTokenKind::Minus:
    case FlowTokenKind::Multiply:
    case FlowTokenKind::UnicodeText:
      return true;
    default:
      return false;
  }
}

bool isStyleComponent(FlowTokenKind kind) {
  switch (kind) {
    case FlowTokenKind::Space:
    case FlowTokenKind::Colon:
    case FlowTokenKind::Style:
    case FlowTokenKind::Number:
    case FlowTokenKind::NodeString:
    case FlowTokenKind::Unit:
    case FlowTokenKind::Bracket:
    case FlowTokenKind::Percent:
      return true;
    default:
      return false;
  }
}

bool isTextNoTagsToken(FlowTokenKind kind) {
  switch (kind) {
    case FlowTokenKind::Space:
    case FlowTokenKind::Graph:
    case FlowTokenKind::Dir:
    case FlowTokenKind::Subgraph:
    case FlowTokenKind::End:
    case FlowTokenKind::Style:
    case FlowTokenKind::LinkStyle:
    case FlowTokenKind::ClassDef:
    case FlowTokenKind::Class:
    case FlowTokenKind::Click:
    case FlowTokenKind::Down:
    case FlowTokenKind::Up:
    case FlowTokenKind::Default:
    case FlowTokenKind::Amp:
    case FlowTokenKind::Colon:
    case FlowTokenKind::Number:
    case FlowTokenKind::Comma:
    case FlowTokenKind::NodeString:
    case FlowTokenKind::Bracket:
    case FlowTokenKind::Minus:
    case FlowTokenKind::Multiply:
    case FlowTokenKind::UnicodeText:
      return true;
    default:
      return false;
  }
}

ParsedNode parseNode(QString source, int line = 0, int column = 1, qsizetype offset = 0) {
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
    node = parseNode(source.left(metadataAt).trimmed(), line, column, offset);
    node.metadata = true;
    if (fields.contains(QStringLiteral("label"))) {
      node.text = fields.value(QStringLiteral("label"));
      node.labelType = QStringLiteral("markdown");
    }
    if (fields.contains(QStringLiteral("shape"))) node.type = fields.value(QStringLiteral("shape"));
    return node;
  }

  QVector<FlowToken> tokens = FlowchartTokenizer(source, false).tokenize();
  for (FlowToken& token : tokens) {
    token.offset += offset;
    if (token.line == 1) token.column += column - 1;
    token.line += line - 1;
  }
  if (!tokens.isEmpty() && tokens.last().kind == FlowTokenKind::Eof) tokens.removeLast();
  while (!tokens.isEmpty() && tokens.first().kind == FlowTokenKind::Space) tokens.removeFirst();
  while (!tokens.isEmpty() && tokens.last().kind == FlowTokenKind::Space) tokens.removeLast();

  static const QVector<TokenShape> shapes = {
      {FlowTokenKind::DoubleCircleStart, {FlowTokenKind::DoubleCircleStart}, {FlowTokenKind::DoubleCircleEnd}, QStringLiteral("doublecircle")},
      {FlowTokenKind::StadiumStart, {FlowTokenKind::StadiumStart}, {FlowTokenKind::StadiumEnd}, QStringLiteral("stadium")},
      {FlowTokenKind::SubroutineStart, {FlowTokenKind::SubroutineStart}, {FlowTokenKind::SubroutineEnd}, QStringLiteral("subroutine")},
      {FlowTokenKind::CylinderStart, {FlowTokenKind::CylinderStart}, {FlowTokenKind::CylinderEnd}, QStringLiteral("cylinder")},
      {FlowTokenKind::EllipseStart, {FlowTokenKind::EllipseStart}, {FlowTokenKind::EllipseEnd}, QStringLiteral("ellipse")},
      {FlowTokenKind::TrapStart, {FlowTokenKind::TrapStart}, {FlowTokenKind::TrapEnd}, QStringLiteral("trapezoid")},
      {FlowTokenKind::InvTrapStart, {FlowTokenKind::InvTrapStart}, {FlowTokenKind::InvTrapEnd}, QStringLiteral("inv_trapezoid")},
      {FlowTokenKind::TrapStart, {FlowTokenKind::TrapStart}, {FlowTokenKind::InvTrapEnd}, QStringLiteral("lean_right")},
      {FlowTokenKind::InvTrapStart, {FlowTokenKind::InvTrapStart}, {FlowTokenKind::TrapEnd}, QStringLiteral("lean_left")},
      {FlowTokenKind::TagEnd, {FlowTokenKind::TagEnd}, {FlowTokenKind::Sqe}, QStringLiteral("odd")},
      {FlowTokenKind::Sqs, {FlowTokenKind::Sqs}, {FlowTokenKind::Sqe}, QStringLiteral("square")},
      {FlowTokenKind::Ps, {FlowTokenKind::Ps, FlowTokenKind::Ps}, {FlowTokenKind::Pe, FlowTokenKind::Pe}, QStringLiteral("circle")},
      {FlowTokenKind::Ps, {FlowTokenKind::Ps}, {FlowTokenKind::Pe}, QStringLiteral("round")},
      {FlowTokenKind::DiamondStart, {FlowTokenKind::DiamondStart, FlowTokenKind::DiamondStart},
       {FlowTokenKind::DiamondStop, FlowTokenKind::DiamondStop}, QStringLiteral("hexagon")},
      {FlowTokenKind::DiamondStart, {FlowTokenKind::DiamondStart}, {FlowTokenKind::DiamondStop}, QStringLiteral("diamond")},
  };

  qsizetype shapeAt = -1;
  for (qsizetype i = 0; i < tokens.size(); ++i) {
    if (tokens.at(i).kind == FlowTokenKind::VertexWithPropsStart) {
      shapeAt = i;
      break;
    }
    for (const TokenShape& shape : shapes) {
      if (tokens.at(i).kind == shape.start) { shapeAt = i; break; }
    }
    if (shapeAt >= 0) break;
  }
  if (shapeAt < 0) {
    for (const FlowToken& token : tokens) {
      if (!isIdStringToken(token.kind))
        throw tokenError(token, FlowchartErrorCategory::InvalidNode,
                         FlowchartErrorStage::Parser, FlowchartErrorCode::InvalidNode,
                         u"idString", {QStringLiteral("id token")});
    }
    node.id = tokenText(tokens, 0, tokens.size());
    if (node.id.isEmpty()) {
      FlowToken eof{FlowTokenKind::Eof, {}, line, column, offset};
      throw tokenError(eof, FlowchartErrorCategory::InvalidNode,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::InvalidNode,
                       u"vertex", {QStringLiteral("node id")});
    }
    node.text = node.id;
    return node;
  }

  if (tokens.at(shapeAt).kind == FlowTokenKind::VertexWithPropsStart) {
    const qsizetype colon = std::find_if(tokens.cbegin() + shapeAt + 1, tokens.cend(),
                                        [](const FlowToken& token) { return token.kind == FlowTokenKind::Colon; }) - tokens.cbegin();
    const qsizetype pipe = std::find_if(tokens.cbegin() + shapeAt + 1, tokens.cend(),
                                       [](const FlowToken& token) { return token.kind == FlowTokenKind::Pipe; }) - tokens.cbegin();
    if (colon >= tokens.size() || pipe >= tokens.size() || colon >= pipe ||
        tokens.last().kind != FlowTokenKind::Sqe) {
      const FlowToken actual = tokens.isEmpty()
                                 ? FlowToken{FlowTokenKind::Eof, {}, line, column, offset}
                                 : tokens.last();
      throw tokenError(actual, FlowchartErrorCategory::InvalidNode,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::InvalidMetadata,
                       u"vertexWithProps", {QStringLiteral("property : value | label ]")});
    }
    node.id = tokenText(tokens, 0, shapeAt);
    node.type = QStringLiteral("rect");
    node.props.insert(tokenText(tokens, shapeAt + 1, colon), tokenText(tokens, colon + 1, pipe));
    const qsizetype labelStart = pipe + 1;
    const qsizetype labelEnd = tokens.size() - 1;
    if (labelStart >= labelEnd)
      throw tokenError(tokens.last(), FlowchartErrorCategory::InvalidNode,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                       u"vertexWithProps", {QStringLiteral("node label")});
    if (labelEnd - labelStart == 1 && tokens.at(labelStart).kind == FlowTokenKind::Str) {
      node.text = tokens.at(labelStart).text; node.labelType = QStringLiteral("string");
    } else if (labelEnd - labelStart == 1 && tokens.at(labelStart).kind == FlowTokenKind::MarkdownStr) {
      node.text = tokens.at(labelStart).text; node.labelType = QStringLiteral("markdown");
    } else {
      node.text = tokenText(tokens, labelStart, labelEnd).trimmed();
    }
    return node;
  }

  node.id = tokenText(tokens, 0, shapeAt);
  if (node.id.isEmpty())
    throw tokenError(tokens.at(shapeAt), FlowchartErrorCategory::InvalidNode,
                     FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                     u"vertex", {QStringLiteral("node id")});
  qsizetype longestPrefix = 0;
  for (const TokenShape& shape : shapes) {
    if (shapeAt + shape.prefix.size() > tokens.size()) continue;
    bool prefixMatches = true;
    for (qsizetype i = 0; i < shape.prefix.size(); ++i)
      prefixMatches &= tokens.at(shapeAt + i).kind == shape.prefix.at(i);
    if (prefixMatches) longestPrefix = std::max(longestPrefix, shape.prefix.size());
  }
  for (const TokenShape& shape : shapes) {
    if (shape.prefix.size() != longestPrefix ||
        shapeAt + shape.prefix.size() + shape.suffix.size() > tokens.size()) continue;
    bool matches = true;
    for (qsizetype i = 0; i < shape.prefix.size(); ++i)
      matches &= tokens.at(shapeAt + i).kind == shape.prefix.at(i);
    for (qsizetype i = 0; i < shape.suffix.size(); ++i)
      matches &= tokens.at(tokens.size() - shape.suffix.size() + i).kind == shape.suffix.at(i);
    if (!matches) continue;
    const qsizetype labelStart = shapeAt + shape.prefix.size();
    const qsizetype labelEnd = tokens.size() - shape.suffix.size();
    if (labelStart >= labelEnd)
      throw tokenError(tokens.at(labelEnd), FlowchartErrorCategory::InvalidNode,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                       u"vertex", {QStringLiteral("node label")});
    if (labelEnd - labelStart == 1 && tokens.at(labelStart).kind == FlowTokenKind::Str) {
      node.text = tokens.at(labelStart).text;
      node.labelType = QStringLiteral("string");
    } else if (labelEnd - labelStart == 1 && tokens.at(labelStart).kind == FlowTokenKind::MarkdownStr) {
      node.text = tokens.at(labelStart).text;
      node.labelType = QStringLiteral("markdown");
    } else {
      node.text = tokenText(tokens, labelStart, labelEnd).trimmed();
    }
    node.type = shape.type;
    return node;
  }
  FlowchartDiagnostic diagnostic;
  diagnostic.category = FlowchartErrorCategory::InvalidNode;
  diagnostic.stage = FlowchartErrorStage::Parser;
  diagnostic.code = FlowchartErrorCode::InvalidNode;
  diagnostic.production = QStringLiteral("vertex");
  diagnostic.span = {offset + source.size(), 0, line,
                     column + static_cast<int>(source.size())};
  throw FlowchartParseError(std::move(diagnostic));
}

struct SourceFragment {
  QString text;
  qsizetype offset = 0;
};

SourceFragment trimmedFragment(const QString& source, qsizetype start, qsizetype length) {
  QString text = source.mid(start, length);
  qsizetype leading = 0;
  while (leading < text.size() && text.at(leading).isSpace()) ++leading;
  qsizetype trailing = text.size();
  while (trailing > leading && text.at(trailing - 1).isSpace()) --trailing;
  return {text.mid(leading, trailing - leading), start + leading};
}

QVector<SourceFragment> splitGroups(const QString& source) {
  QVector<SourceFragment> result;
  if (!source.contains(QLatin1Char('&'))) {
    SourceFragment fragment = trimmedFragment(source, 0, source.size());
    if (!fragment.text.isEmpty()) result.push_back(std::move(fragment));
    return result;
  }
  const QVector<FlowToken> tokens = FlowchartTokenizer(source, false).tokenize();
  qsizetype fragmentStart = 0;
  for (qsizetype i = 0; i < tokens.size(); ++i) {
    const FlowToken& token = tokens.at(i);
    if (token.kind == FlowTokenKind::Eof) break;
    const bool spacedAmp = token.kind == FlowTokenKind::Amp && i > 0 && i + 1 < tokens.size() &&
                           tokens.at(i - 1).kind == FlowTokenKind::Space &&
                           tokens.at(i + 1).kind == FlowTokenKind::Space;
    if (spacedAmp) {
      SourceFragment fragment = trimmedFragment(source, fragmentStart,
                                                token.offset - fragmentStart);
      if (!fragment.text.isEmpty()) result.push_back(std::move(fragment));
      fragmentStart = token.offset + token.text.size();
    }
  }
  SourceFragment fragment = trimmedFragment(source, fragmentStart,
                                            source.size() - fragmentStart);
  if (!fragment.text.isEmpty()) result.push_back(std::move(fragment));
  return result;
}

struct LinkToken {
  qsizetype start = 0;
  qsizetype end = 0;
  qsizetype textStart = 0;
  QString raw;
  QString text;
  QString labelType = QStringLiteral("text");
  QString id;
  QString type = QStringLiteral("arrow_point");
  QString stroke = QStringLiteral("normal");
  int length = 1;
  bool missingText = false;
};

struct SourceStatement {
  QString text;
  int line = 1;
  int column = 1;
  qsizetype offset = 0;
  QChar terminator;
};

struct TokenValidationResult {
  std::optional<FlowchartSourceSpan> vertexLimitSpan;
};

TokenValidationResult validateTokenStream(const QString& source, int maxVertices) {
  FlowchartTokenizer tokenizer(source);
  TokenValidationResult result;
  QSet<QString> definiteVertices;
  QString candidateId;
  FlowToken candidateToken;
  int candidatePhase = 0;
  bool candidateInvalid = false;
  const auto flushCandidate = [&] {
    if (!candidateInvalid && candidatePhase == 3 && !candidateId.isEmpty() &&
        !definiteVertices.contains(candidateId)) {
      definiteVertices.insert(candidateId);
      if (definiteVertices.size() > maxVertices && !result.vertexLimitSpan)
        result.vertexLimitSpan = tokenSpan(candidateToken);
    }
    candidateId.clear();
    candidatePhase = 0;
    candidateInvalid = false;
  };
  for (;;) {
    const FlowToken token = tokenizer.next();
    if (token.kind == FlowTokenKind::Invalid)
      throw tokenError(token, FlowchartErrorCategory::Syntax, FlowchartErrorStage::Lexer,
                       token.diagnosticCode);
    if (token.kind == FlowTokenKind::Eof) {
      flushCandidate();
      return result;
    }
    if (token.kind == FlowTokenKind::Newline || token.kind == FlowTokenKind::Semi ||
        token.kind == FlowTokenKind::NoDir) {
      flushCandidate();
      continue;
    }
    if (token.kind == FlowTokenKind::Space) continue;
    if (candidatePhase == 0 && token.kind == FlowTokenKind::NodeString) {
      candidateId = token.text;
      candidateToken = token;
      candidatePhase = 1;
    } else if (candidatePhase == 1 && token.kind == FlowTokenKind::Sqs) {
      candidatePhase = 2;
    } else if (candidatePhase == 2 && token.kind == FlowTokenKind::Sqe) {
      candidatePhase = 3;
    } else if (candidatePhase != 2) {
      candidateInvalid = true;
    }
  }
  throw std::runtime_error("flowchart lexer validation did not make progress");
}

void scanStatements(const QString& normalized,
                    const std::function<void(const SourceStatement&)>& onStatement) {
  FlowchartTokenizer tokenizer(normalized);
  qsizetype statementStart = 0;
  int statementLine = 1;
  int statementColumn = 1;
  qsizetype statementOffset = 0;
  bool hasStatementToken = false;
  auto flushStatement = [&](qsizetype statementEnd, QChar terminator = {}) {
    QString statement = normalized.mid(statementStart, statementEnd - statementStart);
    qsizetype leading = 0;
    while (leading < statement.size() && statement.at(leading).isSpace()) ++leading;
    statement.remove(0, leading);
    if (!statement.trimmed().isEmpty() && !statement.startsWith(QStringLiteral("%%")))
      onStatement(SourceStatement{statement, statementLine, statementColumn,
                                  statementOffset, terminator});
  };

  for (;;) {
    const FlowToken token = tokenizer.next();
    if (token.kind == FlowTokenKind::Eof) {
      flushStatement(token.offset);
      break;
    }
    if (!hasStatementToken && token.kind != FlowTokenKind::Space &&
        token.kind != FlowTokenKind::Newline) {
      statementLine = token.line;
      statementColumn = token.column;
      statementOffset = token.offset;
      hasStatementToken = true;
    }
    if (token.kind == FlowTokenKind::Semi) {
      flushStatement(token.offset, QLatin1Char(';'));
      statementStart = token.offset + token.text.size();
      hasStatementToken = false;
    } else if (token.kind == FlowTokenKind::Newline || token.kind == FlowTokenKind::NoDir) {
      flushStatement(token.offset, QLatin1Char('\n'));
      statementStart = token.offset + token.text.size();
      hasStatementToken = false;
    } else if (token.kind == FlowTokenKind::Invalid) {
      throw tokenError(token, FlowchartErrorCategory::Syntax, FlowchartErrorStage::Lexer,
                       token.diagnosticCode);
    }
  }
}

QVector<LinkToken> findTokenLinks(const QString& line) {
  const QVector<FlowToken> tokens = FlowchartTokenizer(line, false).tokenize();
  QVector<LinkToken> result;
  for (qsizetype i = 0; i < tokens.size(); ++i) {
    if (tokens.at(i).kind != FlowTokenKind::Link && tokens.at(i).kind != FlowTokenKind::StartLink) continue;
    LinkToken token;
    token.start = tokens.at(i).offset;
    if (i > 0 && tokens.at(i - 1).kind == FlowTokenKind::LinkId) {
      token.start = tokens.at(i - 1).offset;
      token.id = tokens.at(i - 1).text;
      token.id.chop(1);
    }
    qsizetype closing = i;
    if (tokens.at(i).kind == FlowTokenKind::StartLink) {
      closing = i + 1;
      while (closing < tokens.size() && tokens.at(closing).kind != FlowTokenKind::Link) ++closing;
      if (closing >= tokens.size()) continue;
      const qsizetype labelStart = i + 1 < tokens.size() ? tokens.at(i + 1).offset : tokens.at(i).offset;
      token.textStart = labelStart;
      const QString labelSource = line.mid(labelStart, tokens.at(closing).offset - labelStart).trimmed();
      token.missingText = labelSource.isEmpty();
      token.text = labelSource;
      token.text = unquoteLabel(token.text, token.labelType);
    }
    token.end = closing + 1 < tokens.size() ? tokens.at(closing + 1).offset : line.size();
    const QString openingRaw = tokens.at(i).text.trimmed();
    const QString closingRaw = tokens.at(closing).text.trimmed();
    token.raw = tokens.at(i).kind == FlowTokenKind::StartLink
                    ? openingRaw + token.text + closingRaw
                    : closingRaw;

    if (closingRaw.contains(QLatin1Char('.')) || openingRaw.contains(QLatin1Char('.'))) {
      token.stroke = QStringLiteral("dotted");
      token.length = token.text.isEmpty() ? closingRaw.count(QLatin1Char('.')) : 1;
    } else if (closingRaw.contains(QLatin1Char('=')) || openingRaw.contains(QLatin1Char('='))) {
      token.stroke = QStringLiteral("thick");
      token.length = std::max<qsizetype>(1, closingRaw.count(QLatin1Char('=')) - 1);
    } else if (closingRaw.startsWith(QLatin1Char('~'))) {
      token.type = QStringLiteral("arrow_open");
      token.stroke = QStringLiteral("invisible");
      token.length = std::max<qsizetype>(1, closingRaw.count(QLatin1Char('~')) - 2);
    } else if (closingRaw.endsWith(QLatin1Char('>'))) {
      token.length = std::max<qsizetype>(1, closingRaw.count(QLatin1Char('-')) - 1);
    }

    const QChar first = closingRaw.isEmpty() ? QChar() : closingRaw.front();
    const QChar last = closingRaw.isEmpty() ? QChar() : closingRaw.back();
    if (first == QLatin1Char('o') && last == QLatin1Char('o')) token.type = QStringLiteral("double_arrow_circle");
    else if (first == QLatin1Char('x') && last == QLatin1Char('x')) token.type = QStringLiteral("double_arrow_cross");
    else if (first == QLatin1Char('<') && last == QLatin1Char('>')) token.type = QStringLiteral("double_arrow_point");

    result.push_back(std::move(token));
    i = closing;
  }
  return result;
}

class Parser {
public:
  explicit Parser(FlowchartParseOptions options, FlowchartLimits limits) : options_(options), limits_(limits) {}

  FlowchartData parse(const QString& source) {
    QString normalized = source;
    normalized.replace(QRegularExpression(QStringLiteral("\\r\\n?")), QStringLiteral("\n"));
    eofSpan_ = {normalized.size(), 0, 1, 1};
    for (const QChar character : normalized) {
      if (character == QLatin1Char('\n')) {
        ++eofSpan_.line;
        eofSpan_.column = 1;
      } else {
        ++eofSpan_.column;
      }
    }
    if (source.size() > options_.maxTextSize) {
      throw resourceError(QStringLiteral("Maximum flowchart text size exceeded"),
                          {0, source.size(), 1, 1});
    }
    static const QRegularExpression diagramPrefix(
        QStringLiteral(R"(^\s*(?:flowchart(?:-elk)?|graph|swimlane-beta))"));
    if (!diagramPrefix.match(normalized).hasMatch()) {
      FlowchartDiagnostic diagnostic;
      diagnostic.category = FlowchartErrorCategory::MissingHeader;
      diagnostic.stage = FlowchartErrorStage::Detector;
      diagnostic.code = FlowchartErrorCode::MissingHeader;
      diagnostic.span = {0, normalized.isEmpty() ? 0 : 1, 1, 1};
      throw FlowchartParseError(std::move(diagnostic));
    }
    const TokenValidationResult validation =
        validateTokenStream(normalized, limits_.maxVertices);
    if (validation.vertexLimitSpan)
      throw resourceError(QStringLiteral("Maximum vertex count exceeded"),
                          *validation.vertexLimitSpan);
    bool hasHeader = false;
    scanStatements(normalized, [&](const SourceStatement& statement) {
      if (!hasHeader) {
        const QVector<SourceStatement> header{statement};
        qsizetype cursor = 0;
        parseGraphConfig(header, cursor);
        hasHeader = true;
        return;
      }
      currentLine_ = statement.line;
      currentColumn_ = statement.column;
      currentOffset_ = statement.offset;
      if (statement.text.size() > limits_.maxLineLength)
        throw resourceError(QStringLiteral("Maximum line length exceeded"));
      parseStatement(statement.text);
    });
    if (!hasHeader) {
      const QVector<SourceStatement> statements;
      qsizetype cursor = 0;
      parseGraphConfig(statements, cursor);
    }
    if (!contexts_.isEmpty()) throw unclosedSubgraphError();
    FlowchartData result;
    result.direction = direction_;
    result.vertices = vertices_;
    result.edges = edges_;
    result.defaultEdgeStyles = defaultEdgeStyles_;
    result.defaultEdgeInterpolate = defaultEdgeInterpolate_;
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

  FlowchartParseError unclosedSubgraphError() const {
    FlowchartDiagnostic diagnostic;
    diagnostic.category = FlowchartErrorCategory::UnclosedSubgraph;
    diagnostic.stage = FlowchartErrorStage::Parser;
    diagnostic.code = FlowchartErrorCode::UnclosedSubgraph;
    diagnostic.span = eofSpan_;
    diagnostic.production = QStringLiteral("document");
    diagnostic.actual = QStringLiteral("end of input");
    diagnostic.expected = {QStringLiteral("end")};
    return FlowchartParseError(std::move(diagnostic));
  }

  FlowchartParseError resourceError(QString detail, FlowchartSourceSpan span = {}) const {
    if (span.line == 0) {
      span = {currentOffset_, 1, currentLine_, currentColumn_};
      if (span.offset < 0) span.offset = 0;
      if (span.line <= 0) span.line = 1;
      if (span.column <= 0) span.column = 1;
    }
    FlowchartDiagnostic diagnostic;
    diagnostic.category = FlowchartErrorCategory::LimitExceeded;
    diagnostic.stage = FlowchartErrorStage::Resource;
    diagnostic.code = FlowchartErrorCode::LimitExceeded;
    diagnostic.span = span;
    diagnostic.detail = std::move(detail);
    return FlowchartParseError(std::move(diagnostic));
  }

  void parseGraphConfig(const QVector<SourceStatement>& statements, qsizetype& cursor) {
    if (cursor >= statements.size()) {
      FlowchartDiagnostic diagnostic;
      diagnostic.category = FlowchartErrorCategory::MissingHeader;
      diagnostic.stage = FlowchartErrorStage::Detector;
      diagnostic.code = FlowchartErrorCode::MissingHeader;
      diagnostic.span = {0, 0, 1, 1};
      throw FlowchartParseError(std::move(diagnostic));
    }
    const SourceStatement& statement = statements.at(cursor++);
    currentLine_ = statement.line;
    currentColumn_ = statement.column;
    currentOffset_ = statement.offset;
    QVector<FlowToken> header;
    for (FlowToken token : FlowchartTokenizer(statement.text).tokenize()) {
      token.offset += statement.offset;
      if (token.line == 1) token.column += statement.column - 1;
      token.line += statement.line - 1;
      if (token.kind != FlowTokenKind::Space && token.kind != FlowTokenKind::Eof)
        header.push_back(token);
    }
    if (header.isEmpty() || header.first().kind != FlowTokenKind::Graph || header.size() > 2 ||
        (header.size() == 2 && header.at(1).kind != FlowTokenKind::Dir)) {
      static const QRegularExpression flowchartPrefix(
          QStringLiteral(R"(^(?:flowchart(?:-elk)?|graph|swimlane-beta))"));
      if (flowchartPrefix.match(statement.text).hasMatch()) {
        FlowToken actual{FlowTokenKind::Eof, {}, currentLine_,
                         currentColumn_ + static_cast<int>(statement.text.size()),
                         currentOffset_ + statement.text.size()};
        if (!header.isEmpty() && header.first().kind != FlowTokenKind::Graph) {
          actual = header.first();
        } else if (header.size() >= 2 && header.at(1).kind != FlowTokenKind::Dir) {
          actual = header.at(1);
          const QString upper = actual.text.toUpper();
          for (const QString& direction : {QStringLiteral("TB"), QStringLiteral("BT"),
                                           QStringLiteral("RL"), QStringLiteral("LR"),
                                           QStringLiteral("TD")}) {
            if (upper.startsWith(direction) && upper.size() > direction.size()) {
              actual.text.remove(0, direction.size());
              actual.offset += direction.size();
              actual.column += direction.size();
              break;
            }
          }
        } else if (header.size() > 2) {
          actual = header.at(2);
        }
        throw tokenError(actual, FlowchartErrorCategory::Syntax, FlowchartErrorStage::Lexer,
                         FlowchartErrorCode::InvalidDirection, u"graphConfig",
                         {QStringLiteral("flowchart direction")});
      }
      FlowchartDiagnostic diagnostic;
      diagnostic.category = FlowchartErrorCategory::MissingHeader;
      diagnostic.stage = FlowchartErrorStage::Detector;
      diagnostic.code = FlowchartErrorCode::MissingHeader;
      diagnostic.span = {statement.offset, std::max<qsizetype>(1, statement.text.size()),
                         statement.line, statement.column};
      throw FlowchartParseError(std::move(diagnostic));
    }
    direction_ = header.size() == 1 ? QStringLiteral("TB")
                                    : normalizedDirection(header.at(1).text.toUpper());
    if (header.size() == 1 && statement.terminator == QLatin1Char(';')) {
      FlowToken semicolon{FlowTokenKind::Semi, QStringLiteral(";"), statement.line,
                          statement.column + static_cast<int>(statement.text.size()),
                          statement.offset + statement.text.size()};
      throw tokenError(semicolon, FlowchartErrorCategory::Syntax,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::UnexpectedToken,
                       u"graphConfig", {QStringLiteral("newline")});
    }
  }

  Vertex& addVertex(const ParsedNode& parsed, bool applyDefinition = true) {
    auto found = vertexLookup_.find(parsed.id);
    const bool newlyCreated = found == vertexLookup_.end();
    if (newlyCreated) {
      if (parsed.id.size() > limits_.maxNodeIdLength)
        throw resourceError(QStringLiteral("Maximum node id length exceeded"));
      if (vertices_.size() >= limits_.maxVertices)
        throw resourceError(QStringLiteral("Maximum vertex count exceeded"));
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
      vertex.text = sanitizeDbLabel(parsed.text);
      vertex.labelType = parsed.labelType;
      if (!parsed.type.isEmpty()) vertex.type = parsed.type;
      for (auto it = parsed.props.constBegin(); it != parsed.props.constEnd(); ++it)
        vertex.props.insert(it.key(), it.value());
      vertex.classes += parsed.classes;
    }
    return vertex;
  }

  QVector<ParsedNode> parseNodeGroup(const QString& source, qsizetype sourceOffset = 0) {
    QVector<ParsedNode> result;
    for (const SourceFragment& item : splitGroups(source)) {
      ParsedNode node = parseNode(item.text, currentLine_,
                                  currentColumn_ + sourceOffset + item.offset,
                                  currentOffset_ + sourceOffset + item.offset);
      if (node.metadata) {
        ParsedNode bare;
        bare.id = node.id;
        bare.text = node.id;
        addVertex(bare);
      } else {
        addVertex(node);
      }
      result.push_back(std::move(node));
    }
    for (const ParsedNode& node : result)
      if (node.metadata) addVertex(node);
    return result;
  }

  void addLink(const ParsedNode& start, const ParsedNode& end, const LinkToken& token) {
    if (edges_.size() >= options_.maxEdges) {
      throw resourceError(QStringLiteral("Edge limit exceeded"));
    }
    Edge edge;
    edge.start = start.id;
    edge.end = end.id;
    edge.text = token.text;
    edge.labelType = token.labelType;
    edge.stroke = token.stroke;
    edge.type = token.type;
    edge.length = token.length;
    int& pairCount = edgePairCounts_[edge.start][edge.end];
    const int existingPairCount = pairCount++;
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
    edge.id = base + QString::number(existingPairCount == 0 ? 0 : existingPairCount + 1);
    edges_.push_back(std::move(edge));
  }

  QStringList parseGraphStatement(const QString& line) {
    QVector<LinkToken> links = findTokenLinks(line);
    if (links.isEmpty()) {
      const QVector<ParsedNode> nodes = parseNodeGroup(line);
      QStringList ids;
      for (const ParsedNode& node : nodes) ids.push_back(node.id);
      return ids;
    }
    for (const LinkToken& link : links) {
      if (!link.missingText) continue;
      FlowchartDiagnostic diagnostic;
      diagnostic.category = FlowchartErrorCategory::Syntax;
      diagnostic.stage = FlowchartErrorStage::Parser;
      diagnostic.code = FlowchartErrorCode::MissingValue;
      diagnostic.span = {currentOffset_ + link.textStart, 1, currentLine_,
                         currentColumn_ + static_cast<int>(link.textStart)};
      diagnostic.production = QStringLiteral("edgeText");
      diagnostic.actual = QStringLiteral("empty edge label");
      diagnostic.expected = {QStringLiteral("edge text")};
      throw FlowchartParseError(std::move(diagnostic));
    }

    QVector<SourceFragment> segments;
    qsizetype position = 0;
    for (const LinkToken& link : links) {
      segments.push_back(trimmedFragment(line, position, link.start - position));
      position = link.end;
    }
    segments.push_back(trimmedFragment(line, position, line.size() - position));
    QVector<QVector<ParsedNode>> groups;
    for (qsizetype segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
      QString segment = segments.at(segmentIndex).text;
      qsizetype segmentOffset = segments.at(segmentIndex).offset;
      if (segment.startsWith(QLatin1Char('|'))) {
        const qsizetype close = segment.indexOf(QLatin1Char('|'), 1);
        if (close > 0) {
          const SourceFragment afterLabel = trimmedFragment(
              segment, close + 1, segment.size() - close - 1);
          segmentOffset += afterLabel.offset;
          segment = afterLabel.text;
        }
      }
      QVector<ParsedNode> group = parseNodeGroup(segment, segmentOffset);
      if (group.isEmpty()) {
        qsizetype localOffset = segmentIndex == 0 ? 0 : links.at(segmentIndex - 1).end;
        while (localOffset < line.size() && line.at(localOffset).isSpace()) ++localOffset;
        FlowchartDiagnostic diagnostic;
        diagnostic.category = FlowchartErrorCategory::InvalidNode;
        diagnostic.stage = FlowchartErrorStage::Parser;
        diagnostic.code = FlowchartErrorCode::MissingLinkEndpoint;
        diagnostic.production = QStringLiteral("linkStatement");
        diagnostic.span = {currentOffset_ + localOffset, 1, currentLine_,
                           currentColumn_ + static_cast<int>(localOffset)};
        throw FlowchartParseError(std::move(diagnostic));
      }
      groups.push_back(std::move(group));
    }
    for (qsizetype i = 0; i < links.size(); ++i) {
      LinkToken token = links.at(i);
      QString rightSource = segments.at(i + 1).text;
      if (rightSource.startsWith(QLatin1Char('|'))) {
        const qsizetype close = rightSource.indexOf(QLatin1Char('|'), 1);
        token.text = rightSource.mid(1, close - 1);
        token.text = unquoteLabel(token.text, token.labelType);
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

  QString parseRequiredText(TokenCursor& cursor, std::initializer_list<FlowTokenKind> stops,
                            QStringView production) {
    const QString value = cursor.consumeUntil(stops);
    if (!value.isEmpty()) return value;
    throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                     FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                     production);
  }

  QStringList parseStyles(TokenCursor& cursor, QStringView production) {
    QStringList styles;
    QString current;
    while (!cursor.atEnd()) {
      if (cursor.consume(FlowTokenKind::Comma)) {
        if (current.isEmpty())
          throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                           FlowchartErrorStage::Parser, FlowchartErrorCode::MissingListItem,
                           production);
        styles.push_back(current);
        current.clear();
        continue;
      }
      if (!isStyleComponent(cursor.peek().kind))
        throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                         FlowchartErrorStage::Parser, FlowchartErrorCode::UnexpectedToken,
                         production, {QStringLiteral("style component")});
      current += tokenSource(cursor.consume());
    }
    if (current.isEmpty())
      throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::MissingListItem,
                       production);
    styles.push_back(current);
    return styles;
  }

  void parseStyleStatement(const QString& line) {
    TokenCursor cursor(line, currentLine_, currentColumn_, currentOffset_);
    cursor.expect(FlowTokenKind::Style, u"styleStatement");
    cursor.expect(FlowTokenKind::Space, u"styleStatement");
    const QString id = parseRequiredText(cursor, {FlowTokenKind::Space}, u"styleStatement");
    cursor.expect(FlowTokenKind::Space, u"styleStatement");
    const QStringList styles = parseStyles(cursor, u"styleStatement");
    ParsedNode node;
    node.id = id;
    node.text = id;
    Vertex& vertex = addVertex(node, false);
    if (vertex.styles.size() + styles.size() > limits_.maxStylesPerVertex)
      throw resourceError(QStringLiteral("Maximum styles per vertex exceeded"));
    vertex.styles += styles;
  }

  void parseClassDefStatement(const QString& line) {
    TokenCursor cursor(line, currentLine_, currentColumn_, currentOffset_);
    cursor.expect(FlowTokenKind::ClassDef, u"classDefStatement");
    cursor.expect(FlowTokenKind::Space, u"classDefStatement");
    FlowClass definition;
    definition.id = parseRequiredText(cursor, {FlowTokenKind::Space}, u"classDefStatement");
    cursor.expect(FlowTokenKind::Space, u"classDefStatement");
    definition.styles = parseStyles(cursor, u"classDefStatement");
    for (const QString& style : definition.styles) {
      if (style.contains(QStringLiteral("color"))) definition.textStyles.push_back(style);
    }
    const auto existing = classLookup_.constFind(definition.id);
    if (existing != classLookup_.constEnd()) {
      FlowClass& target = classes_[existing.value()];
      target.styles += definition.styles;
      target.textStyles += definition.textStyles;
    } else {
      if (classes_.size() >= limits_.maxClasses)
        throw resourceError(QStringLiteral("Maximum class count exceeded"));
      classLookup_.insert(definition.id, classes_.size());
      classes_.push_back(std::move(definition));
    }
  }

  void parseClassStatement(const QString& line) {
    TokenCursor cursor(line, currentLine_, currentColumn_, currentOffset_);
    cursor.expect(FlowTokenKind::Class, u"classStatement");
    cursor.expect(FlowTokenKind::Space, u"classStatement");
    const QString vertices = parseRequiredText(cursor, {FlowTokenKind::Space}, u"classStatement");
    cursor.expect(FlowTokenKind::Space, u"classStatement");
    const QString classes = parseRequiredText(cursor, {FlowTokenKind::Eof}, u"classStatement");
    cursor.expectEnd(u"classStatement");
    const QStringList ids = vertices.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList names = classes.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& id : ids) {
      ParsedNode node;
      node.id = id;
      node.text = id;
      Vertex& vertex = addVertex(node, false);
      vertex.classes += names;
    }
  }

  void applyLinkStyle(const QVector<int>& indices, const QString& interpolate,
                      const QStringList& styles) {
    for (const int index : indices) {
      Q_ASSERT(index >= 0 && index < edges_.size());
      if (!interpolate.isEmpty()) edges_[index].interpolate = interpolate;
      if (styles.isEmpty()) continue;
      edges_[index].style = styles;
      bool hasFill = false;
      for (const QString& style : styles)
        if (style.startsWith(QStringLiteral("fill"))) hasFill = true;
      if (!hasFill) edges_[index].style.push_back(QStringLiteral("fill:none"));
    }
  }

  void parseLinkStyleStatement(const QString& line) {
    TokenCursor cursor(line, currentLine_, currentColumn_, currentOffset_);
    cursor.expect(FlowTokenKind::LinkStyle, u"linkStyleStatement");
    cursor.expect(FlowTokenKind::Space, u"linkStyleStatement");
    const bool isDefault = cursor.consume(FlowTokenKind::Default);
    QVector<int> indices;
    if (!isDefault) {
      do {
        const FlowToken number = cursor.expect(FlowTokenKind::Number, u"numList");
        const int index = number.text.toInt();
        if (index < 0 || index >= edges_.size()) {
          FlowchartDiagnostic diagnostic;
          diagnostic.category = FlowchartErrorCategory::LinkStyleBounds;
          diagnostic.stage = FlowchartErrorStage::Semantic;
          diagnostic.code = FlowchartErrorCode::LinkStyleBounds;
          diagnostic.span = tokenSpan(number);
          diagnostic.production = QStringLiteral("linkStyleStatement");
          diagnostic.actual = diagnosticToken(number);
          diagnostic.detail = QStringLiteral("linkStyle index %1 is outside the edge list of size %2.")
                                  .arg(index).arg(edges_.size());
          throw FlowchartParseError(std::move(diagnostic));
        }
        indices.push_back(index);
      } while (cursor.consume(FlowTokenKind::Comma));
    }
    cursor.expect(FlowTokenKind::Space, u"linkStyleStatement");

    QString interpolate;
    if (cursor.consume(FlowTokenKind::Interpolate)) {
      cursor.expect(FlowTokenKind::Space, u"linkStyleStatement");
      interpolate = parseRequiredText(cursor, {FlowTokenKind::Space, FlowTokenKind::Eof},
                                      u"linkStyleStatement");
      if (cursor.atEnd()) {
        if (isDefault) defaultEdgeInterpolate_ = interpolate;
        else applyLinkStyle(indices, interpolate, {});
        return;
      }
      cursor.expect(FlowTokenKind::Space, u"linkStyleStatement");
    }
    const QStringList styles = parseStyles(cursor, u"linkStyleStatement");
    if (isDefault) {
      if (!interpolate.isEmpty()) defaultEdgeInterpolate_ = interpolate;
      defaultEdgeStyles_ = styles;
    } else {
      applyLinkStyle(indices, interpolate, styles);
    }
  }

  void markClickable(const QString& id) {
    const auto found = vertexLookup_.constFind(id);
    if (found != vertexLookup_.constEnd())
      vertices_[found.value()].classes.push_back(QStringLiteral("clickable"));
  }

  void setTooltip(const QString& id, const QString& tooltip) {
    if (tooltip.isEmpty()) return;
    if (tooltips_.size() >= limits_.maxTooltips)
      throw resourceError(QStringLiteral("Maximum tooltip count exceeded"));
    tooltips_.insert(id, tooltip);
  }

  void setLink(const QString& id, const QString& link, const QString& target) {
    const auto found = vertexLookup_.constFind(id);
    if (found == vertexLookup_.constEnd()) return;
    FlowVertex& vertex = vertices_[found.value()];
    QUrl url(link);
    QString formatted = url.toString();
    if (!url.scheme().isEmpty() && !url.host().isEmpty() && url.path().isEmpty())
      formatted += QLatin1Char('/');
    vertex.link = formatted;
    vertex.linkTarget = target;
    vertex.linkUnsafe = !muffin::isSafeUrl(vertex.link, false);
    markClickable(id);
  }

  void parseClickStatement(const QString& line) {
    TokenCursor cursor(line, currentLine_, currentColumn_, currentOffset_);
    cursor.expect(FlowTokenKind::Click, u"clickStatement");
    if (!cursor.skipSpace())
      cursor.expect(FlowTokenKind::Space, u"clickStatement");
    QString id;
    while (!cursor.atEnd() && cursor.peek().kind != FlowTokenKind::Space) {
      if (!isIdStringToken(cursor.peek().kind))
        throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                         FlowchartErrorStage::Parser, FlowchartErrorCode::UnexpectedToken,
                         u"clickStatement", {QStringLiteral("node id")});
      id += tokenSource(cursor.consume());
    }
    if (id.isEmpty())
      throw tokenError(cursor.peek(), FlowchartErrorCategory::InvalidDirective,
                       FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                       u"clickStatement", {QStringLiteral("node id")});
    cursor.expect(FlowTokenKind::Space, u"clickStatement");

    if (cursor.peek().kind == FlowTokenKind::CallbackName) {
      cursor.consume();
      cursor.consume(FlowTokenKind::CallbackArgs);
      markClickable(id);
      if (cursor.atEnd()) return;
      cursor.expect(FlowTokenKind::Space, u"clickStatement");
      setTooltip(id, cursor.expect(FlowTokenKind::Str, u"clickStatement").text);
      cursor.expectEnd(u"clickStatement");
      return;
    }

    if (cursor.consume(FlowTokenKind::Href)) {
      cursor.expect(FlowTokenKind::Space, u"clickStatement");
    }
    if (cursor.peek().kind == FlowTokenKind::Str) {
      const QString link = cursor.consume().text;
      QString tooltip;
      QString target;
      if (!cursor.atEnd()) {
        cursor.expect(FlowTokenKind::Space, u"clickStatement");
        if (cursor.peek().kind == FlowTokenKind::Str) {
          tooltip = cursor.consume().text;
          if (!cursor.atEnd()) cursor.expect(FlowTokenKind::Space, u"clickStatement");
        }
        if (!cursor.atEnd()) target = cursor.expect(FlowTokenKind::LinkTarget, u"clickStatement").text;
      }
      cursor.expectEnd(u"clickStatement");
      setLink(id, link, target);
      setTooltip(id, tooltip);
      return;
    }

    parseRequiredText(cursor, {FlowTokenKind::Space, FlowTokenKind::Eof}, u"alphaNum");
    markClickable(id);
    if (cursor.atEnd()) return;
    cursor.expect(FlowTokenKind::Space, u"clickStatement");
    setTooltip(id, cursor.expect(FlowTokenKind::Str, u"clickStatement").text);
    cursor.expectEnd(u"clickStatement");
  }

  void parseStatement(const QString& line) {
    const FlowTokenKind statementKind = firstSignificantKind(line);
    if (statementKind == FlowTokenKind::AccTitle) {
      accTitle_ = line.mid(line.indexOf(QLatin1Char(':')) + 1).trimmed();
      return;
    }
    if (statementKind == FlowTokenKind::AccDescr && line.contains(QLatin1Char(':'))) {
      accDescription_ = line.mid(line.indexOf(QLatin1Char(':')) + 1).trimmed();
      return;
    }
    if (statementKind == FlowTokenKind::AccDescr && line.contains(QLatin1Char('{')) &&
        line.endsWith(QLatin1Char('}'))) {
      QString body = line.mid(line.indexOf(QLatin1Char('{')) + 1);
      body.chop(1);
      QStringList descriptionLines = body.split(QLatin1Char('\n'));
      for (QString& descriptionLine : descriptionLines) descriptionLine = descriptionLine.trimmed();
      while (!descriptionLines.isEmpty() && descriptionLines.first().isEmpty()) descriptionLines.removeFirst();
      while (!descriptionLines.isEmpty() && descriptionLines.last().isEmpty()) descriptionLines.removeLast();
      accDescription_ = descriptionLines.join(QLatin1Char('\n'));
      return;
    }
    const qsizetype edgeMetadataAt = line.indexOf(QStringLiteral("@{"));
    if (edgeMetadataAt > 0 && line.endsWith(QLatin1Char('}')) && findTokenLinks(line).isEmpty()) {
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
    if (statementKind == FlowTokenKind::Subgraph && line.startsWith(QStringLiteral("subgraph "))) {
      qsizetype definitionAt = 9;
      while (definitionAt < line.size() && line.at(definitionAt).isSpace()) ++definitionAt;
      QString definition = line.mid(definitionAt).trimmed();
      Context context;
      QVector<FlowToken> definitionTokens = FlowchartTokenizer(definition, false).tokenize();
      for (FlowToken& token : definitionTokens) {
        token.offset += currentOffset_ + definitionAt;
        if (token.line == 1) token.column += currentColumn_ + definitionAt - 1;
        token.line += currentLine_ - 1;
      }
      if (!definitionTokens.isEmpty() && definitionTokens.last().kind == FlowTokenKind::Eof)
        definitionTokens.removeLast();
      if (definitionTokens.isEmpty()) {
        FlowToken eof{FlowTokenKind::Eof, {}, currentLine_,
                      currentColumn_ + static_cast<int>(definitionAt),
                      currentOffset_ + definitionAt};
        throw tokenError(eof, FlowchartErrorCategory::Syntax,
                         FlowchartErrorStage::Parser, FlowchartErrorCode::MissingValue,
                         u"textNoTags", {QStringLiteral("subgraph id or title")});
      }
      if (definitionTokens.size() == 1 &&
          (definitionTokens.first().kind == FlowTokenKind::Str ||
           definitionTokens.first().kind == FlowTokenKind::MarkdownStr)) {
        context.id = QStringLiteral("subGraph%1").arg(subgraphs_.size());
        context.title = sanitizeDbLabel(definitionTokens.first().text);
        context.labelType = definitionTokens.first().kind == FlowTokenKind::MarkdownStr
                                ? QStringLiteral("markdown") : QStringLiteral("text");
      } else if (std::any_of(definitionTokens.cbegin(), definitionTokens.cend(),
                             [](const FlowToken& token) {
                               return token.kind == FlowTokenKind::Sqs;
                             })) {
        const ParsedNode node = parseNode(definition, currentLine_,
                                          currentColumn_ + definitionAt,
                                          currentOffset_ + definitionAt);
        context.id = node.id;
        context.title = sanitizeDbLabel(node.text);
        context.labelType = node.labelType;
      } else {
        for (const FlowToken& token : definitionTokens) {
          if (!isTextNoTagsToken(token.kind))
            throw tokenError(token, FlowchartErrorCategory::Syntax,
                             FlowchartErrorStage::Parser,
                             FlowchartErrorCode::UnexpectedToken, u"textNoTags",
                             {QStringLiteral("text token")});
        }
        context.id = tokenText(definitionTokens, 0, definitionTokens.size()).trimmed();
        context.title = sanitizeDbLabel(context.id);
        if (context.id.contains(QLatin1Char(' ')))
          context.id = QStringLiteral("subGraph%1").arg(subgraphs_.size());
      }
      if (contexts_.size() >= limits_.maxSubgraphDepth)
        throw resourceError(QStringLiteral("Maximum subgraph nesting depth exceeded"));
      contexts_.push_back(std::move(context));
      return;
    }
    if (statementKind == FlowTokenKind::End && line == QLatin1String("end")) {
      if (contexts_.isEmpty()) {
        FlowchartDiagnostic diagnostic;
        diagnostic.category = FlowchartErrorCategory::UnexpectedEnd;
        diagnostic.stage = FlowchartErrorStage::Parser;
        diagnostic.code = FlowchartErrorCode::UnexpectedEnd;
        diagnostic.span = {currentOffset_, line.size(), currentLine_, currentColumn_};
        diagnostic.production = QStringLiteral("document");
        diagnostic.actual = QStringLiteral("end \"end\"");
        throw FlowchartParseError(std::move(diagnostic));
      }
      Context context = contexts_.takeLast();
      Subgraph subgraph;
      subgraph.id = context.id;
      subgraph.title = context.title;
      subgraph.nodes = context.nodes;
      subgraph.dir = context.dir;
      subgraph.hasExplicitDir = context.explicitDir;
      subgraph.labelType = context.labelType;
      if (subgraphs_.size() >= limits_.maxSubgraphs)
        throw resourceError(QStringLiteral("Maximum subgraph count exceeded"));
      subgraphs_.push_back(std::move(subgraph));
      if (!contexts_.isEmpty()) contexts_.last().nodes.push_back(context.id);
      return;
    }
    if (statementKind == FlowTokenKind::DirectionTb || statementKind == FlowTokenKind::DirectionBt ||
        statementKind == FlowTokenKind::DirectionRl || statementKind == FlowTokenKind::DirectionLr ||
        statementKind == FlowTokenKind::DirectionTd) {
      if (contexts_.isEmpty()) return;
      contexts_.last().dir = line.mid(10).trimmed();
      contexts_.last().explicitDir = true;
      return;
    }
    if (statementKind == FlowTokenKind::ClassDef) {
      parseClassDefStatement(line);
      return;
    }
    if (statementKind == FlowTokenKind::Class) {
      parseClassStatement(line);
      return;
    }
    if (statementKind == FlowTokenKind::Style) {
      parseStyleStatement(line);
      return;
    }
    if (statementKind == FlowTokenKind::LinkStyle) {
      parseLinkStyleStatement(line);
      return;
    }
    if (statementKind == FlowTokenKind::Click) {
      parseClickStatement(line);
      return;
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
  int currentColumn_ = 0;
  qsizetype currentOffset_ = -1;
  FlowchartSourceSpan eofSpan_;
  QVector<Vertex> vertices_;
  QMap<QString, qsizetype> vertexLookup_;
  QVector<Edge> edges_;
  QHash<QString, QHash<QString, int>> edgePairCounts_;
  QStringList defaultEdgeStyles_;
  QString defaultEdgeInterpolate_;
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
// hardening). Applied at the FlowchartParseError constructor, the single
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
  return out.size() > max ? out.left(max) + QStringLiteral("...") : out;
}

}  // namespace

QString flowchartErrorStageName(FlowchartErrorStage stage) {
  switch (stage) {
    case FlowchartErrorStage::Detector: return QStringLiteral("detector");
    case FlowchartErrorStage::Lexer: return QStringLiteral("lexer");
    case FlowchartErrorStage::Parser: return QStringLiteral("parser");
    case FlowchartErrorStage::Semantic: return QStringLiteral("semantic");
    case FlowchartErrorStage::Resource: return QStringLiteral("resource");
    case FlowchartErrorStage::Security: return QStringLiteral("security");
  }
  return QStringLiteral("parser");
}

QString flowchartErrorCodeName(FlowchartErrorCode code) {
  switch (code) {
    case FlowchartErrorCode::Generic: return QStringLiteral("generic");
    case FlowchartErrorCode::UnexpectedCharacter: return QStringLiteral("unexpected-character");
    case FlowchartErrorCode::InvalidDirection: return QStringLiteral("invalid-direction");
    case FlowchartErrorCode::UnterminatedString: return QStringLiteral("unterminated-string");
    case FlowchartErrorCode::UnterminatedShapeData: return QStringLiteral("unterminated-shape-data");
    case FlowchartErrorCode::UnterminatedCallbackArguments: return QStringLiteral("unterminated-callback-arguments");
    case FlowchartErrorCode::UnexpectedToken: return QStringLiteral("unexpected-token");
    case FlowchartErrorCode::MissingToken: return QStringLiteral("missing-token");
    case FlowchartErrorCode::MissingValue: return QStringLiteral("missing-value");
    case FlowchartErrorCode::MissingListItem: return QStringLiteral("missing-list-item");
    case FlowchartErrorCode::InvalidNode: return QStringLiteral("invalid-node");
    case FlowchartErrorCode::InvalidMetadata: return QStringLiteral("invalid-metadata");
    case FlowchartErrorCode::MissingLinkEndpoint: return QStringLiteral("missing-link-endpoint");
    case FlowchartErrorCode::MissingHeader: return QStringLiteral("missing-header");
    case FlowchartErrorCode::UnclosedSubgraph: return QStringLiteral("unclosed-subgraph");
    case FlowchartErrorCode::UnexpectedEnd: return QStringLiteral("unexpected-end");
    case FlowchartErrorCode::LinkStyleBounds: return QStringLiteral("link-style-bounds");
    case FlowchartErrorCode::LimitExceeded: return QStringLiteral("limit-exceeded");
    case FlowchartErrorCode::SecurityViolation: return QStringLiteral("security-violation");
  }
  return QStringLiteral("generic");
}

QString formatFlowchartDiagnostic(const FlowchartDiagnostic& diagnostic) {
  const QString context = diagnostic.production.isEmpty()
                              ? QString()
                              : QStringLiteral(" while parsing %1").arg(diagnostic.production);
  const QString actual = diagnostic.actual.isEmpty() ? QStringLiteral("end of input")
                                                       : diagnostic.actual;
  switch (diagnostic.code) {
    case FlowchartErrorCode::UnexpectedCharacter:
      return QStringLiteral("Unexpected character %1.").arg(actual);
    case FlowchartErrorCode::InvalidDirection:
      return QStringLiteral("Expected a flowchart direction; found %1.").arg(actual);
    case FlowchartErrorCode::UnterminatedString:
      return QStringLiteral("Unterminated quoted string; expected a closing quote.");
    case FlowchartErrorCode::UnterminatedShapeData:
      return QStringLiteral("Unterminated shape metadata; expected '}'.");
    case FlowchartErrorCode::UnterminatedCallbackArguments:
      return QStringLiteral("Unterminated callback arguments; expected ')'.");
    case FlowchartErrorCode::UnexpectedToken:
    case FlowchartErrorCode::MissingToken:
      return QStringLiteral("Expected %1%2; found %3.")
          .arg(diagnostic.expected.join(QStringLiteral(" or ")), context, actual);
    case FlowchartErrorCode::MissingValue:
      return QStringLiteral("Expected a value%1; found %2.").arg(context, actual);
    case FlowchartErrorCode::MissingListItem:
      return QStringLiteral("Expected a list item%1; found %2.").arg(context, actual);
    case FlowchartErrorCode::MissingLinkEndpoint:
      return QStringLiteral("Expected a node at both ends of the link.");
    case FlowchartErrorCode::InvalidNode:
      return QStringLiteral("Invalid flowchart node.");
    case FlowchartErrorCode::InvalidMetadata:
      return QStringLiteral("Invalid flowchart metadata.");
    case FlowchartErrorCode::MissingHeader:
      return QStringLiteral("Expected a flowchart or graph header.");
    case FlowchartErrorCode::UnclosedSubgraph:
      return QStringLiteral("Unclosed subgraph; expected 'end'.");
    case FlowchartErrorCode::UnexpectedEnd:
      return QStringLiteral("Unexpected 'end' without an open subgraph.");
    case FlowchartErrorCode::LinkStyleBounds:
      return diagnostic.detail.isEmpty()
               ? QStringLiteral("linkStyle index is outside the edge list.")
               : diagnostic.detail;
    case FlowchartErrorCode::LimitExceeded:
      return diagnostic.detail.isEmpty() ? QStringLiteral("Flowchart resource limit exceeded.")
                                         : diagnostic.detail;
    case FlowchartErrorCode::SecurityViolation:
      return diagnostic.detail.isEmpty() ? QStringLiteral("Flowchart security policy rejected the input.")
                                         : diagnostic.detail;
    default:
      return diagnostic.detail.isEmpty() ? QStringLiteral("Invalid flowchart syntax.")
                                         : diagnostic.detail;
  }
}

FlowchartParseError::FlowchartParseError(FlowchartDiagnostic diagnostic)
    : std::runtime_error(
          sanitizeErrorMessage(formatFlowchartDiagnostic(diagnostic)).toUtf8().constData()),
      category_(diagnostic.category), diagnostic_(std::move(diagnostic)) {}

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
