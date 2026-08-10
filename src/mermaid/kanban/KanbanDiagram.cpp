#include "mermaid/kanban/KanbanDiagram.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/editor/MermaidRenderSupport.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace muffin::mermaid::kanban {

KanbanParseError::KanbanParseError(const QString& message, int line,
                                   int column, KanbanErrorKind kind,
                                   QString token)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column),
      kind(kind),
      token(std::move(token)) {}

namespace {

bool jsWhitespace(QChar ch) {
  const ushort u = ch.unicode();
  return (u >= 0x0009 && u <= 0x000d) || u == 0x0020 || u == 0x00a0 ||
         u == 0x1680 || (u >= 0x2000 && u <= 0x200a) || u == 0x2028 ||
         u == 0x2029 || u == 0x202f || u == 0x205f || u == 0x3000 ||
         u == 0xfeff;
}

bool inlineWhitespace(QChar ch) {
  return jsWhitespace(ch) && ch != QLatin1Char('\n') &&
         ch != QLatin1Char('\r') && ch.unicode() != 0x2028 &&
         ch.unicode() != 0x2029;
}

QString jsNumberString(double value) {
  if (std::isnan(value)) return QStringLiteral("NaN");
  if (std::isinf(value))
    return value < 0 ? QStringLiteral("-Infinity")
                     : QStringLiteral("Infinity");
  if (value == 0.0) return QStringLiteral("0");
  char buffer[64];
  const auto converted =
      std::to_chars(buffer, buffer + sizeof(buffer), value,
                    std::chars_format::general);
  if (converted.ec == std::errc())
    return QString::fromLatin1(buffer, qsizetype(converted.ptr - buffer));
  return QString::number(value, 'g',
                         std::numeric_limits<double>::max_digits10);
}

QString jsToString(const QJsonValue& value);

QString jsArrayToString(const QJsonArray& array) {
  QStringList values;
  values.reserve(array.size());
  for (const QJsonValue& value : array) values.push_back(jsToString(value));
  return values.join(QLatin1Char(','));
}

QString jsToString(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return {};
  if (value.isString()) return value.toString();
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isDouble()) return jsNumberString(value.toDouble());
  if (value.isArray()) return jsArrayToString(value.toArray());
  return QStringLiteral("[object Object]");
}

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble())
    return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

QJsonValue effectiveNullish(const QJsonValue& value,
                            const QJsonValue& fallback) {
  return value.isUndefined() || value.isNull() ? fallback : value;
}

QString sanitized(QString value) {
  // Lexbor parses a fragment through an HTML body and drops boundary
  // whitespace. DOMPurify.sanitize(), used by Mermaid's sanitizeText, keeps
  // that whitespace; it is observable in ids and labels.
  qsizetype begin = 0;
  qsizetype end = value.size();
  while (begin < end && value.at(begin).isSpace()) ++begin;
  while (end > begin && value.at(end - 1).isSpace()) --end;
  if (begin == end) return value;
  return value.left(begin) +
         HtmlSanitizer().sanitizedMermaidText(value.mid(begin, end - begin)) +
         value.mid(end);
}

QJsonValue yamlScalar(const YAML::Node& node) {
  const QString text = QString::fromStdString(node.Scalar());
  if (node.Tag() == QLatin1String("!")) return text;
  static const QRegularExpression nullValue(
      QStringLiteral(R"(^(?:null|Null|NULL|~)$)"));
  static const QRegularExpression boolValue(
      QStringLiteral(R"(^(?:true|True|TRUE|false|False|FALSE)$)"));
  static const QRegularExpression integer(
      QStringLiteral(R"(^[-+]?(?:0|[1-9][0-9]*|0x[0-9a-fA-F]+)$)"));
  static const QRegularExpression floating(QStringLiteral(
      R"(^[-+]?(?:(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][-+]?[0-9]+)?|[0-9]+[eE][-+]?[0-9]+|\.inf|\.Inf|\.INF|\.nan|\.NaN|\.NAN)$)"));
  if (nullValue.match(text).hasMatch()) return QJsonValue::Null;
  if (boolValue.match(text).hasMatch())
    return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
  if (integer.match(text).hasMatch()) {
    bool ok = false;
    const qlonglong number =
        text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
            ? text.mid(2).toLongLong(&ok, 16)
            : text.toLongLong(&ok, 10);
    if (ok) return double(number);
  }
  if (floating.match(text).hasMatch()) {
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok && std::isfinite(number)) return number;
  }
  return text;
}

void rejectDuplicateYamlKeys(const YAML::Node& node) {
  if (node.IsMap()) {
    QSet<QString> keys;
    for (const auto& item : node) {
      const QString key = QString::fromStdString(item.first.as<std::string>());
      if (keys.contains(key))
        throw KanbanParseError(QStringLiteral("duplicated mapping key"), 0, 0,
                               KanbanErrorKind::Yaml);
      keys.insert(key);
      rejectDuplicateYamlKeys(item.second);
    }
  } else if (node.IsSequence()) {
    for (const YAML::Node& item : node) rejectDuplicateYamlKeys(item);
  }
}

QJsonValue yamlToJson(const YAML::Node& node, int depth = 0) {
  if (!node || node.IsNull()) return QJsonValue::Null;
  if (depth > 100) return QJsonValue::Null;
  if (node.IsScalar()) return yamlScalar(node);
  if (node.IsSequence()) {
    QJsonArray result;
    for (const YAML::Node& item : node)
      result.push_back(yamlToJson(item, depth + 1));
    return result;
  }
  if (node.IsMap()) {
    QJsonObject result;
    for (const auto& item : node) {
      result.insert(QString::fromStdString(item.first.as<std::string>()),
                    yamlToJson(item.second, depth + 1));
    }
    return result;
  }
  return QJsonValue::Null;
}

class Cursor {
public:
  explicit Cursor(QString value) : text(std::move(value)) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  bool end() const { return offset >= text.size(); }
  QChar peek(qsizetype ahead = 0) const {
    return offset + ahead < text.size() ? text.at(offset + ahead) : QChar();
  }
  bool starts(QStringView value,
              Qt::CaseSensitivity cs = Qt::CaseSensitive) const {
    return QStringView(text).mid(offset, value.size()).compare(value, cs) == 0;
  }
  int currentLine() const { return line; }
  int currentColumn() const { return column; }
  qsizetype currentOffset() const { return offset; }
  const QString& source() const { return text; }

  QChar take() {
    if (end()) return {};
    const QChar ch = text.at(offset++);
    if (ch == QLatin1Char('\n')) {
      ++line;
      column = 1;
    } else {
      ++column;
    }
    return ch;
  }

  void take(qsizetype count) {
    while (count-- > 0) take();
  }

private:
  QString text;
  qsizetype offset = 0;
  int line = 1;
  int column = 1;
};

struct RawNode : KanbanNode {};

class Parser {
public:
  Parser(QString source, KanbanParseConfig parseConfig)
      : cursor(std::move(source)), config(std::move(parseConfig)) {
    config.mindmapPadding = effectiveNullish(config.mindmapPadding, 10.0);
    config.mindmapMaxNodeWidth =
        effectiveNullish(config.mindmapMaxNodeWidth, 200.0);
  }

  KanbanData parse() {
    parseHeader();
    if (cursor.end())
      parserError(QStringLiteral("EOF"), 2, 7,
                  QStringLiteral("Parse error on line 2: unexpected EOF"));
    int initialNewlines = 0;
    while (!cursor.end() && cursor.peek() == QLatin1Char('\n')) {
      cursor.take();
      ++initialNewlines;
    }
    if (initialNewlines >= 2) {
      int indentation = 0;
      while (!cursor.end() && inlineWhitespace(cursor.peek())) {
        cursor.take();
        ++indentation;
      }
      if (!cursor.end() && cursor.peek() != QLatin1Char('\n') &&
          indentation > 0) {
        parserError(QStringLiteral("SPACELIST"), 3, 7);
      }
    }
    while (!cursor.end()) parseStatement();
    return materialize();
  }

private:
  [[noreturn]] void parserError(const QString& token, int line = -1,
                                int column = -1,
                                const QString& detail = {}) const {
    if (line < 0) line = cursor.currentLine();
    if (column < 0) column = cursor.currentColumn();
    const QString message = detail.isEmpty()
                                ? QStringLiteral("Parse error on line %1")
                                      .arg(line)
                                : detail;
    throw KanbanParseError(message, line, column, KanbanErrorKind::Parser,
                           token);
  }

  [[noreturn]] void lexerError(int line = -1) const {
    if (line < 0) line = cursor.currentLine();
    throw KanbanParseError(
        QStringLiteral("Lexical error on line %1. Unrecognized text.").arg(line),
        line, 0, KanbanErrorKind::Lexer);
  }

  void parseHeader() {
    while (!cursor.end() && jsWhitespace(cursor.peek())) cursor.take();
    if (!cursor.starts(QStringLiteral("kanban"), Qt::CaseInsensitive))
      parserError(QStringLiteral("NODE_ID"), 1, 1);
    const QChar next = cursor.peek(6);
    if (!next.isNull() && (next.isLetterOrNumber() || next == QLatin1Char('_')))
      parserError(QStringLiteral("NODE_ID"), 1, 1);
    cursor.take(6);
  }

  void parseStatement() {
    while (!cursor.end() && cursor.peek() == QLatin1Char('\n')) cursor.take();
    if (cursor.end()) return;

    int level = 0;
    while (!cursor.end() && inlineWhitespace(cursor.peek())) {
      ++level;
      cursor.take();
    }
    if (cursor.end()) return;
    if (cursor.peek() == QLatin1Char('\n')) {
      cursor.take();
      return;
    }
    if (cursor.starts(QStringLiteral("%%"))) {
      while (!cursor.end() && cursor.peek() != QLatin1Char('\n')) cursor.take();
      return;
    }
    if (cursor.starts(QStringLiteral(":::"))) {
      parseClass();
      return;
    }
    if (cursor.starts(QStringLiteral("::icon("))) {
      parseIcon();
      return;
    }
    parseNode(level);
  }

  void requireLastNode(const QString& property) {
    if (!rawNodes.isEmpty()) return;
    throw KanbanParseError(
        QStringLiteral("Cannot set properties of undefined (setting '%1')")
            .arg(property),
        0, 0, KanbanErrorKind::Runtime);
  }

  void parseClass() {
    cursor.take(3);
    const int startLine = cursor.currentLine();
    QString value;
    while (!cursor.end() && cursor.peek() != QLatin1Char('\n'))
      value += cursor.take();
    if (value.isEmpty()) parserError(QStringLiteral("NL"), startLine, 1);
    requireLastNode(QStringLiteral("cssClasses"));
    rawNodes.last().cssClasses = sanitized(value);
  }

  void parseIcon() {
    cursor.take(7);
    const int startLine = cursor.currentLine();
    QString value;
    while (!cursor.end() && cursor.peek() != QLatin1Char(')') &&
           cursor.peek() != QLatin1Char('\n'))
      value += cursor.take();
    if (value.isEmpty() || cursor.end() || cursor.peek() != QLatin1Char(')'))
      parserError(QStringLiteral("NL"), startLine, 1);
    cursor.take();
    if (!cursor.end() && cursor.peek() != QLatin1Char('\n'))
      parserError(QStringLiteral("NODE_ID"));
    requireLastNode(QStringLiteral("icon"));
    rawNodes.last().icon = sanitized(value);
  }

  static bool idTerminator(QChar ch) {
    return ch == QLatin1Char('(') || ch == QLatin1Char('[') ||
           ch == QLatin1Char('\n') || ch == QLatin1Char(')') ||
           ch == QLatin1Char('{') || ch == QLatin1Char('}') ||
           ch == QLatin1Char('@');
  }

  QString nodeStart() const {
    static const QStringList starts = {
        QStringLiteral("-)"), QStringLiteral("(-"),
        QStringLiteral("))"), QStringLiteral(")"),
        QStringLiteral("(("), QStringLiteral("{{"),
        QStringLiteral("("),  QStringLiteral("[")};
    for (const QString& value : starts)
      if (cursor.starts(value)) return value;
    return {};
  }

  QString nodeEnd() const {
    static const QStringList ends = {
        QStringLiteral("))"), QStringLiteral(")"), QStringLiteral("]"),
        QStringLiteral("}}"), QStringLiteral("(-"), QStringLiteral("(("),
        QStringLiteral("(")};
    for (const QString& value : ends)
      if (cursor.starts(value)) return value;
    return {};
  }

  KanbanNodeType typeFor(const QString& start, const QString& end) const {
    if (start == QLatin1String("[")) return KanbanNodeType::Rect;
    if (start == QLatin1String("("))
      return end == QLatin1String(")") ? KanbanNodeType::RoundedRect
                                        : KanbanNodeType::Cloud;
    if (start == QLatin1String("((")) return KanbanNodeType::Circle;
    if (start == QLatin1String(")")) return KanbanNodeType::Cloud;
    if (start == QLatin1String("))")) return KanbanNodeType::Bang;
    if (start == QLatin1String("{{")) return KanbanNodeType::Hexagon;
    return KanbanNodeType::Default;
  }

  QString parseQuotedDescription() {
    cursor.take();
    const bool backtick = cursor.peek() == QLatin1Char('`');
    if (backtick) cursor.take();
    QString result;
    while (!cursor.end()) {
      if (backtick && cursor.peek() == QLatin1Char('`') &&
          cursor.peek(1) == QLatin1Char('"')) {
        cursor.take(2);
        return result;
      }
      if (!backtick && cursor.peek() == QLatin1Char('"')) {
        cursor.take();
        return result;
      }
      result += cursor.take();
    }
    parserError(QStringLiteral("1"), cursor.currentLine() + 1, 1);
  }

  QString parseDescription() {
    if (cursor.peek() == QLatin1Char('"')) return parseQuotedDescription();
    QString result;
    while (!cursor.end() && nodeEnd().isEmpty()) result += cursor.take();
    return result;
  }

  QString parseShapeData() {
    cursor.take(2);
    QString result;
    while (!cursor.end()) {
      if (cursor.peek() == QLatin1Char('}')) {
        cursor.take();
        return result;
      }
      if (cursor.peek() != QLatin1Char('"')) {
        result += cursor.take();
        continue;
      }
      result += cursor.take();
      while (!cursor.end() && cursor.peek() != QLatin1Char('"')) {
        if (cursor.peek() == QLatin1Char('\n')) {
          cursor.take();
          while (!cursor.end() && jsWhitespace(cursor.peek())) cursor.take();
          result += QStringLiteral("<br/>");
        } else {
          result += cursor.take();
        }
      }
      if (cursor.end()) parserError(QStringLiteral("1"));
      result += cursor.take();
    }
    parserError(QStringLiteral("1"), cursor.currentLine() + 1, 1);
  }

  void parseNode(int level) {
    const int nodeLine = cursor.currentLine();
    const int nodeColumn = cursor.currentColumn();
    QString id;
    QString description;
    QString start = nodeStart();
    if (start.isEmpty()) {
      if (cursor.peek() == QLatin1Char('@')) lexerError();
      while (!cursor.end() && !idTerminator(cursor.peek())) id += cursor.take();
      if (id.isEmpty()) lexerError();
      start = nodeStart();
      if (start.isEmpty()) {
        addNode(level, id, id, KanbanNodeType::Default, std::nullopt);
        finishNodeLine();
        return;
      }
    }

    cursor.take(start.size());
    const int descriptionColumn = cursor.currentColumn();
    description = parseDescription();
    const QString end = nodeEnd();
    if (description.isEmpty())
      parserError(QStringLiteral("NODE_DEND"), nodeLine, nodeColumn);
    if (end.isEmpty())
      parserError(QStringLiteral("1"), cursor.currentLine() + 1,
                  descriptionColumn);
    cursor.take(end.size());
    if (id.isEmpty()) id = description;

    std::optional<QString> shapeData;
    if (cursor.starts(QStringLiteral("@{"))) shapeData = parseShapeData();
    addNode(level, id, description, typeFor(start, end), shapeData);
    finishNodeLine();
  }

  void finishNodeLine() {
    if (cursor.end() || cursor.peek() == QLatin1Char('\n')) return;
    const QString token = cursor.starts(QStringLiteral(":::"))
                              ? QStringLiteral("CLASS")
                          : cursor.starts(QStringLiteral("::icon("))
                              ? QStringLiteral("ICON")
                              : QStringLiteral("NODE_ID");
    parserError(token, cursor.currentLine(),
                qMax(1, cursor.currentColumn() - 1));
  }

  QJsonObject parseMetadata(const QString& value) const {
    const QString wrapped = value.contains(QLatin1Char('\n'))
                                ? value + QLatin1Char('\n')
                                : QStringLiteral("{\n") + value +
                                      QStringLiteral("\n}");
    try {
      const YAML::Node root = YAML::Load(wrapped.toStdString());
      rejectDuplicateYamlKeys(root);
      const QJsonValue json = yamlToJson(root);
      return json.isObject() ? json.toObject() : QJsonObject();
    } catch (const KanbanParseError&) {
      throw;
    } catch (const YAML::Exception& error) {
      throw KanbanParseError(QString::fromUtf8(error.what()), 0, 0,
                             KanbanErrorKind::Yaml);
    }
  }

  void applyMetadata(RawNode& node, const QString& value) const {
    const QJsonObject doc = parseMetadata(value);
    const QJsonValue shape = doc.value(QStringLiteral("shape"));
    if (jsTruthy(shape)) {
      if (!shape.isString())
        throw KanbanParseError(
            QStringLiteral("doc.shape.toLowerCase is not a function"), 0, 0,
            KanbanErrorKind::Runtime);
      const QString shapeText = shape.toString();
      if (shapeText != shapeText.toLower() ||
          shapeText.contains(QLatin1Char('_'))) {
        throw KanbanParseError(
            QStringLiteral("No such shape: %1. Shape names should be lowercase.")
                .arg(shapeText),
            0, 0, KanbanErrorKind::Runtime);
      }
      if (shapeText == QLatin1String("kanbanItem")) node.shape = shapeText;
    }
    const QJsonValue label = doc.value(QStringLiteral("label"));
    if (jsTruthy(label)) node.label = jsToString(label);
    const auto stringField = [&](QStringView key, QString& target) {
      const QJsonValue field = doc.value(key.toString());
      if (jsTruthy(field)) target = jsToString(field);
    };
    stringField(QStringLiteral("icon"), node.icon);
    stringField(QStringLiteral("assigned"), node.assigned);
    stringField(QStringLiteral("ticket"), node.ticket);
    const QJsonValue priority = doc.value(QStringLiteral("priority"));
    if (jsTruthy(priority)) node.priority = priority;
  }

  void addNode(int level, QString id, QString description,
               KanbanNodeType type,
               const std::optional<QString>& shapeData) {
    RawNode node;
    node.id = sanitized(std::move(id));
    if (node.id.isEmpty()) node.id = QStringLiteral("kbn%1").arg(counter++);
    node.level = level;
    node.label = sanitized(std::move(description));
    node.width = config.mindmapMaxNodeWidth;
    node.padding = config.mindmapPadding;
    node.isGroup = false;
    if (type == KanbanNodeType::RoundedRect || type == KanbanNodeType::Rect ||
        type == KanbanNodeType::Hexagon) {
      node.padding = editor::jsNumberValue(node.padding) * 2.0;
    }
    if (shapeData.has_value()) applyMetadata(node, *shapeData);

    int sectionIndex = -1;
    if (!rawNodes.isEmpty()) {
      const int sectionLevel = rawNodes.first().level;
      for (qsizetype i = rawNodes.size(); i-- > 0;) {
        if (rawNodes.at(i).level == sectionLevel && sectionIndex < 0)
          sectionIndex = int(i);
        if (rawNodes.at(i).level < sectionLevel) {
          throw KanbanParseError(
              QStringLiteral(
                  "Items without section detected, found section (\"%1\")")
                  .arg(rawNodes.at(i).label),
              0, 0, KanbanErrorKind::Runtime);
        }
      }
      if (sectionIndex >= 0 && level == rawNodes.at(sectionIndex).level)
        sectionIndex = -1;
    }
    if (sectionIndex >= 0)
      node.parentId = rawNodes.at(sectionIndex).id.isEmpty()
                          ? QStringLiteral("kbn%1").arg(counter++)
                          : rawNodes.at(sectionIndex).id;
    rawNodes.push_back(std::move(node));
    if (sectionIndex < 0) sectionIndices.push_back(rawNodes.size() - 1);
  }

  KanbanData materialize() const {
    KanbanData data;
    data.sections.reserve(sectionIndices.size());
    for (const qsizetype index : sectionIndices)
      data.sections.push_back(rawNodes.at(index));

    for (const qsizetype sectionIndex : sectionIndices) {
      const RawNode& section = rawNodes.at(sectionIndex);
      KanbanNode group;
      group.id = section.id;
      group.level = section.level;
      group.label = sanitized(section.label);
      group.isGroup = true;
      group.ticket = section.ticket;
      group.shape = QStringLiteral("kanbanSection");
      group.look = config.look;
      data.nodes.push_back(std::move(group));

      for (const RawNode& item : rawNodes) {
        if (item.parentId != section.id) continue;
        KanbanNode child;
        child.id = item.id;
        child.level = item.level;
        child.label = sanitized(item.label);
        child.parentId = section.id;
        child.isGroup = false;
        child.ticket = item.ticket;
        child.priority = item.priority;
        child.assigned = item.assigned;
        child.icon = item.icon;
        child.shape = QStringLiteral("kanbanItem");
        data.nodes.push_back(std::move(child));
      }
    }
    return data;
  }

  Cursor cursor;
  KanbanParseConfig config;
  QVector<RawNode> rawNodes;
  QVector<qsizetype> sectionIndices;
  int counter = 0;
};

}  // namespace

KanbanData KanbanDiagram::parse(const QString& source,
                                const KanbanParseConfig& config) {
  return Parser(source, config).parse();
}

}  // namespace muffin::mermaid::kanban
