#include "mermaid/erdiagram/ErTokenizer.h"
#include "mermaid/MermaidPreprocessor.h"

#include <optional>

// Mermaid's erDiagram parser, production for production.
//
// The grammar is src/diagrams/er/parser/erDiagram.jison as built in mermaid
// 11.16.0, read here as the statements a recursive-descent parser expects.
// Where Mermaid's LALR parser has a choice, the lookahead decides the same way
// for both:
//
//   start      : ER_DIAGRAM document EOF
//   document   : line*
//   line       : statement | NEWLINE
//   statement  : acc_title acc_title_value
//              | acc_descr acc_descr_value | acc_descr_multiline_value
//              | direction
//              | CLASSDEF idList stylesOpt separator      (lexed in the style condition)
//              | CLASS idList idList
//              | STYLE idList stylesOpt separator         (lexed in the style condition)
//              | entityStatement
//   entityStatement
//              : entityName [ SQS entityName SQE ] [ STYLE_SEPARATOR idList ]
//                ( BLOCK_START attributes BLOCK_STOP
//                | BLOCK_START BLOCK_STOP
//                | relSpec entityName [ STYLE_SEPARATOR idList ] COLON role
//                | <nothing> )
//   relSpec    : cardinality relType cardinality
//   attribute  : ATTRIBUTE_WORD [ "?" ] ATTRIBUTE_WORD [ keys ] [ COMMENT ]
//   keys       : ATTRIBUTE_KEY ( "," ATTRIBUTE_KEY )*
//
// The database is Mermaid's ErDB. An error names the position of the last
// token the parser took: the line of Mermaid's message and the column of its
// error object.

namespace {

using namespace muffin::mermaid;
using namespace muffin::mermaid::er;
using Kinds = std::initializer_list<ErTokenKind>;

// What the grammar allows at a point: the "Expecting" list of Mermaid's
// message there, derived from the token kinds.
QStringList names(std::initializer_list<ErTokenKind> kinds) {
  QStringList result;
  for (const ErTokenKind kind : kinds)
    result << erTokenName(kind);
  return result;
}

constexpr std::initializer_list<ErTokenKind> kStatementStart = {
    ErTokenKind::Newline,    ErTokenKind::AccTitle, ErTokenKind::AccDescr,   ErTokenKind::AccDescrBlock,
    ErTokenKind::Direction,  ErTokenKind::ClassDef, ErTokenKind::Text,       ErTokenKind::Class,
    ErTokenKind::Style,      ErTokenKind::Number,   ErTokenKind::EntityName, ErTokenKind::Decimal,
    ErTokenKind::EntityOne};
constexpr std::initializer_list<ErTokenKind> kEntityName = {ErTokenKind::Text, ErTokenKind::Number,
                                                            ErTokenKind::EntityName, ErTokenKind::Decimal,
                                                            ErTokenKind::EntityOne};
constexpr std::initializer_list<ErTokenKind> kRole = {ErTokenKind::Text, ErTokenKind::EntityName, ErTokenKind::Word};
constexpr std::initializer_list<ErTokenKind> kId = {ErTokenKind::Text, ErTokenKind::StyleText};
constexpr std::initializer_list<ErTokenKind> kStyleComponent = {ErTokenKind::StyleText, ErTokenKind::Number,
                                                                ErTokenKind::Colon, ErTokenKind::Hash};

class Parser {
  // `text_` is what the lexer reads and `offsets_` maps each of its characters
  // to the caller's text; see toSource().
  QString text_;
  QVector<qsizetype> offsets_;
  ErLimits limits_;
  ErTokenizer lexer_;
  ErToken lookahead_;
  std::optional<ErToken> lastShifted_; // the token the last shift() took
  ErDiagramData data_;

public:
  Parser(QString text, QVector<qsizetype> offsets, ErLimits limits)
      : text_(std::move(text)), offsets_(std::move(offsets)), limits_(limits), lexer_(text_) {
    lookahead_ = read();
  }

  ErDiagramData parse() {
    if (text_.size() > limits_.maxTextSize)
      fail(ErErrorStage::Resource, ErErrorCode::LimitExceeded, QStringLiteral("start"), {},
           QStringLiteral("Maximum er diagram text size exceeded"));
    if (!at(ErTokenKind::Header))
      fail(ErErrorStage::Detector, ErErrorCode::MissingHeader, QStringLiteral("erDiagram"), {QStringLiteral("ER_DIAGRAM")});
    shift();
    while (!at(ErTokenKind::Eof)) {
      if (at(ErTokenKind::Newline)) {
        shift();
        continue;
      }
      parseStatement();
    }
    return std::move(data_);
  }

private:
  // ---- the token stream
  bool at(ErTokenKind kind) const { return lookahead_.kind == kind; }
  bool anyOf(Kinds kinds) const {
    for (const ErTokenKind kind : kinds)
      if (at(kind))
        return true;
    return false;
  }

  // Reads the next token, bringing a lexical error's position back to the
  // caller's text.
  ErToken read() {
    try {
      return lexer_.next();
    } catch (const ErParseError& error) {
      ErDiagnostic diagnostic = error.diagnostic();
      diagnostic.span = toSource(diagnostic.span);
      throw ErParseError(std::move(diagnostic));
    }
  }

  // Takes the lookahead. Mermaid reads the next token right after a shift, so
  // a character its lexer cannot read fails here too.
  const ErToken& shift() {
    lastShifted_ = lookahead_;
    lookahead_ = read();
    return *lastShifted_;
  }

  const ErToken& expect(ErTokenKind kind, const QString& production, Kinds expected) {
    if (!at(kind))
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, production, names(expected));
    return shift();
  }

  // ---- diagnostics
  // Positions are in the text the lexer read; `offsets_` maps each of its
  // characters back to the caller's.
  ErSourceSpan toSource(ErSourceSpan span) const {
    const qsizetype first = qBound<qsizetype>(0, span.offset, offsets_.size() - 1);
    const qsizetype last = qBound<qsizetype>(0, span.offset + span.length, offsets_.size() - 1);
    return {offsets_.at(first), qMax<qsizetype>(0, offsets_.at(last) - offsets_.at(first)), span.line, span.column};
  }

  // What Mermaid's message calls a token: a terminal name, the direction, or
  // the character itself when no rule took it.
  static QString nameOf(const ErToken& token) {
    if (token.kind == ErTokenKind::Character)
      return token.text;
    if (token.kind == ErTokenKind::Direction)
      return QStringLiteral("direction_") + token.text.toLower();
    return erTokenName(token.kind);
  }

  [[noreturn]] void fail(ErErrorStage stage, ErErrorCode code, QString production, QStringList expected,
                         QString detail = {}) const {
    // Mermaid's message names the line its lexer is on after the last token
    // the parser took: a newline just taken counts for the line it starts.
    ErSourceSpan span{0, 0, 1, 0}; // nothing taken yet: the start of the text
    if (lastShifted_)
      span = toSource({lastShifted_->offset, lastShifted_->length, lastShifted_->endLine, lastShifted_->column});
    throw ErParseError({stage, code, span, std::move(production), nameOf(lookahead_), std::move(detail),
                        std::move(expected)});
  }

  // ---- the database: Mermaid's ErDB
  ErEntity* findEntity(const QString& name) {
    for (ErEntity& entity : data_.entities)
      if (entity.name == name)
        return &entity;
    return nullptr;
  }

  void addEntity(const QString& name, const QString& alias = {}) {
    if (ErEntity* existing = findEntity(name)) {
      if (existing->alias.isEmpty() && !alias.isEmpty())
        existing->alias = alias;
      return;
    }
    if (data_.entities.size() >= limits_.maxEntities)
      fail(ErErrorStage::Resource, ErErrorCode::LimitExceeded, QStringLiteral("entityName"), {},
           QStringLiteral("Maximum entity count exceeded"));
    ErEntity entity;
    entity.id = QStringLiteral("entity-%1-%2").arg(name).arg(data_.entities.size());
    entity.name = name;
    entity.alias = alias;
    data_.entities.append(std::move(entity));
  }

  void addAttributes(const QString& name, const QVector<ErAttribute>& attributes) {
    addEntity(name);
    ErEntity& entity = *findEntity(name);
    if (entity.attributes.size() + attributes.size() > limits_.maxAttributesPerEntity)
      fail(ErErrorStage::Resource, ErErrorCode::LimitExceeded, QStringLiteral("attribute"), {},
           QStringLiteral("Maximum attributes per entity exceeded"));
    entity.attributes += attributes;
  }

  void addRelationship(const QString& entityA, const QString& role, ErCardinality atB, ErCardinality atA,
                       bool identifying, const QString& entityB) {
    if (data_.relationships.size() >= limits_.maxRelationships)
      fail(ErErrorStage::Resource, ErErrorCode::LimitExceeded, QStringLiteral("relationshipStatement"), {},
           QStringLiteral("Maximum relationship count exceeded"));
    // Mermaid records both ends by their ids, and drops the relationship
    // when either entity is missing.
    const ErEntity* a = findEntity(entityA);
    const ErEntity* b = findEntity(entityB);
    if (!a || !b)
      return;
    ErRelationship relationship;
    relationship.entityA = a->id;
    relationship.entityB = b->id;
    relationship.cardA = atA;
    relationship.cardB = atB;
    relationship.identifying = identifying;
    relationship.label = role;
    data_.relationships.append(std::move(relationship));
  }

  void setClass(const QStringList& ids, const QStringList& classNames) {
    for (const QString& id : ids) {
      ErEntity* entity = findEntity(id);
      if (!entity)
        continue;
      for (const QString& className : classNames) {
        if (!entity->cssClasses.isEmpty())
          entity->cssClasses += QLatin1Char(' ');
        entity->cssClasses += className;
      }
    }
  }

  // Stops at the first id that names no entity, as Mermaid does.
  void addCssStyles(const QStringList& ids, const QStringList& styles) {
    for (const QString& id : ids) {
      ErEntity* entity = findEntity(id);
      if (!entity)
        return;
      entity->styles += styles;
    }
  }

  void defineClass(const QStringList& ids, const QStringList& styles) {
    for (const QString& id : ids)
      data_.classDefs[id] += styles;
  }

  // ---- productions
  void parseStatement() {
    switch (lookahead_.kind) {
    case ErTokenKind::AccTitle:
      shift();
      data_.accTitle = expect(ErTokenKind::AccTitleValue, QStringLiteral("acc_title"), {ErTokenKind::AccTitleValue})
                           .text.trimmed();
      return;
    case ErTokenKind::AccDescr:
      shift();
      data_.accDescription =
          expect(ErTokenKind::AccDescrValue, QStringLiteral("acc_descr"), {ErTokenKind::AccDescrValue}).text.trimmed();
      return;
    case ErTokenKind::AccDescrBlock:
      data_.accDescription = shift().text.trimmed();
      return;
    case ErTokenKind::Direction:
      data_.direction = shift().text;
      return;
    case ErTokenKind::ClassDef:
      parseClassDef();
      return;
    case ErTokenKind::Class:
      parseClassStatement();
      return;
    case ErTokenKind::Style:
      parseStyleStatement();
      return;
    default:
      if (anyOf(kEntityName))
        parseEntityStatement();
      else
        fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("statement"), names(kStatementStart));
    }
  }

  QString parseEntityName() {
    if (!anyOf(kEntityName))
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("entityName"), names(kEntityName));
    return shift().text;
  }

  void parseEntityStatement() {
    const QString name = parseEntityName();
    QString alias;
    if (at(ErTokenKind::BracketOpen)) {
      shift();
      alias = parseEntityName();
      expect(ErTokenKind::BracketClose, QStringLiteral("entityName"), {ErTokenKind::BracketClose});
    }
    QStringList classes;
    if (at(ErTokenKind::StyleSeparator)) {
      shift();
      classes = parseIdList();
    }

    if (at(ErTokenKind::BlockStart)) {
      shift();
      QVector<ErAttribute> attributes;
      if (!at(ErTokenKind::BlockStop))
        attributes = parseAttributes();
      expect(ErTokenKind::BlockStop, QStringLiteral("attributes"), {ErTokenKind::BlockStop, ErTokenKind::AttributeWord});
      addEntity(name, alias);
      addAttributes(name, attributes);
      setClass({name}, classes);
    } else if (at(ErTokenKind::Cardinality) && alias.isEmpty()) {
      // With an alias the statement ends here and the cardinality is an error.
      parseRelationship(name, classes);
    } else {
      addEntity(name, alias);
      setClass({name}, classes);
    }
  }

  // relSpec is { cardA: the cardinality at entityB, relType, cardB: the one at
  // entityA }: Mermaid names both ends from entityB's point of view.
  void parseRelationship(const QString& entityA, const QStringList& classesA) {
    const ErCardinality atA = parseCardinality();
    const bool identifying = parseRelType();
    const ErCardinality atB = parseCardinality();
    const QString entityB = parseEntityName();
    QStringList classesB;
    if (at(ErTokenKind::StyleSeparator)) {
      shift();
      classesB = parseIdList();
    }
    expect(ErTokenKind::Colon, QStringLiteral("relationship"), {ErTokenKind::Colon, ErTokenKind::StyleSeparator});
    if (!anyOf(kRole))
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("role"), names(kRole));
    const QString role = shift().text;
    addEntity(entityA);
    addEntity(entityB);
    addRelationship(entityA, role, atB, atA, identifying, entityB);
    setClass({entityA}, classesA);
    setClass({entityB}, classesB);
  }

  ErCardinality parseCardinality() {
    if (!at(ErTokenKind::Cardinality))
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("cardinality"),
           names({ErTokenKind::Cardinality}));
    const QString name = shift().text;
    if (name == QLatin1String("ZERO_OR_ONE"))
      return ErCardinality::ZeroOrOne;
    if (name == QLatin1String("ZERO_OR_MORE"))
      return ErCardinality::ZeroOrMore;
    if (name == QLatin1String("ONE_OR_MORE"))
      return ErCardinality::OneOrMore;
    if (name == QLatin1String("MD_PARENT"))
      return ErCardinality::MdParent;
    return ErCardinality::ExactlyOne;
  }

  bool parseRelType() {
    if (at(ErTokenKind::Identifying)) {
      shift();
      return true;
    }
    if (at(ErTokenKind::NonIdentifying)) {
      shift();
      return false;
    }
    fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("relType"),
         names({ErTokenKind::NonIdentifying, ErTokenKind::Identifying}));
  }

  // idList: id ( COMMA id )*. The ids are bare words in the document and
  // STYLE_TEXT words inside a style or classDef statement.
  QStringList parseIdList() {
    QStringList ids;
    const auto take = [&] {
      if (!anyOf(kId))
        fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("idList"), names(kId));
      ids.append(shift().text);
    };
    take();
    while (at(ErTokenKind::Comma)) {
      shift();
      take();
    }
    return ids;
  }

  // classStatement: CLASS idList idList. Nothing separates the two lists: the
  // first takes every comma-joined word, the second starts with the next one.
  void parseClassStatement() {
    shift();
    const QStringList ids = parseIdList();
    if (!anyOf(kId))
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("classStatement"),
           names({ErTokenKind::Text, ErTokenKind::StyleText, ErTokenKind::Comma}));
    setClass(ids, parseIdList());
  }

  // classDefStatement: CLASSDEF idList stylesOpt separator
  void parseClassDef() {
    shift();
    const QStringList ids = parseIdList();
    defineClass(ids, parseStylesOpt());
    parseSeparator();
  }

  // styleStatement: STYLE idList stylesOpt separator
  void parseStyleStatement() {
    shift();
    const QStringList ids = parseIdList();
    const QStringList styles = parseStylesOpt();
    parseSeparator();
    addCssStyles(ids, styles);
  }

  // stylesOpt: style ( COMMA style)*
  QStringList parseStylesOpt() {
    QStringList styles;
    styles.append(parseStyle());
    while (at(ErTokenKind::Comma)) {
      shift();
      styles.append(parseStyle());
    }
    return styles;
  }

  // style: styleComponent+. A declaration is the pieces Mermaid lexes from it,
  // joined without spaces: fill, :, # and f96.
  QString parseStyle() {
    QString style;
    while (anyOf(kStyleComponent))
      style += shift().text;
    if (style.isEmpty())
      fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("style"), names(kStyleComponent));
    return style;
  }

  // separator: SEMI | NEWLINE | EOF. The newline also ends the style
  // condition, so what follows is read as the document again.
  void parseSeparator() {
    if (anyOf({ErTokenKind::Semi, ErTokenKind::Newline, ErTokenKind::Eof})) {
      if (!at(ErTokenKind::Eof))
        shift();
      return;
    }
    fail(ErErrorStage::Parser, ErErrorCode::UnexpectedToken, QStringLiteral("separator"),
         names({ErTokenKind::Semi, ErTokenKind::Newline, ErTokenKind::Eof}));
  }

  // attributes: attribute+. The block's space and newline runs are not tokens.
  QVector<ErAttribute> parseAttributes() {
    QVector<ErAttribute> attributes;
    do {
      attributes.append(parseAttribute());
    } while (at(ErTokenKind::AttributeWord));
    return attributes;
  }

  // attribute: attributeType attributeName [ keys ] [ COMMENT ], with
  // attributeType: ATTRIBUTE_WORD [ "?" ].
  ErAttribute parseAttribute() {
    ErAttribute attribute;
    attribute.attributeType = expect(ErTokenKind::AttributeWord, QStringLiteral("attributeType"),
                                     {ErTokenKind::BlockStop, ErTokenKind::AttributeWord}).text;
    if (at(ErTokenKind::Question)) {
      shift();
      attribute.attributeType += QLatin1Char('?');
    }
    attribute.attributeName = expect(ErTokenKind::AttributeWord, QStringLiteral("attributeName"),
                                     {ErTokenKind::AttributeWord, ErTokenKind::Question}).text;
    if (at(ErTokenKind::AttributeKey)) {
      attribute.keys.append(shift().text);
      while (at(ErTokenKind::Comma)) {
        shift();
        attribute.keys.append(expect(ErTokenKind::AttributeKey, QStringLiteral("attributeKeyTypeList"),
                                     {ErTokenKind::AttributeKey}).text);
      }
    }
    if (at(ErTokenKind::AttributeComment))
      attribute.comment = shift().text;
    return attribute;
  }
};

} // namespace

namespace muffin::mermaid::er {

ErParseError::ErParseError(ErDiagnostic diagnostic)
    : DiagramError(formatErDiagnostic(diagnostic).toUtf8().constData()), diagnostic_(std::move(diagnostic)) {}

DiagramErrorInfo ErParseError::info() const {
  const ErDiagnostic& value = diagnostic();
  DiagramErrorInfo info;
  info.stage = erErrorStageName(value.stage);
  info.code = erErrorCodeName(value.code);
  info.message = value.detail.isEmpty() ? QString::fromUtf8(what()) : value.detail;
  info.place = DiagramErrorInfo::Place::Offset;
  info.offset = value.span.offset;
  info.length = value.span.length > 0 ? value.span.length : qMax<qsizetype>(1, value.actual.size());
  info.line = value.span.line;
  info.column = value.span.column + 1;
  info.production = value.production;
  info.actual = value.actual;
  info.expected = value.expected;
  return info;
}

const ErDiagnostic& ErParseError::diagnostic() const noexcept { return diagnostic_; }

ErDiagram ErDiagram::parse(const QString& source, ErLimits limits) {
  // Mermaid hands a diagram's parser the preprocessed text with
  // encodeEntities applied and a newline appended (Diagram.fromText), so a
  // statement that needs an end of line also has one at the end of the text.
  QVector<qsizetype> offsets;
  QString text = encodeMermaidEntities(source, &offsets);
  text += QLatin1Char('\n');
  offsets.append(source.size());
  ErDiagram diagram;
  diagram.data_ = Parser(std::move(text), std::move(offsets), limits).parse();
  return diagram;
}

QString erErrorStageName(ErErrorStage stage) {
  switch (stage) {
  case ErErrorStage::Detector:
    return QStringLiteral("detector");
  case ErErrorStage::Lexer:
    return QStringLiteral("lexer");
  case ErErrorStage::Parser:
    return QStringLiteral("parser");
  case ErErrorStage::Resource:
    return QStringLiteral("resource");
  }
  return {};
}

QString erErrorCodeName(ErErrorCode code) {
  switch (code) {
  case ErErrorCode::MissingHeader:
    return QStringLiteral("missing-header");
  case ErErrorCode::UnexpectedToken:
    return QStringLiteral("unexpected-token");
  case ErErrorCode::LimitExceeded:
    return QStringLiteral("limit-exceeded");
  }
  return {};
}

QString formatErDiagnostic(const ErDiagnostic& diagnostic) {
  return QStringLiteral("%1 at %2:%3 (%4)")
      .arg(diagnostic.detail.isEmpty() ? erErrorCodeName(diagnostic.code) : diagnostic.detail)
      .arg(diagnostic.span.line)
      .arg(diagnostic.span.column)
      .arg(erErrorStageName(diagnostic.stage));
}

} // namespace muffin::mermaid::er
