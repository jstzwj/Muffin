#include "mermaid/block/BlockDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace muffin::mermaid::block {

BlockParseError::BlockParseError(BlockErrorKind errorKind, int errorLine,
                                 int errorColumn, QString errorToken,
                                 const QString& message)
    : std::runtime_error(message.toUtf8().constData()),
      kind(errorKind),
      line(errorLine),
      column(errorColumn),
      token(std::move(errorToken)) {}

namespace {

struct Statement {
  enum class Kind { Node, Edge, Columns, ClassDef, ApplyClass, ApplyStyles };
  Kind kind = Kind::Node;
  BlockNode node;
  BlockEdge edge;
  int columns = -1;
  QString id;
  QString value;
  QVector<Statement> children;
};

class Cursor {
public:
  explicit Cursor(QString source) : source_(std::move(source)) {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source_.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  bool eof() const { return index_ >= source_.size(); }
  QChar peek(qsizetype ahead = 0) const {
    return index_ + ahead < source_.size() ? source_.at(index_ + ahead) : QChar();
  }
  qsizetype index() const { return index_; }
  int line() const { return line_; }
  int column() const { return column_; }

  void set(qsizetype value) {
    index_ = 0;
    line_ = 1;
    column_ = 1;
    while (index_ < value) advance();
  }

  void advance() {
    if (eof()) return;
    if (source_.at(index_++) == QLatin1Char('\n')) {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
  }

  void skipSpace() {
    while (!eof() && peek().isSpace()) advance();
  }

  bool starts(QStringView value, Qt::CaseSensitivity cs = Qt::CaseSensitive) const {
    return QStringView(source_).mid(index_, value.size()).compare(value, cs) == 0;
  }

  bool keyword(QStringView value) const {
    if (!starts(value)) return false;
    const qsizetype end = index_ + value.size();
    return end >= source_.size() || !source_.at(end).isLetterOrNumber() &&
           source_.at(end) != QLatin1Char('_');
  }

  void consume(QStringView value) {
    for (qsizetype i = 0; i < value.size(); ++i) advance();
  }

  QString lineRemainder() {
    const qsizetype start = index_;
    while (!eof() && peek() != QLatin1Char('\n')) advance();
    return source_.mid(start, index_ - start);
  }

  QString remaining() const { return source_.mid(index_); }

  QString id() {
    const qsizetype start = index_;
    while (!eof()) {
      const QChar ch = peek();
      if (ch == QLatin1Char('(') || ch == QLatin1Char('[') ||
          ch == QLatin1Char('\n') || ch == QLatin1Char('-') ||
          ch == QLatin1Char(')') || ch == QLatin1Char('{') ||
          ch == QLatin1Char('}') || ch.isSpace() || ch == QLatin1Char('<') ||
          ch == QLatin1Char('>') || ch == QLatin1Char(':') || ch == QLatin1Char('='))
        break;
      advance();
    }
    return source_.mid(start, index_ - start);
  }

  QString quoted(bool* markdown = nullptr) {
    if (markdown) *markdown = false;
    if (peek() != QLatin1Char('"')) fail(BlockErrorKind::Parser, QStringLiteral("Expected quoted label"));
    advance();
    const bool md = peek() == QLatin1Char('`');
    if (md) advance();
    QString value;
    while (!eof()) {
      if (md && peek() == QLatin1Char('`') && peek(1) == QLatin1Char('"')) {
        advance(); advance();
        if (markdown) *markdown = true;
        return value;
      }
      if (!md && peek() == QLatin1Char('"')) {
        advance();
        return value;
      }
      value += peek();
      advance();
    }
    fail(BlockErrorKind::Parser, QStringLiteral("Unterminated block label"));
  }

  [[noreturn]] void fail(BlockErrorKind kind, const QString& message,
                         QString token = {}) const {
    if (token.isNull()) token = eof() ? QString() : QString(peek());
    throw BlockParseError(kind, line_, column_, token, message);
  }

private:
  QString source_;
  qsizetype index_ = 0;
  int line_ = 1;
  int column_ = 1;
};

QString shapeType(QStringView opener, QStringView closer) {
  const QString pair = opener.toString() + closer.toString();
  if (pair == QLatin1String("[]")) return QStringLiteral("square");
  if (pair == QLatin1String("()")) return QStringLiteral("round");
  if (pair == QLatin1String("(())")) return QStringLiteral("circle");
  if (pair == QLatin1String("((()))")) return QStringLiteral("doublecircle");
  if (pair == QLatin1String("{}")) return QStringLiteral("diamond");
  if (pair == QLatin1String("{{}}")) return QStringLiteral("hexagon");
  if (pair == QLatin1String("([])")) return QStringLiteral("stadium");
  if (pair == QLatin1String("[[]]")) return QStringLiteral("subroutine");
  if (pair == QLatin1String("[()]")) return QStringLiteral("cylinder");
  if (pair == QLatin1String("[//]")) return QStringLiteral("lean_right");
  if (pair == QLatin1String("[\\\\]")) return QStringLiteral("lean_left");
  if (pair == QLatin1String("[/\\]")) return QStringLiteral("trapezoid");
  if (pair == QLatin1String("[\\/]")) return QStringLiteral("inv_trapezoid");
  if (pair == QLatin1String(">]")) return QStringLiteral("rect_left_inv_arrow");
  if (pair == QLatin1String("<[]>")) return QStringLiteral("block_arrow");
  return QStringLiteral("na");
}

QStringList splitDeclarations(QString value) {
  QStringList result;
  for (QString item : value.split(QLatin1Char(','))) {
    item = item.trimmed();
    if (!item.isEmpty()) result.append(item);
  }
  return result;
}

class Parser {
public:
  explicit Parser(QString source) : cursor_(std::move(source)) {}

  BlockData parse() {
    cursor_.skipSpace();
    if (cursor_.keyword(u"block-beta")) cursor_.consume(u"block-beta");
    else if (cursor_.keyword(u"block")) cursor_.consume(u"block");
    else cursor_.fail(BlockErrorKind::Lexer, QStringLiteral("Expected block diagram header"));
    QVector<Statement> statements = document(false);
    if (statements.isEmpty())
      cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Block diagram requires a statement"));
    if (!cursor_.eof())
      cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Unexpected block token"));
    return populate(std::move(statements));
  }

private:
  QVector<Statement> document(bool nested) {
    QVector<Statement> result;
    while (true) {
      cursor_.skipSpace();
      if (cursor_.eof()) {
        if (nested) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Expected block end"));
        break;
      }
      if (cursor_.keyword(u"end")) {
        if (!nested) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Unexpected block end"));
        cursor_.consume(u"end");
        cursor_.skipSpace();
        break;
      }
      if (cursor_.starts(u"accTitle") || cursor_.starts(u"accDescr"))
        cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Accessibility metadata is not in the block grammar"));
      result += statement();
    }
    return result;
  }

  QVector<Statement> statement() {
    if (cursor_.keyword(u"columns")) return {columns()};
    if (cursor_.starts(u"block:")) return {composite(true)};
    if (cursor_.keyword(u"block")) return {composite(false)};
    if (cursor_.keyword(u"space")) return {space()};
    if (cursor_.keyword(u"classDef")) return {classDef()};
    if (cursor_.keyword(u"class")) return {applyClass()};
    if (cursor_.keyword(u"style")) return {applyStyles()};

    BlockNode first = node();
    cursor_.skipSpace();
    const auto edge = edgeToken();
    if (!edge) {
      Statement value;
      value.kind = Statement::Kind::Node;
      value.node = std::move(first);
      return {value};
    }
    cursor_.skipSpace();
    BlockNode second = node();
    first.hasWidthInColumns = false;
    second.hasWidthInColumns = false;
    Statement left;
    left.kind = Statement::Kind::Node;
    left.node = first;
    Statement middle;
    middle.kind = Statement::Kind::Edge;
    middle.edge = *edge;
    middle.edge.start = first.id;
    middle.edge.end = second.id;
    middle.edge.id = first.id + QLatin1Char('-') + second.id;
    Statement right;
    right.kind = Statement::Kind::Node;
    right.node = std::move(second);
    return {left, middle, right};
  }

  Statement columns() {
    Statement value;
    value.kind = Statement::Kind::Columns;
    cursor_.consume(u"columns");
    cursor_.skipSpace();
    if (cursor_.keyword(u"auto")) {
      cursor_.consume(u"auto");
      value.columns = -1;
      return value;
    }
    QString number;
    while (cursor_.peek().isDigit()) { number += cursor_.peek(); cursor_.advance(); }
    if (number.isEmpty()) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Expected block column count"));
    value.columns = number.toInt();
    return value;
  }

  Statement space() {
    Statement value;
    value.kind = Statement::Kind::Node;
    cursor_.consume(u"space");
    value.node.id = generatedId();
    value.node.type = QStringLiteral("space");
    value.node.hasLabel = true;
    value.node.hasWidth = true;
    value.node.width = 1;
    value.node.children.clear();
    if (cursor_.peek() == QLatin1Char(':')) {
      cursor_.advance();
      QString number;
      while (cursor_.peek().isDigit()) { number += cursor_.peek(); cursor_.advance(); }
      if (number.isEmpty()) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Expected space width"));
      value.node.width = number.toInt();
    }
    return value;
  }

  Statement composite(bool named) {
    Statement value;
    value.kind = Statement::Kind::Node;
    if (named) {
      cursor_.consume(u"block:");
      value.node = node();
    } else {
      cursor_.consume(u"block");
      value.node.id = generatedId();
      value.node.hasLabel = true;
    }
    value.node.type = QStringLiteral("composite");
    value.node.children.clear();
    value.children = document(true);
    return value;
  }

  Statement classDef() {
    const int startLine = cursor_.line();
    cursor_.consume(u"classDef"); cursor_.skipSpace();
    QString line = cursor_.lineRemainder().trimmed();
    const qsizetype split = line.indexOf(QRegularExpression(QStringLiteral(R"(\s)")));
    if (split <= 0) throw BlockParseError(BlockErrorKind::Parser, startLine, 1, {}, "Invalid classDef");
    Statement value; value.kind = Statement::Kind::ClassDef;
    value.id = line.left(split).trimmed(); value.value = line.mid(split).trimmed();
    return value;
  }

  Statement applyClass() {
    cursor_.consume(u"class"); cursor_.skipSpace();
    QString line = cursor_.lineRemainder().trimmed();
    const qsizetype split = line.indexOf(QRegularExpression(QStringLiteral(R"(\s)")));
    if (split <= 0) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Invalid class statement"));
    Statement value; value.kind = Statement::Kind::ApplyClass;
    value.id = line.left(split).trimmed(); value.value = line.mid(split).trimmed();
    return value;
  }

  Statement applyStyles() {
    cursor_.consume(u"style"); cursor_.skipSpace();
    QString line = cursor_.lineRemainder().trimmed();
    const qsizetype split = line.indexOf(QRegularExpression(QStringLiteral(R"(\s)")));
    if (split <= 0) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Invalid style statement"));
    Statement value; value.kind = Statement::Kind::ApplyStyles;
    value.id = line.left(split).trimmed(); value.value = line.mid(split).trimmed();
    return value;
  }

  BlockNode node() {
    BlockNode result;
    result.id = cursor_.id();
    if (result.id.isEmpty()) cursor_.fail(BlockErrorKind::Lexer, QStringLiteral("Expected block node id"));
    result.type = QStringLiteral("na");
    result.hasWidthInColumns = true;

    QString opener;
    QString closer;
    if (cursor_.starts(u"(((")) { opener = "((("; closer = ")))"; }
    else if (cursor_.starts(u"((")) { opener = "(("; closer = "))"; }
    else if (cursor_.starts(u"{{")) { opener = "{{"; closer = "}}"; }
    else if (cursor_.starts(u"([")) { opener = "(["; closer = "])"; }
    else if (cursor_.starts(u"[[")) { opener = "[["; closer = "]]"; }
    else if (cursor_.starts(u"[(")) { opener = "[("; closer = ")]"; }
    else if (cursor_.starts(u"[/")) opener = "[/";
    else if (cursor_.starts(u"[\\")) opener = "[\\";
    else if (cursor_.starts(u"<[")) { opener = "<["; closer = "]>"; }
    else if (cursor_.starts(u">")) { opener = ">"; closer = "]"; }
    else if (cursor_.starts(u"[")) { opener = "["; closer = "]"; }
    else if (cursor_.starts(u"(")) { opener = "("; closer = ")"; }
    else if (cursor_.starts(u"{")) { opener = "{"; closer = "}"; }

    if (!opener.isEmpty()) {
      cursor_.consume(opener);
      if (cursor_.peek() != QLatin1Char('"'))
        cursor_.fail(BlockErrorKind::Lexer, QStringLiteral("Block labels must be quoted"));
      bool markdown = false;
      result.label = cursor_.quoted(&markdown);
      if (markdown) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Markdown labels are rejected by block grammar"));
      if (opener == QLatin1String("[/")) {
        if (cursor_.starts(u"/]")) closer = QStringLiteral("/]");
        else if (cursor_.starts(u"\\]")) closer = QStringLiteral("\\]");
      } else if (opener == QLatin1String("[\\")) {
        if (cursor_.starts(u"\\]")) closer = QStringLiteral("\\]");
        else if (cursor_.starts(u"/]")) closer = QStringLiteral("/]");
      }
      if (closer.isEmpty() || !cursor_.starts(closer))
        cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Invalid block shape terminator"));
      cursor_.consume(closer);
      result.type = shapeType(opener, closer);
      result.hasLabel = true;
      if (result.type == QLatin1String("block_arrow")) {
        if (cursor_.peek() != QLatin1Char('(')) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Expected block arrow directions"));
        cursor_.advance();
        while (!cursor_.eof() && cursor_.peek() != QLatin1Char(')')) {
          cursor_.skipSpace();
          if (cursor_.peek() == QLatin1Char(',')) { cursor_.advance(); continue; }
          QString dir;
          while (cursor_.peek().isLetter()) { dir += cursor_.peek(); cursor_.advance(); }
          if (dir.isEmpty()) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Invalid block arrow direction"));
          result.directions.append(dir);
        }
        if (cursor_.peek() != QLatin1Char(')')) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Unterminated block arrow directions"));
        cursor_.advance();
      }
    }
    if (cursor_.peek() == QLatin1Char(':')) {
      cursor_.advance();
      QString width;
      while (cursor_.peek().isDigit()) { width += cursor_.peek(); cursor_.advance(); }
      if (width.isEmpty()) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Expected block width"));
      result.widthInColumns = width.toInt();
    }
    return result;
  }

  std::optional<BlockEdge> edgeToken() {
    const qsizetype saved = cursor_.index();
    cursor_.skipSpace();
    const qsizetype tokenStart = cursor_.index();
    QString raw;
    while (!cursor_.eof()) {
      const QChar ch = cursor_.peek();
      if (ch == QLatin1Char('-') || ch == QLatin1Char('=') || ch == QLatin1Char('.') ||
          ch == QLatin1Char('x') || ch == QLatin1Char('o') || ch == QLatin1Char('<') ||
          ch == QLatin1Char('>') || ch.isSpace()) {
        raw += ch; cursor_.advance();
      } else break;
      if (raw.contains(QLatin1Char('-')) || raw.contains(QLatin1Char('='))) {
        const qsizetype labelSave = cursor_.index();
        cursor_.skipSpace();
        if (cursor_.peek() == QLatin1Char('"')) {
          bool markdown = false;
          QString label = cursor_.quoted(&markdown);
          if (markdown) cursor_.fail(BlockErrorKind::Parser, QStringLiteral("Markdown edge labels are rejected"));
          cursor_.skipSpace();
          QString tail;
          while (!cursor_.eof() && QStringLiteral("-=.xo<>").contains(cursor_.peek())) {
            tail += cursor_.peek(); cursor_.advance();
          }
          if (!tail.isEmpty()) {
            raw += tail;
            BlockEdge edge = edgeFromRaw(raw);
            edge.label = label;
            return edge;
          }
          cursor_.set(labelSave);
        }
      }
      if (!cursor_.eof() && !cursor_.peek().isSpace() &&
          !QStringLiteral("-=.xo<>").contains(cursor_.peek())) break;
    }
    raw = raw.trimmed();
    if (!raw.contains(QLatin1Char('-')) && !raw.contains(QLatin1Char('='))) {
      cursor_.set(saved); return std::nullopt;
    }
    static const QRegularExpression valid(QStringLiteral(R"(^[xo<]?(?:--+[-xo>]?|==+[=xo>]?|-?\.+-[xo>]?)$)"));
    if (!valid.match(raw).hasMatch()) { cursor_.set(saved); return std::nullopt; }
    (void)tokenStart;
    return edgeFromRaw(raw);
  }

  static BlockEdge edgeFromRaw(QString raw) {
    raw = raw.trimmed();
    BlockEdge edge;
    edge.thickness = raw.contains(QStringLiteral("==")) ? QStringLiteral("thick") : QStringLiteral("normal");
    edge.pattern = raw.contains(QStringLiteral(".-")) ? QStringLiteral("dotted") : QStringLiteral("solid");
    const QChar first = raw.isEmpty() ? QChar() : raw.front();
    const QChar last = raw.isEmpty() ? QChar() : raw.back();
    if (first == QLatin1Char('x')) edge.arrowTypeStart = QStringLiteral("arrow_cross");
    else if (first == QLatin1Char('o')) edge.arrowTypeStart = QStringLiteral("arrow_circle");
    else if (first == QLatin1Char('<')) edge.arrowTypeStart = QStringLiteral("arrow_point");
    if (last == QLatin1Char('x')) edge.arrowTypeEnd = QStringLiteral("arrow_cross");
    else if (last == QLatin1Char('o')) edge.arrowTypeEnd = QStringLiteral("arrow_circle");
    else if (last == QLatin1Char('>')) edge.arrowTypeEnd = QStringLiteral("arrow_point");
    return edge;
  }

  QString generatedId() { return QStringLiteral("generated-%1").arg(++generated_); }

  BlockData populate(QVector<Statement> statements) {
    BlockData data;
    data.root.id = QStringLiteral("root");
    data.root.type = QStringLiteral("composite");
    data.root.hasColumns = true;
    data.root.columns = -1;
    QVector<QString> order{QStringLiteral("root")};
    QHash<QString, BlockNode> database;
    database.insert(data.root.id, data.root);
    QHash<QString, int> edgeCounts;

    auto addClass = [&](const QString& id, const QString& declarations) {
      auto it = std::find_if(data.classes.begin(), data.classes.end(), [&](const BlockClass& c) { return c.id == id; });
      if (it == data.classes.end()) { data.classes.append(BlockClass{id}); it = std::prev(data.classes.end()); }
      for (QString declaration : declarations.split(QLatin1Char(','))) {
        declaration.remove(QRegularExpression(QStringLiteral(R"(;$)")));
        declaration = declaration.trimmed();
        if (declaration.isEmpty()) continue;
        it->styles.append(declaration);
        if (declaration.contains(QStringLiteral("color"))) {
          QString text = declaration;
          text.replace(QStringLiteral("fill"), QStringLiteral("bgFill"));
          text.replace(QStringLiteral("color"), QStringLiteral("fill"));
          it->textStyles.append(text);
        }
      }
    };

    std::function<QVector<BlockNode>(QVector<Statement>&, BlockNode&)> visit;
    visit = [&](QVector<Statement>& list, BlockNode& parent) {
      QVector<BlockNode> children;
      int detectedColumns = -1;
      for (const Statement& s : list) if (s.kind == Statement::Kind::Columns) { detectedColumns = s.columns; break; }
      (void)detectedColumns;
      for (Statement& s : list) {
        if (s.kind == Statement::Kind::ClassDef) { addClass(s.id, s.value); continue; }
        if (s.kind == Statement::Kind::ApplyClass) {
          for (QString id : s.id.split(QLatin1Char(','))) {
            id = id.trimmed();
            if (!database.contains(id)) { BlockNode p; p.id=id; p.type=QStringLiteral("na"); database.insert(id,p); order.append(id); }
            database[id].classes.append(s.value);
          }
          continue;
        }
        if (s.kind == Statement::Kind::ApplyStyles) {
          if (!database.contains(s.id)) throw BlockParseError(BlockErrorKind::Parser, 1, 1, s.id, "Unknown block style target");
          database[s.id].styles = s.value.split(QLatin1Char(','));
          continue;
        }
        if (s.kind == Statement::Kind::Columns) { parent.columns=s.columns; parent.hasColumns=true; continue; }
        if (s.kind == Statement::Kind::Edge) {
          BlockEdge edge=s.edge; const int count=++edgeCounts[edge.id]; edge.id=QString::number(count)+QLatin1Char('-')+edge.id; data.edges.append(edge); continue;
        }
        BlockNode node=s.node;
        if (node.hasLabel) node.label=HtmlSanitizer().sanitizedMermaidText(node.label);
        if (!node.hasLabel || node.label.isEmpty()) {
          node.label = node.type == QLatin1String("composite") ? QString() : node.id;
          node.hasLabel = true;
        }
        const bool existed=database.contains(node.id);
        if (!existed) { database.insert(node.id,node); order.append(node.id); }
        else {
          if (node.type != QLatin1String("na")) database[node.id].type=node.type;
          if (node.label != node.id) { database[node.id].label=node.label; database[node.id].hasLabel=true; }
        }
        if (!s.children.isEmpty()) {
          BlockNode nested=node;
          nested.children=visit(s.children,nested);
          database[node.id].children=nested.children;
          database[node.id].columns=nested.columns;
          database[node.id].hasColumns=nested.hasColumns;
          node=database[node.id];
        }
        if (node.type == QLatin1String("space")) {
          const int width=node.hasWidth?node.width:1;
          for(int i=0;i<width;++i){BlockNode clone=node;clone.id+=QLatin1Char('-')+QString::number(i);database.insert(clone.id,clone);order.append(clone.id);children.append(clone);}
        } else if (!existed) children.append(database[node.id]);
      }
      parent.children=children;
      return children;
    };

    data.root.children=visit(statements,data.root);
    database[data.root.id]=data.root;
    std::function<void(BlockNode&)> sync = [&](BlockNode& node) {
      if (database.contains(node.id)) {
        const QVector<BlockNode> children = node.children;
        node = database.value(node.id);
        if (!children.isEmpty()) node.children = children;
      }
      for (BlockNode& child : node.children) sync(child);
    };
    sync(data.root);
    database[data.root.id] = data.root;
    data.blocks=data.root.children;
    for(const QString& id:order) data.flat.append(database.value(id));
    return data;
  }

  Cursor cursor_;
  int generated_ = 0;
};

}  // namespace

BlockData BlockDiagram::parse(const QString& source) {
  return Parser(source).parse();
}

}  // namespace muffin::mermaid::block
