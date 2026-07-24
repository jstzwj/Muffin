#include "mermaid/erdiagram/ErTokenizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

// erDiagram parser (recursive descent over an ErTokenCursor), mirroring
// src/mermaid/classdiagram/ClassDiagram.cpp. This file produces ErDiagramData;
// layout/scene/painting live in ErLayout.cpp / ErScene.cpp / ErScenePainter.cpp.
//
// Per the module convention (and the lupdate rule in CLAUDE.md, kept here even
// though this file has no tr()) method definitions are fully-qualified and the
// file-local helpers live in a file-scope anonymous namespace.
//
// Grammar (terminals are ErTokenKind; see IMPLEMENTATION_SPEC.md §3):
//
//   diagram        := ER_HEADER NEWLINE ( statement )* EOF
//   statement      := comment | blankLine | accTitle | accDescription
//                   | titleDirective | entityBlock | relationship | entityAlias
//   comment        := COMMENT                                  (skipped)
//   blankLine      := NEWLINE                                  (skipped)
//   accTitle       := "accTitle" COLON <rest of line>
//   accDescription := "accDescr" COLON <rest of line>
//                   | "accDescr" OPENBRACE <lines until CLOSEBRACE>
//   titleDirective := "title" <rest of line>
//   entityAlias    := IDENTIFIER QUOTED_TEXT                   (id, display name)
//   entityBlock    := IDENTIFIER (QUOTED_TEXT)? OPENBRACE
//                     ( attributeLine )* CLOSEBRACE
//   attributeLine  := attribute
//   attribute      := IDENTIFIER IDENTIFIER                              (type name)
//                   | IDENTIFIER IDENTIFIER keyMarker                    (+ key)
//                   | IDENTIFIER IDENTIFIER keyMarker QUOTED_TEXT        (+ comment)
//                   | IDENTIFIER IDENTIFIER QUOTED_TEXT                  (+ comment)
//   keyMarker      := IDENTIFIER-in-{PK,FK,UK} | KeyPK | KeyFK | KeyUK
//   relationship   := entityRef connector entityRef ( COLON label )?
//   entityRef      := (QUOTED_TEXT)? IDENTIFIER (QUOTED_TEXT)?   (role adj. to id)
//   connector      := CARDINALITY ( HYPHEN | DOT ) CARDINALITY
//                     # HYPHEN text length 2 => identifying ("--"),
//                     # DOT text length 2    => non-identifying ("..")
//   label          := <raw text of rest of line>                 (trailing
//                     QUOTED_TEXT peeled into ErRelationship.comment)
//
// Cardinality glyph -> ErCardinality (single table; side is implicit in which
// of the two CARDINALITY tokens flanks the connector — first is cardA/entityA,
// second is cardB/entityB):
//
//   "||" -> ExactlyOne      (both sides)
//   "|o" -> ZeroOrOne       (left  / entityA)
//   "o|" -> ZeroOrOne       (right / entityB)
//   "}|" -> OneOrMore       (left  / entityA)
//   "|{" -> OneOrMore       (right / entityB)
//   "}o" -> ZeroOrMore      (left  / entityA)
//   "o{" -> ZeroOrMore      (right / entityB)

namespace {
using namespace muffin::mermaid::er;

ErSourceSpan spanForOffset(const QString& source, qsizetype offset, qsizetype length = 0) {
  ErSourceSpan span{offset, length, 1, 0};
  for (qsizetype i = 0; i < std::min(offset, source.size()); ++i) {
    if (source[i] == QLatin1Char('\n')) {
      ++span.line;
      span.column = 0;
    } else {
      ++span.column;
    }
  }
  return span;
}

// NOTE: ErDiagnostic field order is `..., detail, expected` (the reverse of
// ClassDiagnostic), so the aggregate init swaps those two trailing fields.
[[noreturn]] void raise(const QString& source, qsizetype offset, ErErrorStage stage,
                        ErErrorCode code, QString production, QString actual,
                        QStringList expected, QString detail = {}) {
  throw ErParseError({stage, code, spanForOffset(source, offset), std::move(production),
                      std::move(actual), std::move(detail), std::move(expected)});
}

// Crow's-foot glyph text -> ErCardinality. The tokenizer only ever emits the
// eight valid CARDINALITY glyphs, so the fallback is unreachable in practice.
ErCardinality cardinalityFromGlyph(const QString& glyph) {
  if (glyph == QLatin1String("||")) return ErCardinality::ExactlyOne;
  if (glyph == QLatin1String("|o") || glyph == QLatin1String("o|"))
    return ErCardinality::ZeroOrOne;
  if (glyph == QLatin1String("}|") || glyph == QLatin1String("|{"))
    return ErCardinality::OneOrMore;
  if (glyph == QLatin1String("}o") || glyph == QLatin1String("o{"))
    return ErCardinality::ZeroOrMore;
  return ErCardinality::ExactlyOne;
}

ErAttributeKeyType keyTypeFromWord(const QString& text) {
  if (text == QLatin1String("PK")) return ErAttributeKeyType::PrimaryKey;
  if (text == QLatin1String("FK")) return ErAttributeKeyType::ForeignKey;
  if (text == QLatin1String("UK")) return ErAttributeKeyType::UniqueKey;
  return ErAttributeKeyType::None;
}

QString cardinalityName(ErCardinality card) {
  switch (card) {
    case ErCardinality::ExactlyOne: return QStringLiteral("ExactlyOne");
    case ErCardinality::ZeroOrOne: return QStringLiteral("ZeroOrOne");
    case ErCardinality::OneOrMore: return QStringLiteral("OneOrMore");
    case ErCardinality::ZeroOrMore: return QStringLiteral("ZeroOrMore");
  }
  return {};
}

QString attributeKeyTypeName(ErAttributeKeyType key) {
  switch (key) {
    case ErAttributeKeyType::None: return QStringLiteral("None");
    case ErAttributeKeyType::PrimaryKey: return QStringLiteral("PrimaryKey");
    case ErAttributeKeyType::ForeignKey: return QStringLiteral("ForeignKey");
    case ErAttributeKeyType::UniqueKey: return QStringLiteral("UniqueKey");
  }
  return {};
}

class Parser {
public:
  Parser(QString source, ErLimits limits) : source_(std::move(source)), limits_(limits) {
    // ErTokenizer emits SPACE tokens so raw() can rebuild source; the parser
    // skips them, so drop them once up front. All token offsets are preserved,
    // keeping raw()/span diagnostics exact. This makes the cursor behave like
    // ClassTokenCursor (whose tokenizer skips whitespace silently).
    QVector<ErToken> raw = ErTokenizer(source_).tokenize();
    tokens_.reserve(raw.size());
    for (const ErToken& token : raw)
      if (token.kind != ErTokenKind::Space) tokens_.append(token);
  }

  ErDiagramData parse() {
    if (source_.size() > limits_.maxTextSize)
      raise(source_, 0, ErErrorStage::Resource, ErErrorCode::LimitExceeded,
            QStringLiteral("start"), QStringLiteral("text"), {},
            QStringLiteral("Maximum er diagram text size exceeded"));
    for (const ErToken& token : tokens_)
      if (token.kind == ErTokenKind::Invalid)
        raise(source_, token.offset, ErErrorStage::Lexer, ErErrorCode::UnexpectedToken,
              QStringLiteral("token"), token.text, {},
              QStringLiteral("Invalid er diagram token"));

    bool headerSeen = false;
    bool accDescriptionBlock = false;
    ErTokenCursor document(tokens_);
    while (!document.atEnd() && document.peek().kind != ErTokenKind::Eof) {
      ErTokenCursor line = document.consumeLine();
      const qsizetype lineOffset =
          line.atEnd() ? document.peek().offset : line.peek().offset;

      if (accDescriptionBlock) {
        if (line.match(ErTokenKind::CloseBrace) && line.atEnd()) {
          accDescriptionBlock = false;
        } else {
          const QString text = line.raw(source_);
          if (!data_.accDescription.isEmpty()) data_.accDescription += QLatin1Char('\n');
          data_.accDescription += text;
        }
        continue;
      }

      // Comments and blank lines are never meaningful (even before the header).
      if (!line.atEnd() && line.peek().kind == ErTokenKind::Comment) {
        line.consume();
        continue;
      }
      if (line.atEnd()) continue;

      if (!headerSeen) {
        if (line.match(ErTokenKind::ErHeader) && line.atEnd()) {
          headerSeen = true;
          continue;
        }
        raise(source_, lineOffset, ErErrorStage::Detector, ErErrorCode::MissingHeader,
              QStringLiteral("erDiagram"), line.raw(source_),
              {QStringLiteral("erDiagram")});
      }

      // Inside an entity block: every remaining line is an attribute or the
      // closing brace.
      if (!openEntityId_.isEmpty()) {
        parseEntityMemberLine(line, lineOffset);
        continue;
      }

      if (line.peek().kind == ErTokenKind::Identifier &&
          line.peek().text == QLatin1String("accTitle")) {
        line.consume();
        if (!line.match(ErTokenKind::Colon))
          unexpected(line, lineOffset, QStringLiteral("acc_title"), {QStringLiteral("COLON")});
        data_.accTitle = line.raw(source_);
        continue;
      }
      if (line.peek().kind == ErTokenKind::Identifier &&
          line.peek().text == QLatin1String("accDescr")) {
        line.consume();
        if (line.match(ErTokenKind::Colon)) {
          data_.accDescription = line.raw(source_);
        } else if (line.match(ErTokenKind::OpenBrace) && line.atEnd()) {
          accDescriptionBlock = true;
          data_.accDescription.clear();
        } else {
          unexpected(line, lineOffset, QStringLiteral("acc_descr"),
                     {QStringLiteral("COLON"), QStringLiteral("STRUCT_START")});
        }
        continue;
      }
      if (line.peek().kind == ErTokenKind::Identifier &&
          line.peek().text == QLatin1String("title")) {
        line.consume();
        data_.title = line.raw(source_);
        continue;
      }

      parseStatement(line, lineOffset);
    }

    if (!headerSeen)
      raise(source_, 0, ErErrorStage::Detector, ErErrorCode::MissingHeader,
            QStringLiteral("erDiagram"), QStringLiteral("EOF"),
            {QStringLiteral("erDiagram")});
    if (accDescriptionBlock || !openEntityId_.isEmpty())
      raise(source_, source_.size(), ErErrorStage::Parser, ErErrorCode::MissingClosingBrace,
            QStringLiteral("entityStatement"), QStringLiteral("EOF_IN_STRUCT"),
            {QStringLiteral("STRUCT_STOP")});
    return data_;
  }

private:
  ErEntity& addEntity(const QString& id, qsizetype offset) {
    auto found = std::find_if(data_.entities.begin(), data_.entities.end(),
                              [&](const ErEntity& entity) { return entity.id == id; });
    if (found != data_.entities.end()) return *found;
    if (data_.entities.size() >= limits_.maxEntities)
      raise(source_, offset, ErErrorStage::Resource, ErErrorCode::LimitExceeded,
            QStringLiteral("entityIdentifier"), id, {},
            QStringLiteral("Maximum entity count exceeded"));
    ErEntity entity;
    entity.id = id;
    entity.name = id;
    data_.entities.append(std::move(entity));
    return data_.entities.back();
  }

  ErEntity* findEntity(const QString& id) {
    auto found = std::find_if(data_.entities.begin(), data_.entities.end(),
                              [&](const ErEntity& entity) { return entity.id == id; });
    return found == data_.entities.end() ? nullptr : &*found;
  }

  [[noreturn]] void unexpected(const ErTokenCursor& cursor, qsizetype fallback,
                               QString production, QStringList expected = {}) const {
    const qsizetype offset = cursor.atEnd() ? fallback : cursor.peek().offset;
    const QString actual = cursor.atEnd() ? QStringLiteral("NEWLINE")
                                          : erTokenName(cursor.peek().kind);
    raise(source_, offset, ErErrorStage::Parser, ErErrorCode::UnexpectedToken,
          std::move(production), actual, std::move(expected));
  }

  bool tokenKindAt(qsizetype index, qsizetype end, ErTokenKind kind) const {
    return index >= 0 && index < end && index < tokens_.size() &&
           tokens_[index].kind == kind;
  }

  void parseStatement(ErTokenCursor line, qsizetype offset) {
    if (line.atEnd()) return;
    const ErTokenKind leadKind = line.peek().kind;
    if (leadKind != ErTokenKind::Identifier && leadKind != ErTokenKind::QuotedText)
      unexpected(line, offset, QStringLiteral("statement"),
                 {QStringLiteral("entityName"), QStringLiteral("relationship")});

    // Relationship probe: consumes a copy, leaves `line` untouched on failure.
    if (parseRelationship(line, offset)) return;

    if (leadKind == ErTokenKind::Identifier) {
      parseEntityHeader(line, offset);
      return;
    }
    unexpected(line, offset, QStringLiteral("statement"),
               {QStringLiteral("entityName"), QStringLiteral("relationship")});
  }

  // Returns true when the line is a (complete) relationship. Returns false only
  // when no CARDINALITY is present (so the caller can retry as an entity
  // statement). Once a cardinality is seen the line is committed and any
  // incompleteness raises MissingRelationTarget / InvalidCardinality — mirroring
  // ClassDiagram::parseRelation's "partial connector" handling.
  bool parseRelationship(ErTokenCursor cursor, qsizetype offset) {
    QString roleA;
    if (!cursor.atEnd() && cursor.peek().kind == ErTokenKind::QuotedText)
      roleA = cursor.consume().text;

    if (cursor.atEnd() || cursor.peek().kind != ErTokenKind::Identifier) return false;
    const ErToken entityAToken = cursor.consume();
    const QString entityAId = entityAToken.text;

    // roleA may trail entityA, but only when a CARDINALITY follows the quote
    // (otherwise `IDENT QUOTED_TEXT` is an entity alias, handled elsewhere).
    if (roleA.isEmpty() && !cursor.atEnd() &&
        cursor.peek().kind == ErTokenKind::QuotedText &&
        tokenKindAt(cursor.position() + 1, cursor.endPosition(), ErTokenKind::Cardinality))
      roleA = cursor.consume().text;

    if (cursor.atEnd() || cursor.peek().kind != ErTokenKind::Cardinality) return false;
    const ErToken leftCardToken = cursor.consume();
    const ErCardinality cardA = cardinalityFromGlyph(leftCardToken.text);

    bool identifying = true;
    if (!cursor.atEnd() && cursor.peek().kind == ErTokenKind::Hyphen) {
      const ErToken mid = cursor.consume();
      if (mid.text.size() != 2)
        raise(source_, mid.offset, ErErrorStage::Parser, ErErrorCode::InvalidCardinality,
              QStringLiteral("relationship"), mid.text, {QStringLiteral("--")},
              QStringLiteral("Relationship connector must be '--'"));
      identifying = true;
    } else if (!cursor.atEnd() && cursor.peek().kind == ErTokenKind::Dot) {
      const ErToken mid = cursor.consume();
      if (mid.text.size() != 2)
        raise(source_, mid.offset, ErErrorStage::Parser, ErErrorCode::InvalidCardinality,
              QStringLiteral("relationship"), mid.text, {QStringLiteral("..")},
              QStringLiteral("Relationship connector must be '..'"));
      identifying = false;
    } else {
      raise(source_, cursor.atEnd() ? leftCardToken.offset : cursor.peek().offset,
            ErErrorStage::Parser, ErErrorCode::MissingRelationTarget,
            QStringLiteral("relationship"),
            cursor.atEnd() ? QStringLiteral("NEWLINE") : erTokenName(cursor.peek().kind),
            {QStringLiteral("--"), QStringLiteral("..")});
    }

    if (cursor.atEnd() || cursor.peek().kind != ErTokenKind::Cardinality)
      raise(source_, cursor.atEnd() ? offset : cursor.peek().offset,
            ErErrorStage::Parser, ErErrorCode::MissingRelationTarget,
            QStringLiteral("relationship"),
            cursor.atEnd() ? QStringLiteral("NEWLINE") : erTokenName(cursor.peek().kind),
            {QStringLiteral("CARDINALITY")});
    const ErToken rightCardToken = cursor.consume();
    const ErCardinality cardB = cardinalityFromGlyph(rightCardToken.text);

    QString roleB;
    if (!cursor.atEnd() && cursor.peek().kind == ErTokenKind::QuotedText)
      roleB = cursor.consume().text;
    if (cursor.atEnd() || cursor.peek().kind != ErTokenKind::Identifier)
      raise(source_, cursor.atEnd() ? rightCardToken.offset : cursor.peek().offset,
            ErErrorStage::Parser, ErErrorCode::MissingRelationTarget,
            QStringLiteral("relationship"),
            cursor.atEnd() ? QStringLiteral("NEWLINE") : erTokenName(cursor.peek().kind),
            {QStringLiteral("entityName")});
    const ErToken entityBToken = cursor.consume();
    const QString entityBId = entityBToken.text;
    if (roleB.isEmpty() && !cursor.atEnd() &&
        cursor.peek().kind == ErTokenKind::QuotedText)
      roleB = cursor.consume().text;

    QString label;
    QString comment;
    if (!cursor.atEnd()) {
      if (!cursor.match(ErTokenKind::Colon))
        raise(source_, cursor.peek().offset, ErErrorStage::Parser,
              ErErrorCode::UnexpectedToken, QStringLiteral("relationship"),
              erTokenName(cursor.peek().kind),
              {QStringLiteral("COLON"), QStringLiteral("NEWLINE")});
      // Remaining tokens are the label; a trailing QUOTED_TEXT (with at least
      // one token before it) is peeled into `comment`.
      const qsizetype start = cursor.position();
      const qsizetype end = cursor.endPosition();
      const bool peelComment =
          end - start >= 2 && tokens_[end - 1].kind == ErTokenKind::QuotedText;
      if (peelComment) {
        comment = tokens_[end - 1].text;
        if (end - 1 > start) {
          ErTokenCursor labelCursor(tokens_, start, end - 1);
          label = labelCursor.raw(source_);
        }
      } else {
        label = cursor.raw(source_);
      }
      label = label.trimmed();
    }

    if (data_.relationships.size() >= limits_.maxRelationships)
      raise(source_, offset, ErErrorStage::Resource, ErErrorCode::LimitExceeded,
            QStringLiteral("relationshipStatement"), cursor.raw(source_), {},
            QStringLiteral("Maximum relationship count exceeded"));

    // A relationship implies its entity nodes even without an explicit block.
    addEntity(entityAId, entityAToken.offset);
    addEntity(entityBId, entityBToken.offset);

    ErRelationship relationship;
    relationship.id = QStringLiteral("rel%1").arg(data_.relationships.size());
    relationship.entityA = entityAId;
    relationship.entityB = entityBId;
    relationship.cardA = cardA;
    relationship.cardB = cardB;
    relationship.identifying = identifying;
    relationship.roleA = roleA;
    relationship.roleB = roleB;
    relationship.label = label;
    relationship.comment = comment;
    data_.relationships.append(std::move(relationship));
    return true;
  }

  void parseEntityHeader(ErTokenCursor line, qsizetype offset) {
    const ErToken idToken = line.consume();  // IDENTIFIER
    const QString entityId = idToken.text;
    QString alias;
    if (!line.atEnd() && line.peek().kind == ErTokenKind::QuotedText)
      alias = line.consume().text;

    if (line.match(ErTokenKind::OpenBrace)) {
      ErEntity& entity = addEntity(entityId, idToken.offset);
      if (!alias.isEmpty()) entity.name = alias;
      openEntityId_ = entityId;
      if (!line.atEnd()) parseEntityMemberLine(line, offset);
      return;
    }

    if (line.atEnd()) {
      // Bare entity declaration (with or without display alias). Lenient, like
      // `class Foo` in the class diagram.
      ErEntity& entity = addEntity(entityId, idToken.offset);
      if (!alias.isEmpty()) entity.name = alias;
      return;
    }
    unexpected(line, offset, QStringLiteral("entityStatement"),
               {QStringLiteral("STRUCT_START"), QStringLiteral("NEWLINE")});
  }

  void parseEntityMemberLine(ErTokenCursor line, qsizetype offset) {
    while (!line.atEnd()) {
      const ErTokenKind kind = line.peek().kind;
      if (kind == ErTokenKind::Comment) {
        line.consume();
        continue;
      }
      if (kind == ErTokenKind::CloseBrace) {
        line.consume();
        openEntityId_.clear();
        if (!line.atEnd())
          unexpected(line, offset, QStringLiteral("entityStatement"),
                     {QStringLiteral("NEWLINE")});
        return;
      }
      parseAttribute(line, offset);
    }
  }

  void parseAttribute(ErTokenCursor& line, qsizetype offset) {
    if (line.atEnd() || line.peek().kind != ErTokenKind::Identifier)
      raise(source_, line.atEnd() ? offset : line.peek().offset,
            ErErrorStage::Parser, ErErrorCode::InvalidAttribute,
            QStringLiteral("attribute"),
            line.atEnd() ? QStringLiteral("NEWLINE") : erTokenName(line.peek().kind),
            {QStringLiteral("attributeType")});
    const ErToken typeToken = line.consume();
    if (line.atEnd() || line.peek().kind != ErTokenKind::Identifier)
      raise(source_, line.atEnd() ? offset : line.peek().offset,
            ErErrorStage::Parser, ErErrorCode::InvalidAttribute,
            QStringLiteral("attribute"),
            line.atEnd() ? QStringLiteral("NEWLINE") : erTokenName(line.peek().kind),
            {QStringLiteral("attributeName")});
    const ErToken nameToken = line.consume();

    ErAttribute attribute;
    attribute.attributeType = typeToken.text;
    attribute.attributeName = nameToken.text;

    // Optional single key marker: PK/FK/UK (either dedicated token kinds or
    // bare IDENTIFIERs whose text is the keyword).
    if (!line.atEnd()) {
      const ErTokenKind kind = line.peek().kind;
      const QString text = line.peek().text;
      ErAttributeKeyType key = keyTypeFromWord(text);
      const bool dedicatedKey = kind == ErTokenKind::KeyPK ||
                                kind == ErTokenKind::KeyFK ||
                                kind == ErTokenKind::KeyUK;
      if (key != ErAttributeKeyType::None && (dedicatedKey || kind == ErTokenKind::Identifier)) {
        attribute.keyType = key;
        line.consume();
      }
    }

    if (!line.atEnd() && line.peek().kind == ErTokenKind::QuotedText)
      attribute.comment = line.consume().text;

    // A trailing CLOSEBRACE (same-line block close) is allowed; anything else
    // is an invalid attribute.
    if (!line.atEnd() && line.peek().kind != ErTokenKind::CloseBrace)
      raise(source_, line.peek().offset, ErErrorStage::Parser,
            ErErrorCode::InvalidAttribute, QStringLiteral("attribute"),
            erTokenName(line.peek().kind),
            {QStringLiteral("NEWLINE"), QStringLiteral("STRUCT_STOP")});

    ErEntity* entity = findEntity(openEntityId_);
    if (!entity)
      raise(source_, offset, ErErrorStage::Parser, ErErrorCode::UnexpectedToken,
            QStringLiteral("attribute"), openEntityId_, {},
            QStringLiteral("Attribute outside of an entity block"));
    if (entity->attributes.size() >= limits_.maxAttributesPerEntity)
      raise(source_, offset, ErErrorStage::Resource, ErErrorCode::LimitExceeded,
            QStringLiteral("attribute"), attribute.attributeName, {},
            QStringLiteral("Maximum attributes per entity exceeded"));
    entity->attributes.append(std::move(attribute));
  }

  QString source_;
  ErLimits limits_;
  QVector<ErToken> tokens_;
  ErDiagramData data_;
  QString openEntityId_;  // non-empty while an entity block is open (no nesting in er)
};

}  // namespace

muffin::mermaid::er::ErParseError::ErParseError(ErDiagnostic diagnostic)
    : std::runtime_error(formatErDiagnostic(diagnostic).toUtf8().constData()),
      diagnostic_(std::move(diagnostic)) {}

const ErDiagnostic& muffin::mermaid::er::ErParseError::diagnostic() const noexcept {
  return diagnostic_;
}

muffin::mermaid::er::ErDiagram muffin::mermaid::er::ErDiagram::parse(const QString& source,
                                                                     ErLimits limits) {
  ErDiagram diagram;
  diagram.data_ = Parser(source, limits).parse();
  return diagram;
}

QJsonObject muffin::mermaid::er::ErDiagram::toJson() const {
  QJsonArray entities;
  for (const ErEntity& entity : data_.entities) {
    QJsonArray attributes;
    for (const ErAttribute& attribute : entity.attributes) {
      attributes.append(QJsonObject{
          {QStringLiteral("type"), attribute.attributeType},
          {QStringLiteral("name"), attribute.attributeName},
          {QStringLiteral("comment"), attribute.comment},
          {QStringLiteral("keyType"), attributeKeyTypeName(attribute.keyType)}});
    }
    entities.append(QJsonObject{{QStringLiteral("id"), entity.id},
                                {QStringLiteral("name"), entity.name},
                                {QStringLiteral("attributes"), attributes}});
  }
  QJsonArray relationships;
  for (const ErRelationship& relationship : data_.relationships) {
    relationships.append(QJsonObject{
        {QStringLiteral("id"), relationship.id},
        {QStringLiteral("entityA"), relationship.entityA},
        {QStringLiteral("entityB"), relationship.entityB},
        {QStringLiteral("cardA"), cardinalityName(relationship.cardA)},
        {QStringLiteral("cardB"), cardinalityName(relationship.cardB)},
        {QStringLiteral("identifying"), relationship.identifying},
        {QStringLiteral("roleA"), relationship.roleA},
        {QStringLiteral("roleB"), relationship.roleB},
        {QStringLiteral("label"), relationship.label},
        {QStringLiteral("comment"), relationship.comment}});
  }
  return {{QStringLiteral("title"), data_.title},
          {QStringLiteral("accTitle"), data_.accTitle},
          {QStringLiteral("accDescription"), data_.accDescription},
          {QStringLiteral("entities"), entities},
          {QStringLiteral("relationships"), relationships}};
}

QString muffin::mermaid::er::erErrorStageName(ErErrorStage stage) {
  switch (stage) {
    case ErErrorStage::Detector: return QStringLiteral("detector");
    case ErErrorStage::Lexer: return QStringLiteral("lexer");
    case ErErrorStage::Parser: return QStringLiteral("parser");
    case ErErrorStage::Semantic: return QStringLiteral("semantic");
    case ErErrorStage::Resource: return QStringLiteral("resource");
  }
  return {};
}

QString muffin::mermaid::er::erErrorCodeName(ErErrorCode code) {
  switch (code) {
    case ErErrorCode::MissingHeader: return QStringLiteral("missing-header");
    case ErErrorCode::UnexpectedToken: return QStringLiteral("unexpected-token");
    case ErErrorCode::MissingClosingBrace: return QStringLiteral("missing-closing-brace");
    case ErErrorCode::MissingRelationTarget: return QStringLiteral("missing-relation-target");
    case ErErrorCode::InvalidCardinality: return QStringLiteral("invalid-cardinality");
    case ErErrorCode::InvalidAttribute: return QStringLiteral("invalid-attribute");
    case ErErrorCode::LimitExceeded: return QStringLiteral("limit-exceeded");
  }
  return {};
}

QString muffin::mermaid::er::formatErDiagnostic(const ErDiagnostic& diagnostic) {
  return QStringLiteral("%1 at %2:%3 (%4)")
      .arg(diagnostic.detail.isEmpty() ? erErrorCodeName(diagnostic.code) : diagnostic.detail)
      .arg(diagnostic.span.line)
      .arg(diagnostic.span.column)
      .arg(erErrorStageName(diagnostic.stage));
}
