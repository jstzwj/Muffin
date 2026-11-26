#pragma once

#include "mermaid/erdiagram/ErDiagram.h"

#include <QString>
#include <QVector>

namespace muffin::mermaid::er {

// The lexer conditions of Mermaid's erDiagram grammar, each with its own
// ordered rule table:
//   Initial         the document
//   Block           after `{`: attribute words, keys, quoted comments
//   BlockQuote      inside a `...` word of a block
//   Style           after `style` or `classDef`, until the end of the line
//   AccTitle        after `accTitle:`, for the rest of the line
//   AccDescr        after `accDescr:`, for the rest of the line
//   AccDescrBlock   after `accDescr {`, until the closing brace
enum class ErLexerCondition : quint8 { Initial, Block, BlockQuote, Style, AccTitle, AccDescr, AccDescrBlock };

// Tokens of Mermaid's erDiagram grammar (src/diagrams/er/parser/erDiagram.jison,
// as built in mermaid 11.16.0). The comment on each kind is the grammar's
// terminal; erTokenName() returns that name for diagnostics, so an error says
// what Mermaid's would.
enum class ErTokenKind {
  // Two ways the input can end, as in Mermaid's lexer. Eof is the EOF the
  // grammar's start production ends with, read in the document condition.
  // InputEnd is the lexer's own end, read inside an unterminated block,
  // backtick word or accDescr block; no production takes it.
  Eof,
  InputEnd,
  Header,           // ER_DIAGRAM
  Newline,          // NEWLINE
  EntityName,       // ENTITY_NAME: a quoted name; text is without the quotes
  Word,             // WORD: a quoted string that is not a valid ENTITY_NAME
  Text,             // UNICODE_TEXT
  Number,           // NUM
  Decimal,          // DECIMAL_NUM
  EntityOne,        // ENTITY_ONE: a "1" that can name an entity
  BlockStart,       // BLOCK_START
  BlockStop,        // BLOCK_STOP
  BracketOpen,      // SQS
  BracketClose,     // SQE
  StyleSeparator,   // STYLE_SEPARATOR
  Colon,            // COLON
  Comma,            // COMMA
  Semi,             // SEMI
  Hash,             // BRKT
  ClassDef,         // CLASSDEF
  Class,            // CLASS
  Style,            // STYLE
  StyleText,        // STYLE_TEXT: a word of a classDef or style statement
  AccTitle,         // acc_title
  AccDescr,         // acc_descr
  AccTitleValue,    // acc_title_value
  AccDescrValue,    // acc_descr_value
  AccDescrBlock,    // acc_descr_multiline_value
  Direction,        // direction_tb and alike; text is TB, BT, RL or LR
  Cardinality,      // text is Mermaid's name: ONLY_ONE, ZERO_OR_ONE, ONE_OR_MORE, ZERO_OR_MORE or MD_PARENT
  Identifying,      // IDENTIFYING
  NonIdentifying,   // NON_IDENTIFYING
  AttributeWord,    // ATTRIBUTE_WORD
  AttributeKey,     // ATTRIBUTE_KEY
  AttributeComment, // COMMENT: quoted; text is without the quotes
  Question,         // "?"
  Character,        // any other character: the grammar has no use for it
};

struct ErToken {
  ErTokenKind kind = ErTokenKind::Eof;
  QString text;
  qsizetype offset = 0;
  qsizetype length = 0; // of the match, before any quote is stripped
  int line = 1;         // 1-based, where the match starts
  int endLine = 1;      // 1-based, where the match ends: Mermaid's line counter after it
  int column = 0;       // 0-based, in UTF-16 units, as Mermaid's lexer counts
};

// Mermaid's lexer for erDiagram: an ordered rule table per condition, where
// the first rule that matches at the cursor wins. A rule may switch condition
// (enter one, or return to the previous one), which is how `{`, the backtick,
// the style keywords and the accTitle/accDescr keywords change what the
// following characters mean. A newline leaves the style condition and a `}`
// leaves the block condition; the rest is the table.
//
// next() throws ErParseError (stage Lexer) where Mermaid's lexer reports a
// lexical error: a character that no rule of the current condition matches.
// Only the style condition has no catch-all rule.
class ErTokenizer {
public:
  explicit ErTokenizer(QString source) : source_(std::move(source)) {}
  ErToken next();
  QVector<ErToken> tokenize();

private:
  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 0;
  bool finished_ = false;
  QVector<ErLexerCondition> conditions_{ErLexerCondition::Initial};
};

// The grammar's name of a token ("UNICODE_TEXT", "BLOCK_START", ...).
QString erTokenName(ErTokenKind kind);

} // namespace muffin::mermaid::er
