#include "mermaid/erdiagram/ErTokenizer.h"

#include <QRegularExpression>

#include <algorithm>
#include <optional>

// Mermaid's lexer for erDiagram, rule for rule.
//
// The grammar's lexer (src/diagrams/er/parser/erDiagram.jison in mermaid
// 11.16.0) is a table of rules per condition; jison takes the first rule of
// the current condition that matches at the cursor, whatever its length. The
// table below keeps Mermaid's order, because the order is the behaviour: `one`
// is a cardinality because its rule comes before the one for bare words, and
// `class` opens no style condition because only `style` and `classDef` say so.
//
// Two JavaScript pattern classes have no exact PCRE spelling and are spelled
// out: `\s` (PCRE's stops at ASCII) and `.` (PCRE's also matches \r).

// JavaScript's \s.
#define ER_WS                                                                                                          \
  "[\\t\\n\\v\\f\\r \\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}\\x{FEFF}]"
// The negation of JavaScript's \s.
#define ER_NOT_WS                                                                                                      \
  "[^\\t\\n\\v\\f\\r \\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}\\x{FEFF}]"
// JavaScript's `.`: anything but a line terminator.
#define ER_DOT "[^\\n\\r\\x{2028}\\x{2029}]"
// The non-ASCII end of Mermaid's letter class, \u00C0-\uFFFF.
#define ER_HIGH_LETTER "\\x{C0}-\\x{FFFF}"

namespace {

using muffin::mermaid::DiagramError;
using muffin::mermaid::er::ErDiagnostic;
using muffin::mermaid::er::ErErrorCode;
using muffin::mermaid::er::ErErrorStage;
using muffin::mermaid::er::ErLexerCondition;
using muffin::mermaid::er::ErParseError;
using muffin::mermaid::er::ErToken;
using muffin::mermaid::er::ErTokenKind;

struct Rule {
  ErLexerCondition condition;
  const char* pattern;
  std::optional<ErTokenKind> kind; // nullopt: the rule consumes and returns nothing
  const char* text = nullptr;      // replaces the matched text when set
  bool unquote = false;            // Mermaid's action drops every `"` from the text
  bool enters = false;             // begin(entered)
  ErLexerCondition entered = ErLexerCondition::Initial;
  bool leaves = false; // popState()
  QRegularExpression expression;
};

QString stripQuotes(const QString& text) {
  QString result;
  for (const QChar character : text)
    if (character != QLatin1Char('"'))
      result += character;
  return result;
}

QVector<Rule> buildRules() {
  const auto skip = std::optional<ErTokenKind>();
  const auto token = [](ErTokenKind kind) { return std::optional<ErTokenKind>(kind); };
  const QRegularExpression::PatternOptions options = QRegularExpression::CaseInsensitiveOption;
  QVector<Rule> rules;
  const auto add = [&](ErLexerCondition condition, const char* pattern, std::optional<ErTokenKind> kind,
                       const char* text = nullptr, bool unquote = false, bool enters = false,
                       ErLexerCondition entered = ErLexerCondition::Initial, bool leaves = false) {
    QRegularExpression expression(QString::fromUtf8(pattern), options);
    Q_ASSERT(expression.isValid());
    rules.append(
        Rule{condition, pattern, std::move(kind), text, unquote, enters, entered, leaves, std::move(expression)});
  };
  using C = ErLexerCondition;
  using K = ErTokenKind;

  // ---- INITIAL: the document
  // Three of Mermaid's rules here never match, an earlier rule taking all
  // they would: its second `[\s]+`, its second `#`, and one for the text
  // "acc_descr_multiline_value". They are left out.
  add(C::Initial, "accTitle" ER_WS "*:" ER_WS "*", token(K::AccTitle), nullptr, false, true, C::AccTitle);
  add(C::Initial, "accDescr" ER_WS "*:" ER_WS "*", token(K::AccDescr), nullptr, false, true, C::AccDescr);
  add(C::Initial, "accDescr" ER_WS "*\\{" ER_WS "*", skip, nullptr, false, true, C::AccDescrBlock);
  // A line holding `direction TB` (in any case) is a direction statement, whatever else is on it.
  add(C::Initial, ER_DOT "*direction" ER_WS "+TB[^\\n]*", token(K::Direction), "TB");
  add(C::Initial, ER_DOT "*direction" ER_WS "+BT[^\\n]*", token(K::Direction), "BT");
  add(C::Initial, ER_DOT "*direction" ER_WS "+RL[^\\n]*", token(K::Direction), "RL");
  add(C::Initial, ER_DOT "*direction" ER_WS "+LR[^\\n]*", token(K::Direction), "LR");
  add(C::Initial, "[\\n]+", token(K::Newline));
  add(C::Initial, ER_WS "+", skip);
  add(C::Initial, "\"[^\"%\\r\\n\\v\\b\\\\]+\"", token(K::EntityName), nullptr, true);
  add(C::Initial, "\"[^\"]*\"", token(K::Word), nullptr, true);
  add(C::Initial, "erDiagram\\b", token(K::Header));
  add(C::Initial, "\\{", token(K::BlockStart), nullptr, false, true, C::Block);
  add(C::Initial, "#", token(K::Hash));
  add(C::Initial, ",", token(K::Comma));
  add(C::Initial, ":::", token(K::StyleSeparator));
  add(C::Initial, ":", token(K::Colon));
  add(C::Initial, "\\[", token(K::BracketOpen));
  add(C::Initial, "\\]", token(K::BracketClose));
  add(C::Initial, "style\\b", token(K::Style), nullptr, false, true, C::Style);
  add(C::Initial, "classDef\\b", token(K::ClassDef), nullptr, false, true, C::Style);
  add(C::Initial, "class\\b", token(K::Class));
  add(C::Initial, "one or zero", token(K::Cardinality), "ZERO_OR_ONE");
  add(C::Initial, "one or more", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "one or many", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "1\\+", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "\\|o\\b", token(K::Cardinality), "ZERO_OR_ONE");
  add(C::Initial, "zero or one", token(K::Cardinality), "ZERO_OR_ONE");
  add(C::Initial, "zero or more", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "zero or many", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "0\\+", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "\\}o\\b", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "many\\(0\\)", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "many\\(1\\)", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "many\\b", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "\\}\\|", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "one\\b", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "only one", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "[0-9]+\\.[0-9]+", token(K::Decimal));
  // A "1" followed by a name, a number or a connector is the cardinality
  // "exactly one"; anywhere else it can name an entity.
  add(C::Initial, "1(?=" ER_WS "+[A-Za-z_\"'])", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "1(?=" ER_WS "+[0-9])", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "1(?=(--|\\.\\.|\\.-|-\\.))", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "1\\b", token(K::EntityOne));
  add(C::Initial, "[0-9]+", token(K::Number));
  add(C::Initial, "\\|\\|", token(K::Cardinality), "ONLY_ONE");
  add(C::Initial, "o\\|", token(K::Cardinality), "ZERO_OR_ONE");
  add(C::Initial, "o\\{", token(K::Cardinality), "ZERO_OR_MORE");
  add(C::Initial, "\\|\\{", token(K::Cardinality), "ONE_OR_MORE");
  add(C::Initial, "u(?=[.\\-|])", token(K::Cardinality), "MD_PARENT");
  add(C::Initial, "\\.\\.", token(K::NonIdentifying));
  add(C::Initial, "--", token(K::Identifying));
  add(C::Initial, "to\\b", token(K::Identifying));
  add(C::Initial, "optionally to", token(K::NonIdentifying));
  add(C::Initial, "\\.-", token(K::NonIdentifying));
  add(C::Initial, "-\\.", token(K::NonIdentifying));
  add(C::Initial, "([^\\x00-\\x7F]|\\w|-|\\*|\\.)+", token(K::Text));
  add(C::Initial, ER_DOT, token(K::Character));

  // ---- block: between the braces of an entity
  add(C::Block, ER_WS "+", skip);
  add(C::Block, "\\b((?:PK)|(?:FK)|(?:UK))\\b", token(K::AttributeKey));
  add(C::Block, ER_NOT_WS "*[~]" ER_DOT "*[~]" ER_NOT_WS "*", token(K::AttributeWord));
  add(C::Block, "([\\*A-Za-z_" ER_HIGH_LETTER "][A-Za-z0-9\\-\\_\\[\\]\\(\\)\\.," ER_HIGH_LETTER "\\*]*)",
      token(K::AttributeWord));
  add(C::Block, "`", skip, nullptr, false, true, C::BlockQuote);
  add(C::Block, "\"[^\"]*\"", token(K::AttributeComment), nullptr, true);
  add(C::Block, "[\\n]+", skip);
  add(C::Block, "\\}", token(K::BlockStop), nullptr, false, false, C::Initial, true);
  add(C::Block, ER_DOT, token(K::Character));

  // ---- block_bq: a backtick word of a block
  add(C::BlockQuote, "[^`]+", token(K::AttributeWord));
  add(C::BlockQuote, "`", skip, nullptr, false, false, C::Initial, true);

  // ---- style: after style or classDef, up to the end of the line
  add(C::Style, "[\\n]+", token(K::Newline), nullptr, false, false, C::Initial, true);
  add(C::Style, ER_WS "+", skip);
  add(C::Style, ":", token(K::Colon));
  add(C::Style, ",", token(K::Comma));
  add(C::Style, "#", token(K::Hash));
  add(C::Style, "([^\\x00-\\x7F]|\\w|-|\\*)+", token(K::StyleText));
  add(C::Style, ";", token(K::Semi));

  // ---- acc_descr_multiline: between the braces of accDescr
  add(C::AccDescrBlock, "\\}", skip, nullptr, false, false, C::Initial, true);
  add(C::AccDescrBlock, "[^}]*", token(K::AccDescrBlock));

  // ---- acc_descr and acc_title: the rest of the line
  add(C::AccDescr, "[^\\n]*", token(K::AccDescrValue), nullptr, false, false, C::Initial, true);
  add(C::AccTitle, "[^\\n]*", token(K::AccTitleValue), nullptr, false, false, C::Initial, true);

  return rules;
}

const QVector<Rule>& rules() {
  static const QVector<Rule> table = buildRules();
  return table;
}

} // namespace

namespace muffin::mermaid::er {

QString erTokenName(ErTokenKind kind) {
  switch (kind) {
  case ErTokenKind::Eof:
    return QStringLiteral("EOF");
  case ErTokenKind::InputEnd:
    return QStringLiteral("1");
  case ErTokenKind::Header:
    return QStringLiteral("ER_DIAGRAM");
  case ErTokenKind::Newline:
    return QStringLiteral("NEWLINE");
  case ErTokenKind::EntityName:
    return QStringLiteral("ENTITY_NAME");
  case ErTokenKind::Word:
    return QStringLiteral("WORD");
  case ErTokenKind::Text:
    return QStringLiteral("UNICODE_TEXT");
  case ErTokenKind::Number:
    return QStringLiteral("NUM");
  case ErTokenKind::Decimal:
    return QStringLiteral("DECIMAL_NUM");
  case ErTokenKind::EntityOne:
    return QStringLiteral("ENTITY_ONE");
  case ErTokenKind::BlockStart:
    return QStringLiteral("BLOCK_START");
  case ErTokenKind::BlockStop:
    return QStringLiteral("BLOCK_STOP");
  case ErTokenKind::BracketOpen:
    return QStringLiteral("SQS");
  case ErTokenKind::BracketClose:
    return QStringLiteral("SQE");
  case ErTokenKind::StyleSeparator:
    return QStringLiteral("STYLE_SEPARATOR");
  case ErTokenKind::Colon:
    return QStringLiteral("COLON");
  case ErTokenKind::Comma:
    return QStringLiteral("COMMA");
  case ErTokenKind::Semi:
    return QStringLiteral("SEMI");
  case ErTokenKind::Hash:
    return QStringLiteral("BRKT");
  case ErTokenKind::ClassDef:
    return QStringLiteral("CLASSDEF");
  case ErTokenKind::Class:
    return QStringLiteral("CLASS");
  case ErTokenKind::Style:
    return QStringLiteral("STYLE");
  case ErTokenKind::StyleText:
    return QStringLiteral("STYLE_TEXT");
  case ErTokenKind::AccTitle:
    return QStringLiteral("acc_title");
  case ErTokenKind::AccDescr:
    return QStringLiteral("acc_descr");
  case ErTokenKind::AccTitleValue:
    return QStringLiteral("acc_title_value");
  case ErTokenKind::AccDescrValue:
    return QStringLiteral("acc_descr_value");
  case ErTokenKind::AccDescrBlock:
    return QStringLiteral("acc_descr_multiline_value");
  case ErTokenKind::Direction:
    return QStringLiteral("direction");
  case ErTokenKind::Cardinality:
    return QStringLiteral("CARDINALITY");
  case ErTokenKind::Identifying:
    return QStringLiteral("IDENTIFYING");
  case ErTokenKind::NonIdentifying:
    return QStringLiteral("NON_IDENTIFYING");
  case ErTokenKind::AttributeWord:
    return QStringLiteral("ATTRIBUTE_WORD");
  case ErTokenKind::AttributeKey:
    return QStringLiteral("ATTRIBUTE_KEY");
  case ErTokenKind::AttributeComment:
    return QStringLiteral("COMMENT");
  case ErTokenKind::Question:
    return QStringLiteral("?");
  case ErTokenKind::Character:
    return QStringLiteral("CHARACTER");
  }
  return {};
}

// The catch-all rule hands a character back as a token of that character's
// name. Two such names are terminals of the grammar inside a block.
ErTokenKind characterToken(const QString& matched) {
  if (matched == QLatin1String(","))
    return ErTokenKind::Comma;
  if (matched == QLatin1String("?"))
    return ErTokenKind::Question;
  return ErTokenKind::Character;
}

ErToken ErTokenizer::next() {
  ErToken token;
  token.line = line_;
  token.endLine = line_;
  token.column = column_;
  token.offset = offset_;
  if (finished_) {
    if (conditions_.last() != ErLexerCondition::Initial)
      token.kind = ErTokenKind::InputEnd;
    return token;
  }
  const ErLexerCondition condition = conditions_.last();
  if (offset_ >= source_.size()) {
    // At the end of the input Mermaid's lexer answers EOF without consulting a
    // rule, with one exception: inside an unterminated `accDescr {` the rule
    // for the block's text matches the empty remainder once.
    if (condition == ErLexerCondition::AccDescrBlock) {
      finished_ = true;
      token.kind = ErTokenKind::AccDescrBlock;
      return token;
    }
    finished_ = true;
    if (condition != ErLexerCondition::Initial)
      token.kind = ErTokenKind::InputEnd;
    return token;
  }

  for (const Rule& rule : rules()) {
    if (rule.condition != condition)
      continue;
    const QRegularExpressionMatch match = rule.expression.match(source_, offset_, QRegularExpression::NormalMatch,
                                                                QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch())
      continue;
    const QString matched = match.captured(0);
    // Mermaid's line and column bookkeeping: a newline resets the column.
    const qsizetype lastNewline = matched.lastIndexOf(QLatin1Char('\n'));
    if (lastNewline < 0) {
      column_ += static_cast<int>(matched.size());
    } else {
      line_ += static_cast<int>(std::count(matched.cbegin(), matched.cend(), QLatin1Char('\n')));
      column_ = static_cast<int>(matched.size() - lastNewline - 1);
    }
    offset_ += matched.size();
    if (rule.leaves && conditions_.size() > 1)
      conditions_.removeLast();
    if (rule.enters)
      conditions_.append(rule.entered);
    if (!rule.kind) {
      token.line = line_;
      token.column = column_;
      token.offset = offset_;
      return next();
    }
    token.kind = *rule.kind == ErTokenKind::Character ? characterToken(matched) : *rule.kind;
    token.length = matched.size();
    token.endLine = line_;
    token.text = rule.text != nullptr ? QString::fromUtf8(rule.text) : rule.unquote ? stripQuotes(matched) : matched;
    return token;
  }

  // No rule of this condition matches: a lexical error. It names the
  // character Mermaid's own message points at.
  throw ErParseError({ErErrorStage::Lexer,
                      ErErrorCode::UnexpectedToken,
                      {offset_, 1, line_, column_},
                      QStringLiteral("token"),
                      source_.mid(offset_, 1),
                      QStringLiteral("Unrecognized text"),
                      {}});
}

QVector<ErToken> ErTokenizer::tokenize() {
  QVector<ErToken> result;
  for (ErToken token = next(); token.kind != ErTokenKind::Eof; token = next())
    result.append(std::move(token));
  return result;
}

} // namespace muffin::mermaid::er
