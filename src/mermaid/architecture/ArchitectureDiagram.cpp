#include "mermaid/architecture/ArchitectureDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <optional>
#include <utility>

namespace muffin::mermaid::architecture {

ArchitectureParseError::ArchitectureParseError(ArchitectureErrorKind kind,
                                               int line, int column,
                                               QString token,
                                               const QString& message)
    : std::runtime_error(message.toUtf8().constData()),
      kind(kind),
      line(line),
      column(column),
      token(std::move(token)) {}

namespace {

bool inlineWhitespace(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

bool asciiWord(QChar ch) {
  return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
         (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
         (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
         ch == QLatin1Char('_');
}

bool idBody(QChar ch) { return asciiWord(ch) || ch == QLatin1Char('-'); }

QString collapseInline(QString value) {
  value = value.trimmed();
  value.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")),
                QStringLiteral(" "));
  return value;
}

QString normalizeBlock(QString value) {
  value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  value = value.trimmed();
  value.replace(QRegularExpression(QStringLiteral(R"(\n\s+)")),
                QStringLiteral("\n"));
  return value;
}

QString sanitizeMetadata(QString value) {
  return HtmlSanitizer().sanitizedMermaidText(std::move(value));
}

QString decodeQuoted(const QString& raw) {
  if (raw.size() < 2) return raw;
  const QChar quote = raw.front();
  if ((quote != QLatin1Char('\'') && quote != QLatin1Char('"')) ||
      raw.back() != quote)
    return raw;
  QString result;
  result.reserve(raw.size() - 2);
  for (qsizetype i = 1; i + 1 < raw.size(); ++i) {
    QChar ch = raw.at(i);
    if (ch == QLatin1Char('\\') && i + 2 < raw.size()) {
      ch = raw.at(++i);
      if (ch == QLatin1Char('n')) result += QLatin1Char('\n');
      else if (ch == QLatin1Char('r')) result += QLatin1Char('\r');
      else if (ch == QLatin1Char('t')) result += QLatin1Char('\t');
      else result += ch;
    } else {
      result += ch;
    }
  }
  return result;
}

class LineCursor {
public:
  LineCursor(QString line, int lineNumber)
      : line_(std::move(line)), lineNumber_(lineNumber) {}

  bool atEnd() const { return index_ >= line_.size(); }
  QChar peek(qsizetype ahead = 0) const {
    return index_ + ahead < line_.size() ? line_.at(index_ + ahead) : QChar();
  }
  int column() const { return int(index_) + 1; }
  int lineNumber() const { return lineNumber_; }

  void whitespace() {
    while (!atEnd() && inlineWhitespace(peek())) ++index_;
  }

  bool keyword(QLatin1String value) {
    if (line_.mid(index_, value.size()) != value) return false;
    index_ += value.size();
    return true;
  }

  bool take(QChar value) {
    if (peek() != value) return false;
    ++index_;
    return true;
  }

  QString id() {
    if (!asciiWord(peek())) return {};
    const qsizetype start = index_++;
    while (!atEnd() && idBody(peek())) ++index_;
    while (index_ > start && line_.at(index_ - 1) == QLatin1Char('-')) --index_;
    return line_.mid(start, index_ - start);
  }

  QString icon(bool* present = nullptr) {
    if (present) *present = false;
    if (!take(QLatin1Char('('))) return {};
    const qsizetype start = index_;
    while (!atEnd() && (asciiWord(peek()) || peek() == QLatin1Char('-') ||
                        peek() == QLatin1Char(':')))
      ++index_;
    if (index_ == start || !take(QLatin1Char(')')))
      fail(ArchitectureErrorKind::Lexer, QStringLiteral("Invalid architecture icon"));
    if (present) *present = true;
    return line_.mid(start, index_ - start - 1).trimmed();
  }

  QString string(bool* present = nullptr) {
    if (present) *present = false;
    const QChar quote = peek();
    if (quote != QLatin1Char('\'') && quote != QLatin1Char('"')) return {};
    const qsizetype start = index_++;
    bool escaped = false;
    while (!atEnd()) {
      const QChar ch = line_.at(index_++);
      if (escaped) {
        escaped = false;
      } else if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == quote) {
        if (present) *present = true;
        return decodeQuoted(line_.mid(start, index_ - start));
      }
    }
    fail(ArchitectureErrorKind::Lexer, QStringLiteral("Unterminated architecture string"));
  }

  QString title(bool* present = nullptr) {
    if (present) *present = false;
    if (!take(QLatin1Char('['))) return {};
    const qsizetype start = index_;
    QString result;
    if (peek() == QLatin1Char('\'') || peek() == QLatin1Char('"')) {
      bool quoted = false;
      result = string(&quoted);
      if (!quoted || !take(QLatin1Char(']')))
        fail(ArchitectureErrorKind::Lexer, QStringLiteral("Invalid architecture title"));
    } else {
      while (!atEnd() && (asciiWord(peek()) || peek() == QLatin1Char(' ')))
        ++index_;
      if (index_ == start || !take(QLatin1Char(']')))
        failAt(ArchitectureErrorKind::Lexer, int(start) + 1,
               QStringLiteral("Invalid architecture title"));
      result = line_.mid(start, index_ - start - 1).trimmed();
    }
    if (present) *present = true;
    return result.trimmed();
  }

  bool groupModifier() {
    constexpr QLatin1String marker("{group}");
    return keyword(marker);
  }

  QChar direction() {
    const QChar value = peek();
    if (value != QLatin1Char('L') && value != QLatin1Char('R') &&
        value != QLatin1Char('T') && value != QLatin1Char('B'))
      fail(ArchitectureErrorKind::Parser,
           QStringLiteral("Expected architecture direction"));
    ++index_;
    return value;
  }

  void requireEnd() {
    whitespace();
    if (!atEnd() && peek() == QLatin1Char(';'))
      fail(ArchitectureErrorKind::Lexer,
           QStringLiteral("Invalid architecture character"));
    if (!atEnd())
      fail(ArchitectureErrorKind::Parser,
           QStringLiteral("Unexpected architecture token"));
  }

  [[noreturn]] void fail(ArchitectureErrorKind kind,
                         const QString& message) const {
    const QString value = atEnd() ? QString() : QString(peek());
    throw ArchitectureParseError(kind, lineNumber_, column(), value, message);
  }

  [[noreturn]] void failAt(ArchitectureErrorKind kind, int column,
                           const QString& message) const {
    throw ArchitectureParseError(kind, lineNumber_, column, {}, message);
  }

private:
  QString line_;
  int lineNumber_ = 1;
  qsizetype index_ = 0;
};

struct ParsedStatements {
  ArchitectureData data;
};

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source_.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  ArchitectureData run() {
    QStringList lines = source_.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    bool foundHeader = false;
    for (int i = 0; i < lines.size(); ++i) {
      QString line = stripHidden(lines.at(i));
      if (line.trimmed().isEmpty()) continue;
      if (!foundHeader) {
        const qsizetype begin = leadingWhitespace(line);
        const bool headerText =
            line.mid(begin, 17) == QLatin1String("architecture-beta");
        const bool headerBoundary =
            begin + 17 >= line.size() || inlineWhitespace(line.at(begin + 17));
        if (!headerText || !headerBoundary)
          throw ArchitectureParseError(
              ArchitectureErrorKind::Parser, i + 1, int(begin) + 1,
              line.mid(begin).section(QLatin1Char(' '), 0, 0),
              QStringLiteral("Expected architecture-beta header"));
        foundHeader = true;
        line = line.mid(begin + 17);
        if (line.trimmed().isEmpty()) continue;
      }
      const int statementLine = i + 1;
      if (line.trimmed().startsWith(QStringLiteral("accDescr")) &&
          line.contains(QLatin1Char('{')) &&
          !line.mid(line.indexOf(QLatin1Char('{')) + 1)
               .contains(QLatin1Char('}'))) {
        while (++i < lines.size()) {
          line += QLatin1Char('\n') + lines.at(i);
          if (lines.at(i).contains(QLatin1Char('}'))) break;
        }
      }
      parseStatement(line, statementLine);
    }
    if (!foundHeader)
      throw ArchitectureParseError(ArchitectureErrorKind::Parser, 1, 1, {},
                                   QStringLiteral("Expected architecture-beta header"));
    validateDatabase();
    return std::move(parsed_.data);
  }

private:
  static qsizetype leadingWhitespace(const QString& value) {
    qsizetype result = 0;
    while (result < value.size() && inlineWhitespace(value.at(result))) ++result;
    return result;
  }

  static QString stripHidden(QString line) {
    bool quoted = false;
    QChar quote;
    bool escaped = false;
    int squareDepth = 0;
    for (qsizetype i = 0; i + 1 < line.size(); ++i) {
      const QChar ch = line.at(i);
      if (quoted) {
        if (escaped) escaped = false;
        else if (ch == QLatin1Char('\\')) escaped = true;
        else if (ch == quote) quoted = false;
        continue;
      }
      if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
        quoted = true;
        quote = ch;
      } else if (ch == QLatin1Char('[')) {
        ++squareDepth;
      } else if (ch == QLatin1Char(']') && squareDepth > 0) {
        --squareDepth;
      } else if (squareDepth == 0 && ch == QLatin1Char('%') &&
                 line.at(i + 1) == QLatin1Char('%')) {
        line.truncate(i);
        break;
      }
    }
    return line;
  }

  void parseStatement(QString line, int lineNumber) {
    const qsizetype begin = leadingWhitespace(line);
    line = line.mid(begin);
    if (line.isEmpty()) return;
    if (line.startsWith(QStringLiteral("title")) &&
        (line.size() == 5 || inlineWhitespace(line.at(5)))) {
      const QString value = sanitizeMetadata(collapseInline(line.mid(5)));
      parsed_.data.title = value;
      parsed_.data.hasTitleDirective = true;
      return;
    }
    if (line.startsWith(QStringLiteral("accTitle"))) {
      const int colon = line.indexOf(QLatin1Char(':'));
      if (colon < 0)
        throw ArchitectureParseError(ArchitectureErrorKind::Lexer, lineNumber,
                                     1, {}, QStringLiteral("Invalid accTitle"));
      parsed_.data.accTitle =
          sanitizeMetadata(collapseInline(line.mid(colon + 1)));
      return;
    }
    if (line.startsWith(QStringLiteral("accDescr"))) {
      parseAccDescr(line, lineNumber);
      return;
    }
    LineCursor cursor(line, lineNumber);
    cursor.whitespace();
    if (cursor.keyword(QLatin1String("group"))) return parseGroup(cursor);
    if (cursor.keyword(QLatin1String("service"))) return parseService(cursor);
    if (cursor.keyword(QLatin1String("junction"))) return parseJunction(cursor);
    if (cursor.keyword(QLatin1String("align"))) return parseAlignment(cursor);
    parseEdge(cursor);
  }

  void parseAccDescr(const QString& firstLine, int lineNumber) {
    const int colon = firstLine.indexOf(QLatin1Char(':'));
    const int brace = firstLine.indexOf(QLatin1Char('{'));
    QString value;
    if (colon >= 0 && (brace < 0 || colon < brace)) {
      value = collapseInline(firstLine.mid(colon + 1));
    } else if (brace >= 0) {
      value = firstLine.mid(brace + 1);
      const int close = value.indexOf(QLatin1Char('}'));
      if (close < 0)
        throw ArchitectureParseError(ArchitectureErrorKind::Lexer, lineNumber,
                                     brace + 1, {},
                                     QStringLiteral("Unterminated accDescr"));
      value.truncate(close);
      value = normalizeBlock(value);
    } else {
      throw ArchitectureParseError(ArchitectureErrorKind::Lexer, lineNumber,
                                   1, {}, QStringLiteral("Invalid accDescr"));
    }
    parsed_.data.accDescr = sanitizeMetadata(value);
  }

  void parseGroup(LineCursor& cursor) {
    cursor.whitespace();
    ArchitectureGroup group;
    group.id = cursor.id();
    if (group.id.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected group id"));
    cursor.whitespace();
    group.icon = cursor.icon(&group.hasIcon);
    cursor.whitespace();
    group.title = cursor.title(&group.hasTitle);
    cursor.whitespace();
    if (cursor.keyword(QLatin1String("in"))) {
      cursor.whitespace();
      group.parent = cursor.id();
      if (group.parent.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected group parent"));
      group.hasParent = true;
    }
    cursor.requireEnd();
    parsed_.data.groups.append(std::move(group));
  }

  void parseService(LineCursor& cursor) {
    cursor.whitespace();
    ArchitectureService service;
    service.id = cursor.id();
    if (service.id.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected service id"));
    cursor.whitespace();
    service.iconText = cursor.string(&service.hasIconText);
    if (!service.hasIconText) service.icon = cursor.icon(&service.hasIcon);
    cursor.whitespace();
    service.title = cursor.title(&service.hasTitle);
    cursor.whitespace();
    if (cursor.keyword(QLatin1String("in"))) {
      cursor.whitespace();
      service.parent = cursor.id();
      if (service.parent.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected service parent"));
      service.hasParent = true;
    }
    cursor.requireEnd();
    parsed_.data.services.append(std::move(service));
  }

  void parseJunction(LineCursor& cursor) {
    cursor.whitespace();
    ArchitectureJunction junction;
    junction.id = cursor.id();
    if (junction.id.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected junction id"));
    cursor.whitespace();
    if (cursor.keyword(QLatin1String("in"))) {
      cursor.whitespace();
      junction.parent = cursor.id();
      if (junction.parent.isEmpty()) cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected junction parent"));
      junction.hasParent = true;
    }
    cursor.requireEnd();
    parsed_.data.junctions.append(std::move(junction));
  }

  void parseAlignment(LineCursor& cursor) {
    cursor.whitespace();
    ArchitectureAlignment alignment;
    if (cursor.keyword(QLatin1String("row")))
      alignment.direction = ArchitectureAlignment::Direction::Row;
    else if (cursor.keyword(QLatin1String("column")))
      alignment.direction = ArchitectureAlignment::Direction::Column;
    else
      cursor.fail(ArchitectureErrorKind::Parser,
                  QStringLiteral("Expected row or column"));
    while (true) {
      cursor.whitespace();
      const QString member = cursor.id();
      if (member.isEmpty()) break;
      alignment.members.append(member);
    }
    cursor.requireEnd();
    if (alignment.members.size() < 2)
      cursor.fail(ArchitectureErrorKind::Parser,
                  QStringLiteral("Expected at least two alignment members"));
    parsed_.data.alignments.append(std::move(alignment));
  }

  void parseEdge(LineCursor& cursor) {
    ArchitectureEdge edge;
    edge.lhsId = cursor.id();
    if (edge.lhsId.isEmpty())
      cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected architecture statement"));
    cursor.whitespace();
    edge.lhsGroup = cursor.groupModifier();
    cursor.whitespace();
    if (!cursor.take(QLatin1Char(':')))
      cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected left port"));
    edge.lhsDir = cursor.direction();
    cursor.whitespace();
    if (cursor.peek() == QLatin1Char('<') || cursor.peek() == QLatin1Char('>')) {
      edge.lhsInto = true;
      cursor.take(cursor.peek());
    }
    if (!cursor.take(QLatin1Char('-')))
      cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected architecture arrow"));
    if (cursor.take(QLatin1Char('-'))) {
      // Unlabelled arrow.
    } else {
      edge.title = cursor.title(&edge.hasTitle);
      if (!edge.hasTitle || !cursor.take(QLatin1Char('-')))
        cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Invalid edge title"));
    }
    if (cursor.peek() == QLatin1Char('<') || cursor.peek() == QLatin1Char('>')) {
      edge.rhsInto = true;
      cursor.take(cursor.peek());
    }
    cursor.whitespace();
    edge.rhsDir = cursor.direction();
    if (!cursor.take(QLatin1Char(':')))
      cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected right port"));
    cursor.whitespace();
    edge.rhsId = cursor.id();
    if (edge.rhsId.isEmpty())
      cursor.fail(ArchitectureErrorKind::Parser, QStringLiteral("Expected edge target"));
    cursor.whitespace();
    edge.rhsGroup = cursor.groupModifier();
    cursor.requireEnd();
    parsed_.data.edges.append(std::move(edge));
  }

  [[noreturn]] void runtime(const QString& message) const {
    throw ArchitectureParseError(ArchitectureErrorKind::Runtime, 0, 0, {},
                                 message);
  }

  void validateParent(const QString& kind, const QString& id,
                      const QString& parent,
                      const QHash<QString, QString>& registered) const {
    if (id == parent)
      runtime(QStringLiteral("The %1 [%2] cannot be placed within itself")
                  .arg(kind, id));
    if (!registered.contains(parent))
      runtime(QStringLiteral("The %1 [%2]'s parent does not exist. Please make sure the parent is created before this %1")
                  .arg(kind, id));
    if (registered.value(parent) == QLatin1String("node"))
      runtime(QStringLiteral("The %1 [%2]'s parent is not a group")
                  .arg(kind, id));
  }

  void validateDatabase() {
    QHash<QString, QString> registered;
    for (const ArchitectureGroup& group : std::as_const(parsed_.data.groups)) {
      if (registered.contains(group.id))
        runtime(QStringLiteral("The group id [%1] is already in use by another %2")
                    .arg(group.id, registered.value(group.id)));
      if (group.hasParent)
        validateParent(QStringLiteral("group"), group.id, group.parent, registered);
      registered.insert(group.id, QStringLiteral("group"));
    }
    for (const ArchitectureService& service : std::as_const(parsed_.data.services)) {
      if (registered.contains(service.id))
        runtime(QStringLiteral("The service id [%1] is already in use by another %2")
                    .arg(service.id, registered.value(service.id)));
      if (service.hasParent)
        validateParent(QStringLiteral("service"), service.id, service.parent, registered);
      registered.insert(service.id, QStringLiteral("node"));
    }
    for (const ArchitectureJunction& junction : std::as_const(parsed_.data.junctions)) {
      if (registered.contains(junction.id))
        runtime(QStringLiteral("The junction id [%1] is already in use by another %2")
                    .arg(junction.id, registered.value(junction.id)));
      if (junction.hasParent)
        validateParent(QStringLiteral("junction"), junction.id, junction.parent, registered);
      registered.insert(junction.id, QStringLiteral("node"));
    }
    for (const ArchitectureEdge& edge : std::as_const(parsed_.data.edges)) {
      if (!registered.contains(edge.lhsId))
        runtime(QStringLiteral("The left-hand id [%1] does not yet exist. Please create the service/group before declaring an edge to it.")
                    .arg(edge.lhsId));
      if (!registered.contains(edge.rhsId))
        runtime(QStringLiteral("The right-hand id [%1] does not yet exist. Please create the service/group before declaring an edge to it.")
                    .arg(edge.rhsId));
      const auto serviceParent = [&](const QString& id) {
        for (const ArchitectureService& value : std::as_const(parsed_.data.services))
          if (value.id == id) return value.parent;
        for (const ArchitectureJunction& value : std::as_const(parsed_.data.junctions))
          if (value.id == id) return value.parent;
        return QString();
      };
      const QString lhsParent = serviceParent(edge.lhsId);
      const QString rhsParent = serviceParent(edge.rhsId);
      if (edge.lhsGroup && !lhsParent.isEmpty() && lhsParent == rhsParent)
        runtime(QStringLiteral("The left-hand id [%1] is modified to traverse the group boundary, but the edge does not pass through two groups.")
                    .arg(edge.lhsId));
      if (edge.rhsGroup && !lhsParent.isEmpty() && lhsParent == rhsParent)
        runtime(QStringLiteral("The right-hand id [%1] is modified to traverse the group boundary, but the edge does not pass through two groups.")
                    .arg(edge.rhsId));
    }
    for (const ArchitectureAlignment& alignment : std::as_const(parsed_.data.alignments)) {
      QHash<QString, bool> seen;
      const QString direction = alignment.direction == ArchitectureAlignment::Direction::Row
          ? QStringLiteral("row") : QStringLiteral("column");
      for (const QString& id : alignment.members) {
        if (registered.value(id) != QLatin1String("node"))
          runtime(QStringLiteral("align %1 references [%2], which is not a service or junction")
                      .arg(direction, id));
        if (seen.contains(id))
          runtime(QStringLiteral("align %1 lists [%2] more than once")
                      .arg(direction, id));
        seen.insert(id, true);
      }
    }
  }

  QString source_;
  ParsedStatements parsed_;
};

}  // namespace

ArchitectureData ArchitectureDiagram::parse(const QString& source) {
  return Parser(source).run();
}

}  // namespace muffin::mermaid::architecture
