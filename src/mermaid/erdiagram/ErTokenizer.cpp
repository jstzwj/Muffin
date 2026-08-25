#include "mermaid/erdiagram/ErTokenizer.h"

#include <algorithm>
#include <stdexcept>

// erDiagram tokenizer.
//
// Mirrors src/mermaid/classdiagram/ClassTokenizer.cpp for structure: one
// `next()` call produces exactly one token, every token carries line/column/
// offset, maximal-munch lexer rules, and unrecognized characters become Invalid
// tokens (carrying ErErrorCode::UnexpectedToken) that advance one character so
// the parser can turn any Invalid into a Lexer-stage ErParseError.
//
// Token-model note (frozen header): ErToken has no `length` field (unlike
// ClassToken). raw()/rawFrom() therefore reconstruct source spans from
// `token.offset` + `token.text.size()`. This is exact for the tokens that
// matter for label reconstruction (Identifier, Space, Hyphen, Dot, punctuation
// — whose text is the verbatim source substring) and only approximate for
// QuotedText/Comment (whose text has delimiters stripped / body trimmed), which
// never terminate a `raw()` span in practice.
//
// PK/FK/UK strategy (IMPLEMENTATION_SPEC.md §2 precedence note): the spec's
// "simplest correct approach" is to emit Identifier for every
// [A-Za-z][A-Za-z0-9_-]* match — including the bare words PK/FK/UK — and let
// the parser reinterpret Identifier("PK"/"FK"/"UK") as a key marker. This
// avoids the context-sensitive lexer state the spec warns about (KeyPK/KeyFK/
// KeyUK are "only recognized ... inside an attribute block", which a stateless
// lexer cannot determine). The KeyPK/KeyFK/KeyUK kinds remain in the frozen
// enum and are named by erTokenName() so an alternative tokenizer strategy can
// adopt them later; the parser MUST accept Identifier("PK") etc. either way.

namespace {

// [A-Za-z][A-Za-z0-9_-]* (broadened to Unicode letters + combining marks for
// robustness, matching ClassTokenizer's isWordCharacter philosophy). Dash lives
// in the body so `LINE-ITEM` is a single Identifier.
bool isErIdentifierStart(QChar character) {
  return character.isLetter() || character.category() == QChar::Mark_NonSpacing;
}

bool isErIdentifierBody(QChar character) {
  return character.isLetterOrNumber() || character == QLatin1Char('_') ||
         character == QLatin1Char('-') || character.category() == QChar::Mark_NonSpacing;
}

// Word-boundary test for the `erDiagram` header: the keyword must not be the
// prefix of a longer identifier (e.g. `erDiagramX` is an identifier).
bool isErWordBoundary(QChar character) {
  return !character.isLetterOrNumber() && character != QLatin1Char('_') &&
         character != QLatin1Char('-');
}

// The seven crow's-foot cardinality glyphs (2 chars each). ExactlyOne (`||`)
// is the same on both sides; the other three cardinalities have a left and a
// right form. The "eight glyphs" wording in the frozen header comment counts
// the dotted connector `..` informally — `..` is a Dot connector token here,
// not a Cardinality, so only these seven are Cardinality.
bool isErCardinalityGlyph(QStringView pair) {
  return pair == QLatin1String("||") || pair == QLatin1String("|o") ||
         pair == QLatin1String("o|") || pair == QLatin1String("}|") ||
         pair == QLatin1String("|{") || pair == QLatin1String("}o") ||
         pair == QLatin1String("o{");
}

// True when only whitespace precedes `offset` on the current line. A backward
// scan bounded by the current line length — only invoked on an `erDiagram`
// match, so it is amortized cheap. Replaces the `atLineStart_` / `headerSeen_`
// member flags the frozen header does not provide.
bool atErLineStart(const QString& source, qsizetype offset) {
  for (qsizetype i = offset - 1; i >= 0; --i) {
    const QChar character = source[i];
    if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) return true;
    if (!character.isSpace()) return false;
  }
  return true;
}
}  // namespace

void muffin::mermaid::er::ErTokenizer::advance(QStringView text) {
  // Line tracking that survives all three newline spellings. `\r\n` bumps the
  // line once (the CR is absorbed by the following LF); a lone `\r` (old Mac)
  // or lone `\n` each bump once. Embedded newlines inside a quoted string are
  // handled here too.
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QChar character = text[i];
    if (character == QLatin1Char('\r')) {
      if (i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) continue;  // CRLF: LF bumps below
      ++line_;
      column_ = 0;
      continue;
    }
    if (character == QLatin1Char('\n')) {
      ++line_;
      column_ = 0;
      continue;
    }
    ++column_;
  }
  offset_ += text.size();
}

muffin::mermaid::er::ErToken muffin::mermaid::er::ErTokenizer::make(
    ErTokenKind kind, qsizetype length, bool stripDelimiters) {
  const qsizetype start = offset_;
  const int line = line_;
  const int column = column_;
  const QStringView raw = QStringView(source_).mid(start, length);
  advance(raw);
  QString text = raw.toString();
  if (stripDelimiters && text.size() >= 2) text = text.mid(1, text.size() - 2);
  ErToken token;
  token.kind = kind;
  token.text = std::move(text);
  token.line = line;
  token.column = column;
  token.offset = start;
  token.diagnosticCode = ErErrorCode::UnexpectedToken;
  return token;
}

muffin::mermaid::er::ErToken muffin::mermaid::er::ErTokenizer::makeInvalid(qsizetype length) {
  return make(ErTokenKind::Invalid, length);
}

QChar muffin::mermaid::er::ErTokenizer::peek(qsizetype ahead) const {
  const qsizetype index = offset_ + ahead;
  if (index < source_.size()) return source_[index];
  return QChar();
}

muffin::mermaid::er::ErToken muffin::mermaid::er::ErTokenizer::next() {
  if (offset_ >= source_.size()) {
    ErToken token;
    token.kind = ErTokenKind::Eof;
    token.line = line_;
    token.column = column_;
    token.offset = offset_;
    token.diagnosticCode = ErErrorCode::UnexpectedToken;
    return token;
  }

  const QChar current = source_[offset_];

  // 1. Newlines: \r\n | \n | \r
  if (current == QLatin1Char('\r')) {
    if (offset_ + 1 < source_.size() && source_[offset_ + 1] == QLatin1Char('\n'))
      return make(ErTokenKind::Newline, 2);
    return make(ErTokenKind::Newline, 1);
  }
  if (current == QLatin1Char('\n')) return make(ErTokenKind::Newline, 1);

  // 2. Space run (any non-newline whitespace). The run is preserved as the token
  //    text so ErTokenCursor::raw() can rebuild inter-token spacing verbatim.
  if (current.isSpace()) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && source_[end].isSpace() &&
           source_[end] != QLatin1Char('\r') && source_[end] != QLatin1Char('\n'))
      ++end;
    return make(ErTokenKind::Space, end - offset_);
  }

  const QStringView rest = QStringView(source_).mid(offset_);

  // 3. Line comment: %% to end of line. Text is the body after `%%`, trimmed.
  if (rest.startsWith(QLatin1String("%%"))) {
    qsizetype end = offset_ + 2;
    while (end < source_.size() && source_[end] != QLatin1Char('\r') &&
           source_[end] != QLatin1Char('\n'))
      ++end;
    ErToken token = make(ErTokenKind::Comment, end - offset_);
    token.text = token.text.size() >= 2 ? token.text.mid(2).trimmed() : QString();
    return token;
  }

  // 4. Quoted string: "..." with no escape handling (per upstream jison). A
  //    missing closing quote on/after EOF is an Invalid token spanning the rest.
  if (current == QLatin1Char('"')) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && source_[end] != QLatin1Char('"'))
      ++end;
    if (end >= source_.size()) return makeInvalid(source_.size() - offset_);
    return make(ErTokenKind::QuotedText, end - offset_ + 1, /*stripDelimiters=*/true);
  }

  // 4b. Backtick word (mermaid 11.16): `...` emits an Identifier whose text is
  //     the delimited body — any characters except a backtick, so attribute
  //     types and names can carry spaces/commes/etc. Upstream's lexer does this
  //     with a block_bq condition returning ATTRIBUTE_WORD; emitting Identifier
  //     here lets both attribute positions consume it without parser changes.
  //     Missing closing backtick → Invalid spanning the rest (QuotedText rule).
  if (current == QLatin1Char('`')) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && source_[end] != QLatin1Char('`'))
      ++end;
    if (end >= source_.size()) return makeInvalid(source_.size() - offset_);
    return make(ErTokenKind::Identifier, end - offset_ + 1, /*stripDelimiters=*/true);
  }

  // 5. Header keyword `erDiagram` — only as the first non-space token of a line
  //    and with a word boundary. The "header not yet emitted" guard from the
  //    spec cannot be enforced without extra member state (the frozen header
  //    exposes no such flag); a stray second `erDiagram` line would also yield
  //    ErHeader, which the parser rejects as MissingHeader/duplicate. This is
  //    the documented trade-off.
  if (rest.startsWith(QLatin1String("erDiagram"))) {
    // `erDiagram` is 9 chars; the boundary char is at offset+9 (e.g. '\n').
    const QChar after = peek(9);
    if (isErWordBoundary(after) && atErLineStart(source_, offset_))
      return make(ErTokenKind::ErHeader, 9);
    // Otherwise (`erDiagramX`, or mid-line) fall through to the Identifier rule.
  }

  // 6. Cardinality glyphs and the brace/pipe chars they overlap with. Checked
  //    before Identifier so `o{`/`o|` are Cardinality rather than Identifier
  //    `o` + `{`/`|`. A lone `|` (no 2-char glyph) is Invalid; a `}` not
  //    starting a glyph is CloseBrace; an `o` not starting a glyph falls
  //    through to Identifier.
  if (current == QLatin1Char('|') || current == QLatin1Char('}') ||
      current == QLatin1Char('o')) {
    if (isErCardinalityGlyph(rest.left(2))) return make(ErTokenKind::Cardinality, 2);
    if (current == QLatin1Char('|')) return makeInvalid(1);
    if (current == QLatin1Char('}')) return make(ErTokenKind::CloseBrace, 1);
    // current == 'o': fall through to Identifier.
  }

  // 7. Identifier: [A-Za-z][A-Za-z0-9_-]*. PK/FK/UK match here as Identifier.
  //    A single trailing '?' folds into the word (mermaid 11.16's optional-type
  //    marker: `string?` — upstream grammar concatenates ATTRIBUTE_WORD '?' into
  //    the stored type, rendering literally). Only one '?' and only at the very
  //    end, so `a?b` still tokenizes as Identifier("a?") followed by whatever
  //    follows (and `??` alone never starts an Identifier).
  if (isErIdentifierStart(current)) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && isErIdentifierBody(source_[end])) ++end;
    if (end < source_.size() && source_[end] == QLatin1Char('?') &&
        (end + 1 >= source_.size() || !isErIdentifierBody(source_[end + 1]))) {
      ++end;
    }
    return make(ErTokenKind::Identifier, end - offset_);
  }

  // 8. Hyphen run: -+ (parser validates length == 2 for a relationship
  //    connector). Only reached when the dash is not internal to an Identifier.
  if (current == QLatin1Char('-')) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && source_[end] == QLatin1Char('-')) ++end;
    return make(ErTokenKind::Hyphen, end - offset_);
  }

  // 9. Dot run: .+ (parser validates length == 2 for a non-identifying
  //    connector).
  if (current == QLatin1Char('.')) {
    qsizetype end = offset_ + 1;
    while (end < source_.size() && source_[end] == QLatin1Char('.')) ++end;
    return make(ErTokenKind::Dot, end - offset_);
  }

  // 10. Single-character punctuation.
  switch (current.unicode()) {
    case '{':
      return make(ErTokenKind::OpenBrace, 1);
    case ':':
      return make(ErTokenKind::Colon, 1);
    case ',':
      return make(ErTokenKind::Comma, 1);
    default:
      break;
  }

  // 11. Unrecognized character — Invalid, advance one (parser raises
  //    Lexer/UnexpectedToken on any Invalid token).
  return makeInvalid(1);
}

QVector<muffin::mermaid::er::ErToken> muffin::mermaid::er::ErTokenizer::tokenize() {
  QVector<ErToken> result;
  do {
    result.append(next());
  } while (result.back().kind != ErTokenKind::Eof);
  return result;
}

muffin::mermaid::er::ErTokenCursor::ErTokenCursor(const QVector<ErToken>& tokens,
                                                   qsizetype begin, qsizetype end)
    : tokens_(&tokens), position_(begin),
      end_(end < 0 ? tokens.size() : std::min(end, tokens.size())) {}

const muffin::mermaid::er::ErToken& muffin::mermaid::er::ErTokenCursor::peek(
    qsizetype lookahead) const {
  const qsizetype index = position_ + lookahead;
  if (!tokens_ || index < 0 || index >= end_)
    throw std::out_of_range("er token cursor reached the end");
  return (*tokens_)[index];
}

const muffin::mermaid::er::ErToken& muffin::mermaid::er::ErTokenCursor::consume() {
  const ErToken& token = peek();
  ++position_;
  return token;
}

bool muffin::mermaid::er::ErTokenCursor::match(ErTokenKind kind) {
  if (atEnd() || peek().kind != kind) return false;
  consume();
  return true;
}

bool muffin::mermaid::er::ErTokenCursor::matchWord(QStringView word) {
  if (atEnd() || peek().kind != ErTokenKind::Identifier || peek().text != word) return false;
  consume();
  return true;
}

const muffin::mermaid::er::ErToken& muffin::mermaid::er::ErTokenCursor::expect(ErTokenKind kind) {
  if (atEnd() || peek().kind != kind)
    throw std::invalid_argument("unexpected er diagram token");
  return consume();
}

muffin::mermaid::er::ErTokenCursor muffin::mermaid::er::ErTokenCursor::consumeLine() {
  const qsizetype begin = position_;
  while (!atEnd() && peek().kind != ErTokenKind::Newline && peek().kind != ErTokenKind::Eof)
    ++position_;
  const qsizetype end = position_;
  if (!atEnd() && peek().kind == ErTokenKind::Newline) ++position_;
  return ErTokenCursor(*tokens_, begin, end);
}

QString muffin::mermaid::er::ErTokenCursor::raw(const QString& source) const {
  return rawFrom(source, position_);
}

QString muffin::mermaid::er::ErTokenCursor::rawFrom(const QString& source,
                                                    qsizetype position) const {
  if (!tokens_ || position < 0 || position >= end_) return {};
  const ErToken& first = (*tokens_)[position];
  const ErToken& last = (*tokens_)[end_ - 1];
  const qsizetype length = (last.offset + last.text.size()) - first.offset;
  return source.mid(first.offset, length).trimmed();
}

QString muffin::mermaid::er::erTokenName(ErTokenKind kind) {
  switch (kind) {
    case ErTokenKind::Eof:
      return QStringLiteral("EOF");
    case ErTokenKind::Newline:
      return QStringLiteral("NEWLINE");
    case ErTokenKind::Space:
      return QStringLiteral("SPACE");
    case ErTokenKind::ErHeader:
      return QStringLiteral("ER_DIAGRAM");
    case ErTokenKind::Identifier:
      return QStringLiteral("ALPHA");
    case ErTokenKind::QuotedText:
      return QStringLiteral("STR");
    case ErTokenKind::Cardinality:
      return QStringLiteral("CARDINALITY");
    case ErTokenKind::Hyphen:
      return QStringLiteral("HYPHEN");
    case ErTokenKind::Dot:
      return QStringLiteral("DOT");
    case ErTokenKind::OpenBrace:
      return QStringLiteral("LBRACE");
    case ErTokenKind::CloseBrace:
      return QStringLiteral("RBRACE");
    case ErTokenKind::Colon:
      return QStringLiteral("COLON");
    case ErTokenKind::Comma:
      return QStringLiteral("COMMA");
    case ErTokenKind::KeyPK:
      return QStringLiteral("PK");
    case ErTokenKind::KeyFK:
      return QStringLiteral("FK");
    case ErTokenKind::KeyUK:
      return QStringLiteral("UK");
    case ErTokenKind::Comment:
      return QStringLiteral("COMMENT");
    case ErTokenKind::Invalid:
      return QStringLiteral("INVALID");
    case ErTokenKind::Unknown:
      return QStringLiteral("UNKNOWN");
  }
  return QStringLiteral("UNKNOWN");
}
