#include "mermaid/mindmap/MindmapDiagram.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/editor/MermaidRenderSupport.h"

#include <lexbor/dom/interfaces/attr.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
#include <lexbor/html/html.h>

#include <QJsonArray>
#include <QStringList>

#include <cmath>
#include <utility>

namespace muffin::mermaid::mindmap {

MindmapParseError::MindmapParseError(const QString& message, int line,
                                     int column, MindmapErrorKind kind,
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

QString sanitized(QString value) {
  // DOMPurify preserves boundary whitespace while fragment parsing does not.
  qsizetype begin = 0;
  qsizetype end = value.size();
  while (begin < end && value.at(begin).isSpace()) ++begin;
  while (end > begin && value.at(end - 1).isSpace()) --end;
  if (begin == end) return value;
  return value.left(begin) +
         HtmlSanitizer().sanitizedMermaidText(value.mid(begin, end - begin)) +
         value.mid(end);
}

void collectAnchors(lxb_dom_node_t* node, QVector<MindmapAnchor>& anchors,
                    qsizetype& visibleOffset) {
  if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    size_t textLength = 0;
    const lxb_char_t* textData = lxb_dom_node_text_content(node, &textLength);
    if (textData)
      visibleOffset += QString::fromUtf8(
                           reinterpret_cast<const char*>(textData),
                           static_cast<int>(textLength))
                           .size();
    return;
  }
  bool isAnchor = false;
  bool hasHref = false;
  QString href;
  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    auto* element = lxb_dom_interface_element(node);
    size_t tagLength = 0;
    const lxb_char_t* tagData =
        lxb_dom_element_local_name(element, &tagLength);
    const QString tag = QString::fromUtf8(
                            reinterpret_cast<const char*>(tagData),
                            static_cast<int>(tagLength))
                            .toLower();
    if (tag == QLatin1String("a")) {
      isAnchor = true;
      for (lxb_dom_attr_t* attr = lxb_dom_element_first_attribute(element);
           attr != nullptr; attr = lxb_dom_element_next_attribute(attr)) {
        size_t nameLength = 0;
        const lxb_char_t* nameData =
            lxb_dom_attr_qualified_name(attr, &nameLength);
        const QString name = QString::fromUtf8(
                                 reinterpret_cast<const char*>(nameData),
                                 static_cast<int>(nameLength))
                                 .toLower();
        if (name != QLatin1String("href")) continue;
        hasHref = true;
        size_t valueLength = 0;
        const lxb_char_t* valueData = lxb_dom_attr_value(attr, &valueLength);
        if (valueData)
          href = QString::fromUtf8(reinterpret_cast<const char*>(valueData),
                                   static_cast<int>(valueLength));
        break;
      }
    }
  }
  const qsizetype anchorStart = visibleOffset;
  for (lxb_dom_node_t* child = node->first_child; child != nullptr;
       child = child->next)
    collectAnchors(child, anchors, visibleOffset);
  if (isAnchor && hasHref) {
    size_t textLength = 0;
    const lxb_char_t* textData = lxb_dom_node_text_content(node, &textLength);
    MindmapAnchor anchor;
    anchor.href = std::move(href);
    if (textData)
      anchor.label = QString::fromUtf8(reinterpret_cast<const char*>(textData),
                                       static_cast<int>(textLength));
    anchor.start = anchorStart;
    anchor.length = visibleOffset - anchorStart;
    anchors.append(std::move(anchor));
  }
}

QVector<MindmapAnchor> htmlAnchors(const QString& sanitizedHtml) {
  QVector<MindmapAnchor> result;
  lxb_html_document_t* document = lxb_html_document_create();
  if (!document) return result;
  const QByteArray utf8 = sanitizedHtml.toUtf8();
  const lxb_status_t status = lxb_html_document_parse(
      document, reinterpret_cast<const lxb_char_t*>(utf8.constData()),
      static_cast<size_t>(utf8.size()));
  if (status == LXB_STATUS_OK) {
    if (lxb_html_body_element_t* body =
            lxb_html_document_body_element(document)) {
      qsizetype visibleOffset = 0;
      collectAnchors(lxb_dom_interface_node(body), result, visibleOffset);
    }
  }
  lxb_html_document_destroy(document);
  return result;
}

QJsonValue sourceEntryScalar(QJsonValue value, QJsonValue fallback) {
  if (value.isUndefined() || value.isNull() || value.isArray() ||
      value.isObject())
    return fallback;
  return value;
}

enum class TokenKind {
  SpaceLine,
  Newline,
  Mindmap,
  Eof,
  SpaceList,
  Icon,
  CssClass,
  NodeStart,
  NodeDescr,
  NodeEnd,
  NodeId,
};

struct Token {
  TokenKind kind = TokenKind::Eof;
  QString text;
  int line = 1;
  int column = 1;
};

QString tokenName(TokenKind kind) {
  switch (kind) {
    case TokenKind::SpaceLine: return QStringLiteral("SPACELINE");
    case TokenKind::Newline: return QStringLiteral("NL");
    case TokenKind::Mindmap: return QStringLiteral("MINDMAP");
    case TokenKind::Eof: return QStringLiteral("EOF");
    case TokenKind::SpaceList: return QStringLiteral("SPACELIST");
    case TokenKind::Icon: return QStringLiteral("ICON");
    case TokenKind::CssClass: return QStringLiteral("CLASS");
    case TokenKind::NodeStart: return QStringLiteral("NODE_DSTART");
    case TokenKind::NodeDescr: return QStringLiteral("NODE_DESCR");
    case TokenKind::NodeEnd: return QStringLiteral("NODE_DEND");
    case TokenKind::NodeId: return QStringLiteral("NODE_ID");
  }
  return {};
}

class Lexer {
public:
  explicit Lexer(QString source) : source_(std::move(source)) {}

  QVector<Token> scan() {
    QVector<Token> result;
    while (true) {
      Token token = next();
      result.append(token);
      if (token.kind == TokenKind::Eof) break;
    }
    return result;
  }

private:
  enum class State { Initial, CssClass, Icon, Node, Quoted, MarkdownQuoted };

  bool starts(QStringView value, Qt::CaseSensitivity cs = Qt::CaseInsensitive) const {
    return QStringView(source_).mid(pos_, value.size()).compare(value, cs) == 0;
  }

  void consume(qsizetype count) {
    const qsizetype end = std::min(pos_ + count, source_.size());
    while (pos_ < end) {
      const QChar ch = source_.at(pos_++);
      if (ch == QLatin1Char('\n')) {
        ++line_;
        column_ = 1;
      } else {
        ++column_;
      }
    }
  }

  Token take(TokenKind kind, qsizetype count) {
    Token result{kind, source_.mid(pos_, count), line_, column_};
    consume(count);
    return result;
  }

  [[noreturn]] void lexicalError() const {
    throw MindmapParseError(
        QStringLiteral("Lexical error on line %1. Unrecognized text.").arg(line_),
        line_, column_, MindmapErrorKind::Lexer);
  }

  Token next() {
    if (pos_ >= source_.size()) return {TokenKind::Eof, {}, line_, column_};
    if (state_ == State::CssClass) {
      if (source_.at(pos_) == QLatin1Char('\n')) {
        state_ = State::Initial;
        consume(1);
        return next();
      }
      qsizetype end = pos_;
      while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
             source_.at(end) != QLatin1Char('\r'))
        ++end;
      if (end == pos_) lexicalError();
      Token result = take(TokenKind::CssClass, end - pos_);
      return result;
    }
    if (state_ == State::Icon) {
      if (source_.at(pos_) == QLatin1Char(')')) {
        state_ = State::Initial;
        consume(1);
        return next();
      }
      qsizetype end = pos_;
      while (end < source_.size() && source_.at(end) != QLatin1Char(')')) ++end;
      if (end == pos_) lexicalError();
      return take(TokenKind::Icon, end - pos_);
    }
    if (state_ == State::Quoted || state_ == State::MarkdownQuoted) {
      const bool markdown = state_ == State::MarkdownQuoted;
      const QStringView closing = markdown ? QStringView(u"`\"") : QStringView(u"\"");
      if (starts(closing, Qt::CaseSensitive)) {
        state_ = State::Node;
        consume(closing.size());
        return next();
      }
      qsizetype end = pos_;
      while (end < source_.size()) {
        const QChar ch = source_.at(end);
        if ((!markdown && ch == QLatin1Char('"')) ||
            (markdown && (ch == QLatin1Char('`') || ch == QLatin1Char('"'))))
          break;
        ++end;
      }
      if (end == pos_) lexicalError();
      return take(TokenKind::NodeDescr, end - pos_);
    }
    if (state_ == State::Node) {
      if (starts(QStringView(u"\"`"), Qt::CaseSensitive)) {
        state_ = State::MarkdownQuoted;
        consume(2);
        return next();
      }
      if (source_.at(pos_) == QLatin1Char('"')) {
        state_ = State::Quoted;
        consume(1);
        return next();
      }
      static const QVector<QString> endings = {
          QStringLiteral("))"), QStringLiteral(")"), QStringLiteral("]"),
          QStringLiteral("}}"), QStringLiteral("(-"), QStringLiteral("-)"),
          QStringLiteral("(("), QStringLiteral("(")};
      for (const QString& ending : endings) {
        if (starts(ending)) {
          state_ = State::Initial;
          return take(TokenKind::NodeEnd, ending.size());
        }
      }
      qsizetype end = pos_;
      while (end < source_.size()) {
        const QChar ch = source_.at(end);
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']') ||
            ch == QLatin1Char('(') || ch == QLatin1Char('}'))
          break;
        ++end;
      }
      if (end == pos_) {
        // The final catch-all NODE rule consumes any remaining character.
        return take(TokenKind::NodeDescr, 1);
      }
      return take(TokenKind::NodeDescr, end - pos_);
    }

    // INITIAL rule 0: \s*%%.*. The whitespace prefix can span lines.
    qsizetype comment = pos_;
    while (comment < source_.size() && jsWhitespace(source_.at(comment))) ++comment;
    if (QStringView(source_).mid(comment, 2) == QStringView(u"%%")) {
      qsizetype end = comment + 2;
      while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
             source_.at(end) != QLatin1Char('\r'))
        ++end;
      return take(TokenKind::SpaceLine, end - pos_);
    }
    if (starts(QStringView(u"mindmap"))) {
      const qsizetype after = pos_ + 7;
      const bool wordBoundary =
          after >= source_.size() ||
          !(source_.at(after).isLetterOrNumber() || source_.at(after) == QLatin1Char('_'));
      if (wordBoundary) return take(TokenKind::Mindmap, 7);
    }
    if (starts(QStringView(u":::"))) {
      state_ = State::CssClass;
      consume(3);
      return next();
    }
    if (starts(QStringView(u"::icon("))) {
      state_ = State::Icon;
      consume(7);
      return next();
    }

    // SPACELINE consumes a contiguous JS-whitespace run only through its last
    // newline; trailing indentation remains for SPACELIST.
    if (jsWhitespace(source_.at(pos_))) {
      qsizetype end = pos_;
      qsizetype lastNewline = -1;
      while (end < source_.size() && jsWhitespace(source_.at(end))) {
        if (source_.at(end) == QLatin1Char('\n')) lastNewline = end;
        ++end;
      }
      if (lastNewline > pos_)
        return take(TokenKind::SpaceLine, lastNewline - pos_ + 1);
      if (source_.at(pos_) == QLatin1Char('\n')) {
        end = pos_;
        while (end < source_.size() && source_.at(end) == QLatin1Char('\n')) ++end;
        return take(TokenKind::Newline, end - pos_);
      }
      return take(TokenKind::SpaceList, end - pos_);
    }

    static const QVector<QString> startsInOrder = {
        QStringLiteral("-)"), QStringLiteral("(-"), QStringLiteral("))"),
        QStringLiteral(")"), QStringLiteral("(("), QStringLiteral("{{"),
        QStringLiteral("("), QStringLiteral("[")};
    for (const QString& opening : startsInOrder) {
      if (starts(opening)) {
        state_ = State::Node;
        return take(TokenKind::NodeStart, opening.size());
      }
    }
    qsizetype end = pos_;
    while (end < source_.size()) {
      const QChar ch = source_.at(end);
      if (ch == QLatin1Char('(') || ch == QLatin1Char('[') ||
          ch == QLatin1Char('\n') || ch == QLatin1Char(')') ||
          ch == QLatin1Char('{') || ch == QLatin1Char('}'))
        break;
      ++end;
    }
    if (end > pos_) return take(TokenKind::NodeId, end - pos_);
    lexicalError();
  }

  QString source_;
  qsizetype pos_ = 0;
  int line_ = 1;
  int column_ = 1;
  State state_ = State::Initial;
};

class Parser {
public:
  Parser(QString source, MindmapParseConfig config)
      : tokens_(Lexer(std::move(source)).scan()) {
    data_.config = std::move(config);
    data_.config.padding = sourceEntryScalar(
        data_.config.padding, QJsonValue(10.0));
    data_.config.maxNodeWidth = sourceEntryScalar(
        data_.config.maxNodeWidth, QJsonValue(200.0));
    data_.config.useMaxWidth = sourceEntryScalar(
        data_.config.useMaxWidth, QJsonValue(true));
    data_.effectiveLayout = data_.config.userDefinedLayout
                                ? data_.config.layout
                                : QStringLiteral("cose-bilkent");
  }

  MindmapData parse() {
    bool hadLeadingSpaceLines = false;
    while (peek().kind == TokenKind::SpaceLine ||
           (hadLeadingSpaceLines && peek().kind == TokenKind::Newline)) {
      if (peek().kind == TokenKind::SpaceLine) hadLeadingSpaceLines = true;
      take();
    }
    const Token header = expect(TokenKind::Mindmap);

    // MINDMAP followed directly by EOF is not a complete grammar production.
    if (peek().kind == TokenKind::Eof)
      throw MindmapParseError(
          QStringLiteral("Parse error on line %1: Unexpected EOF")
              .arg(header.line + 1),
          header.line + 1, header.column + header.text.size(),
          MindmapErrorKind::Parser, QStringLiteral("EOF"));
    // A blank run immediately after MINDMAP reduces as the diagram stop. A
    // following indented node is therefore the unexpected SPACELIST; the same
    // run followed by EOF is a valid empty document.
    if (peek().kind == TokenKind::SpaceLine) {
      take();
      if (peek().kind != TokenKind::Eof)
        throw MindmapParseError(
            QStringLiteral("Parse error on line %1: Unexpected %2")
                .arg(peek().line)
                .arg(tokenName(peek().kind)),
            peek().line, header.column + header.text.size(),
            MindmapErrorKind::Parser, tokenName(peek().kind));
      finalize();
      return std::move(data_);
    }
    if (peek().kind == TokenKind::Newline) {
      take();
      if (peek().kind == TokenKind::Eof) {
        finalize();
        return std::move(data_);
      }
    }

    bool sawStatement = false;
    while (peek().kind != TokenKind::Eof) {
      if (peek().kind == TokenKind::Newline) {
        take();
        continue;
      }
      if (peek().kind == TokenKind::SpaceLine) {
        take();
        sawStatement = true;
        continue;
      }
      parseStatement();
      sawStatement = true;
      if (peek().kind == TokenKind::Newline) {
        take();
        continue;
      }
      if (peek().kind == TokenKind::SpaceLine) continue;
      // CLASS consumes its terminating newline, so the next statement can
      // start immediately at token level.
      if (peek().kind != TokenKind::Eof && lastWasClass_) continue;
      if (peek().kind != TokenKind::Eof) parserError(peek());
    }
    if (!sawStatement) parserError(peek());
    finalize();
    return std::move(data_);
  }

private:
  const Token& peek(int offset = 0) const {
    return tokens_.at(
        std::min<qsizetype>(index_ + offset, tokens_.size() - 1));
  }

  Token take() { return tokens_.at(index_++); }

  Token expect(TokenKind kind) {
    if (peek().kind != kind) parserError(peek());
    return take();
  }

  [[noreturn]] void parserError(const Token& token) const {
    throw MindmapParseError(
        QStringLiteral("Parse error on line %1: Unexpected %2")
            .arg(token.line)
            .arg(tokenName(token.kind)),
        token.line, token.column, MindmapErrorKind::Parser,
        tokenName(token.kind));
  }

  void parseStatement() {
    lastWasClass_ = false;
    int rawLevel = 0;
    if (peek().kind == TokenKind::SpaceList)
      rawLevel = take().text.size();

    if (peek().kind == TokenKind::Icon) {
      decorate(QStringLiteral("icon"), take().text);
      return;
    }
    if (peek().kind == TokenKind::CssClass) {
      decorate(QStringLiteral("class"), take().text);
      lastWasClass_ = true;
      return;
    }
    if (peek().kind == TokenKind::Newline || peek().kind == TokenKind::Eof)
      return;  // SPACELIST-only statement

    QString id;
    QString descr;
    QString opening;
    QString ending;
    if (peek().kind == TokenKind::NodeId) {
      id = take().text;
      descr = id;
      if (peek().kind == TokenKind::NodeStart) {
        opening = take().text;
        const Token description = expect(TokenKind::NodeDescr);
        descr = description.text;
        if (peek().kind == TokenKind::Eof)
          throw MindmapParseError(
              QStringLiteral("Parse error on line %1: Unexpected end of input")
                  .arg(description.line + 1),
              description.line + 1, description.column,
              MindmapErrorKind::Parser, QStringLiteral("1"));
        ending = expect(TokenKind::NodeEnd).text;
      }
    } else if (peek().kind == TokenKind::NodeStart) {
      opening = take().text;
      const Token description = expect(TokenKind::NodeDescr);
      descr = description.text;
      if (peek().kind == TokenKind::Eof)
        throw MindmapParseError(
            QStringLiteral("Parse error on line %1: Unexpected end of input")
                .arg(description.line + 1),
            description.line + 1, description.column,
            MindmapErrorKind::Parser, QStringLiteral("1"));
      ending = expect(TokenKind::NodeEnd).text;
      id = descr;
    } else {
      parserError(peek());
    }
    addNode(rawLevel, id, descr, nodeType(opening, ending));
  }

  static MindmapNodeType nodeType(const QString& opening,
                                  const QString& ending) {
    if (opening == QLatin1String("[")) return MindmapNodeType::Rect;
    if (opening == QLatin1String("("))
      return ending == QLatin1String(")") ? MindmapNodeType::RoundedRect
                                           : MindmapNodeType::Cloud;
    if (opening == QLatin1String("((")) return MindmapNodeType::Circle;
    if (opening == QLatin1String(")")) return MindmapNodeType::Cloud;
    if (opening == QLatin1String("))")) return MindmapNodeType::Bang;
    if (opening == QLatin1String("{{")) return MindmapNodeType::Hexagon;
    return MindmapNodeType::Default;
  }

  void addNode(int rawLevel, const QString& id, const QString& descr,
               MindmapNodeType type) {
    MindmapNode node;
    node.id = data_.nodes.size();
    node.nodeId = sanitized(id);
    node.descr = sanitized(descr);
    node.anchors = htmlAnchors(node.descr);
    node.type = type;
    node.width = data_.config.maxNodeWidth;
    node.padding = data_.config.padding;
    if (type == MindmapNodeType::RoundedRect || type == MindmapNodeType::Rect ||
        type == MindmapNodeType::Hexagon)
      node.padding = editor::jsNumberValue(node.padding) * 2.0;

    if (data_.nodes.isEmpty()) {
      baseLevel_ = rawLevel;
      node.level = 0;
      node.isRoot = true;
      data_.rootId = node.id;
    } else {
      node.level = rawLevel - baseLevel_;
      for (int i = data_.nodes.size() - 1; i >= 0; --i) {
        if (data_.nodes.at(i).level < node.level) {
          node.parentId = data_.nodes.at(i).id;
          break;
        }
      }
      if (node.parentId < 0) {
        throw MindmapParseError(
            QStringLiteral("There can be only one root. No parent could be found for (\"%1\")")
                .arg(node.descr),
            0, 0, MindmapErrorKind::Runtime);
      }
    }
    data_.nodes.append(std::move(node));
    if (data_.nodes.back().parentId >= 0)
      data_.nodes[data_.nodes.back().parentId].children.append(data_.nodes.back().id);
  }

  void decorate(const QString& field, const QString& value) {
    if (data_.nodes.isEmpty()) {
      throw MindmapParseError(
          QStringLiteral("Cannot set properties of undefined (setting '%1')")
              .arg(field),
          0, 0, MindmapErrorKind::Runtime);
    }
    if (field == QLatin1String("icon"))
      data_.nodes.back().icon = sanitized(value);
    else
      data_.nodes.back().cssClass = sanitized(value);
  }

  QString shapeFor(const MindmapNode& node) const {
    switch (node.type) {
      case MindmapNodeType::Circle: return QStringLiteral("mindmapCircle");
      case MindmapNodeType::Rect: return QStringLiteral("rect");
      case MindmapNodeType::RoundedRect: return QStringLiteral("rounded");
      case MindmapNodeType::Cloud: return QStringLiteral("cloud");
      case MindmapNodeType::Bang: return QStringLiteral("bang");
      case MindmapNodeType::Hexagon: return QStringLiteral("hexagon");
      case MindmapNodeType::Default:
        return data_.config.theme.contains(QLatin1String("redux"),
                                           Qt::CaseInsensitive)
                   ? QStringLiteral("rounded")
                   : QStringLiteral("defaultMindmapNode");
    }
    return QStringLiteral("rect");
  }

  void assignSections(int nodeId, int section = -1) {
    MindmapNode& node = data_.nodes[nodeId];
    node.hasSection = node.level != 0;
    node.section = section;
    for (int i = 0; i < node.children.size(); ++i) {
      const int childSection = node.level == 0 ? i % 11 : section;
      assignSections(node.children.at(i), childSection);
    }
  }

  void generateEdges(int nodeId) {
    const MindmapNode& node = data_.nodes.at(nodeId);
    for (int childId : node.children) {
      const MindmapNode& child = data_.nodes.at(childId);
      MindmapEdge edge;
      edge.id = QStringLiteral("edge_%1_%2").arg(node.id).arg(child.id);
      edge.start = node.id;
      edge.end = child.id;
      edge.look = data_.config.look;
      edge.depth = node.level;
      edge.hasSection = child.hasSection;
      edge.section = child.section;
      edge.classes = QStringLiteral("edge");
      if (child.hasSection)
        edge.classes += QStringLiteral(" section-edge-%1").arg(child.section);
      edge.classes += QStringLiteral(" edge-depth-%1").arg(node.level + 1);
      data_.edges.append(std::move(edge));
      generateEdges(childId);
    }
  }

  void finalize() {
    if (data_.rootId < 0) return;
    assignSections(data_.rootId);
    for (MindmapNode& node : data_.nodes) {
      node.shape = shapeFor(node);
      node.look = data_.config.look;
      QStringList classes{QStringLiteral("mindmap-node")};
      if (node.isRoot)
        classes << QStringLiteral("section-root") << QStringLiteral("section--1");
      else if (node.hasSection)
        classes << QStringLiteral("section-%1").arg(node.section);
      if (!node.cssClass.isEmpty()) classes << node.cssClass;
      node.cssClasses = classes.join(QLatin1Char(' '));
    }
    generateEdges(data_.rootId);
  }

  QVector<Token> tokens_;
  int index_ = 0;
  int baseLevel_ = 0;
  bool lastWasClass_ = false;
  MindmapData data_;
};

}  // namespace

MindmapData MindmapDiagram::parse(const QString& source,
                                  const MindmapParseConfig& config) {
  return Parser(source, config).parse();
}

}  // namespace muffin::mermaid::mindmap
