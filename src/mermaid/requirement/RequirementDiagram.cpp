// Native recursive-descent parser for requirementDiagram.
//
// Per CLAUDE.md / the lupdate namespace convention, this .cpp contains NO
// `namespace muffin { }` block — file-local helpers live in an anonymous
// namespace and public functions use fully-qualified
// `muffin::mermaid::requirement::` names. The module has no tr() calls.

#include "mermaid/requirement/RequirementDiagram.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace {

// Strips surrounding double-quotes from a bareword/quoted value and trims.
// (Used only by the deferred title/accTitle/accDescription directives; names,
// body values, endpoints and idList use the unified consumeToken API.)
QString unwrapValue(const QString& raw) {
  QString v = raw.trimmed();
  if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
    v = v.mid(1, v.size() - 2);
  return v.trimmed();
}

// Keyword → display type, matching RequirementType enum values.
const QHash<QString, QString>& typeKeywordMap() {
  static const QHash<QString, QString> kMap = {
      {QStringLiteral("requirement"), QStringLiteral("Requirement")},
      {QStringLiteral("functionalRequirement"), QStringLiteral("Functional Requirement")},
      {QStringLiteral("interfaceRequirement"), QStringLiteral("Interface Requirement")},
      {QStringLiteral("performanceRequirement"), QStringLiteral("Performance Requirement")},
      {QStringLiteral("physicalRequirement"), QStringLiteral("Physical Requirement")},
      {QStringLiteral("designConstraint"), QStringLiteral("Design Constraint")},
  };
  return kMap;
}

// Relationship keyword set (lowercase). Used by the relationship regex + parser.
const QSet<QString>& relationshipKeywords() {
  static const QSet<QString> kSet = {
      QStringLiteral("contains"), QStringLiteral("copies"), QStringLiteral("derives"),
      QStringLiteral("satisfies"), QStringLiteral("verifies"), QStringLiteral("refines"),
      QStringLiteral("traces"),
  };
  return kSet;
}

}  // namespace

namespace muffin::mermaid::requirement {

QString requirementTypeDisplay(const QString& keyword) {
  return typeKeywordMap().value(keyword);
}

RequirementParseError::RequirementParseError(const QString& message, int line)
    : std::runtime_error(message.toUtf8().toStdString()), line(line) {}

QString RequirementEnumDisplay::risk(const QString& keyword) {
  static const QHash<QString, QString> kMap = {
      {QStringLiteral("low"), QStringLiteral("Low")},
      {QStringLiteral("medium"), QStringLiteral("Medium")},
      {QStringLiteral("high"), QStringLiteral("High")},
  };
  return kMap.value(keyword.toLower(), keyword.trimmed());
}

QString RequirementEnumDisplay::verifyMethod(const QString& keyword) {
  static const QHash<QString, QString> kMap = {
      {QStringLiteral("analysis"), QStringLiteral("Analysis")},
      {QStringLiteral("demonstration"), QStringLiteral("Demonstration")},
      {QStringLiteral("inspection"), QStringLiteral("Inspection")},
      {QStringLiteral("test"), QStringLiteral("Test")},
  };
  return kMap.value(keyword.toLower(), keyword.trimmed());
}

namespace {

// Body-opener classification for a declaration tail. Mermaid 11.16.0 requires a
// body that spans multiple lines: `requirement X` (no body), `requirement X {}`
// (single-line) and `requirement X { junk` (content after `{`) are all Parse
// errors; only `requirement X {\n id: 1\n}` is valid.
enum class BodyKind { None, MultiLine, SingleLine, Invalid, JunkBeforeBrace };

struct NameAndClass {
  QString name;
  QStringList styleClasses;  // `::: <idList>` — comma-separated classes
  BodyKind body = BodyKind::None;
};

// Token + consumeToken are defined in full with the other token helpers below;
// declared here so parseNameTail (and parseIdList) can use them.
struct Token {
  QString value;
  bool quoted = false;
  int end = 0;  // index just past the consumed token
};
Token consumeToken(const QString& s, int pos, int lineNo);
QStringList parseIdList(const QString& raw, int lineNo);

// Parses `<name> [::: <idList>] [{]` from a declaration tail, matching mermaid's
// lexer. The name may be a double-quoted string or an UNQUOTED bareword that
// runs to the first `:::` or `{` (multi-word names like `My Requirement` are
// valid). The `:::` idList is comma-separated and whitespace-tolerant
// (`red,blue` and `red, blue` both yield [red, blue]). Body: MultiLine = `{` as
// the last token on the line (valid); SingleLine = `{ ... }` on one line;
// Invalid = content after `{`; None = no `{`.
NameAndClass parseNameTail(QString tail, int lineNo) {
  NameAndClass result;
  tail = tail.trimmed();
  // Name = one token (qString or unqString) at the start. consumeToken enforces
  // the lexer rule (first char \w; an unqString stops at : { < > - =), so an
  // invalid name (`.abc`, or `A-B`/`A:B` leaving trailing junk) is rejected here
  // or via the junk-before-brace check below. `}`/`#`/`%` are part of the name.
  const Token nameTok = consumeToken(tail, 0, lineNo);
  // An unqString is trimmed (mermaid's unqString rule); a qString keeps its
  // exact content, including leading/trailing spaces (`" X "` stays " X "), so
  // the node id matches a relationship endpoint decoded the same way.
  result.name = nameTok.quoted ? nameTok.value : nameTok.value.trimmed();
  QString rest = tail.mid(nameTok.end).trimmed();
  // `::: <idList>` up to `{` — parseIdList rejects empty/malformed lists and
  // only splits on commas OUTSIDE quotes.
  const int sep = rest.indexOf(QStringLiteral(":::"));
  if (sep >= 0) {
    QString after = rest.mid(sep + 3);
    const int braceInAfter = after.indexOf(QLatin1Char('{'));
    const QString classList = (braceInAfter >= 0) ? after.left(braceInAfter) : after;
    result.styleClasses = parseIdList(classList, lineNo);
    rest = (braceInAfter >= 0) ? after.mid(braceInAfter) : QString();
  }
  rest = rest.trimmed();
  // Body opener `{`. It must be the first token of the remaining rest (no junk
  // between the name/`:::` idList and `{`) and the last token on the line.
  const int brace = rest.indexOf(QLatin1Char('{'));
  if (brace < 0)
    result.body = BodyKind::None;
  else if (brace > 0)
    result.body = BodyKind::JunkBeforeBrace;  // e.g. `"X" junk {`
  else {
    const QString afterBrace = rest.mid(1).trimmed();
    if (afterBrace.isEmpty())
      result.body = BodyKind::MultiLine;
    else if (afterBrace.startsWith(QLatin1Char('}')))
      result.body = BodyKind::SingleLine;  // `{ ... }` on one line
    else
      result.body = BodyKind::Invalid;  // `{ junk`
  }
  return result;
}

// Parses `field: value` from a body line. Returns true if the line has a
// recognized `key:` prefix; valueOut is the RAW (trimmed, not unwrapped) value —
// parseValue validates + unwraps it.
bool parseFieldLine(const QString& line, QString& keyOut, QString& valueOut) {
  const int colon = line.indexOf(QLatin1Char(':'));
  if (colon <= 0) return false;
  keyOut = line.left(colon).trimmed().toLower();
  valueOut = line.mid(colon + 1).trimmed();
  return true;
}

// A small stateful tokenizer for mermaid's qString/unqString/idList contract.
// These replace the earlier scattered contains() checks, which kept missing
// edge cases (malformed qString, junk between tokens, comment-in-value).

// Unified token API — a direct port of mermaid's lexer rule, reused by every
// token consumer (declaration name, body value, relationship endpoint, idList)
// so the contract is consistent and edge cases can't slip through one path.
//
//   unqString = [\w][^:,\r\n{<>\-=]*    (first char an ASCII word char; then any
//                                        char except : , { < > - = and newlines.
//                                        Note: `}` IS allowed.)
//   qString   = "..."                    (decoded; must be fully consumed)
//
// Note `}` IS allowed in an unqString (verified: `text: a}b`, name `X}Y`).
// `#`/`%`/space/`.`/`/`/`;` are allowed, so comments are NOT stripped mid-token.
bool isWordChar(QChar c) {
  // Upstream JS \w is ASCII-only [A-Za-z0-9_] — a Unicode first char (e.g. a
  // CJK identifier) is a Lexical error, so do not use Unicode isLetterOrNumber.
  return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
         (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
         (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
         c == QLatin1Char('_');
}
bool isUnqStopChar(QChar c) {
  return c == QLatin1Char(':') || c == QLatin1Char(',') || c == QLatin1Char('{') ||
         c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('-') ||
         c == QLatin1Char('=') || c == QLatin1Char('\r') || c == QLatin1Char('\n');
}

// Consumes one token (qString or unqString) starting at `pos`. Throws on an
// unquoted token whose first char is not \w, or an unterminated qString.
Token consumeToken(const QString& s, int pos, int lineNo) {
  Token t;
  if (pos >= s.size())
    throw RequirementParseError(QStringLiteral("expected a value"), lineNo);
  if (s.at(pos) == QLatin1Char('"')) {
    const int close = s.indexOf(QLatin1Char('"'), pos + 1);
    if (close < 0)
      throw RequirementParseError(QStringLiteral("unterminated quoted string"), lineNo);
    t.value = s.mid(pos + 1, close - pos - 1);
    t.quoted = true;
    t.end = close + 1;
    return t;
  }
  if (!isWordChar(s.at(pos)))
    throw RequirementParseError(
        QStringLiteral("value must start with a word character or be quoted"), lineNo);
  int end = pos + 1;
  while (end < s.size() && !isUnqStopChar(s.at(end))) ++end;
  t.value = s.mid(pos, end - pos);
  t.end = end;
  return t;
}

// A body field value: one token that must consume the WHOLE raw text (trailing
// text, or a special char that stops an unqString mid-value, is rejected).
QString parseValue(const QString& raw, int lineNo) {
  const Token t = consumeToken(raw, 0, lineNo);
  if (t.end != raw.size())
    throw RequirementParseError(
        QStringLiteral("invalid value '%1' (trailing text or a special char outside quotes)").arg(raw),
        lineNo);
  if (t.value.isEmpty())
    throw RequirementParseError(QStringLiteral("empty field value"), lineNo);
  return t.value;
}

// A `::: <idList>`: comma-separated tokens, splitting ONLY on commas outside
// quotes (`::: "red,blue"` is one class "red,blue"). Empty tokens (empty list,
// leading/trailing/double comma) are rejected.
QStringList parseIdList(const QString& raw, int lineNo) {
  if (raw.trimmed().isEmpty())
    throw RequirementParseError(QStringLiteral("empty class list after ':::'"), lineNo);
  QStringList parts;
  QString current;
  bool inQuote = false;
  for (int i = 0; i < raw.size(); ++i) {
    const QChar c = raw.at(i);
    if (c == QLatin1Char('"')) {
      inQuote = !inQuote;
      current += c;
    } else if (c == QLatin1Char(',') && !inQuote) {
      parts.append(current);
      current.clear();
    } else {
      current += c;
    }
  }
  parts.append(current);
  QStringList result;
  for (const QString& part : parts) {
    const QString trimmed = part.trimmed();
    if (trimmed.isEmpty())
      throw RequirementParseError(
          QStringLiteral("malformed class list (empty/leading/trailing/double comma)"), lineNo);
    const Token t = consumeToken(trimmed, 0, lineNo);
    if (t.end != trimmed.size())
      throw RequirementParseError(
          QStringLiteral("malformed class list token '%1'").arg(trimmed), lineNo);
    if (t.value.isEmpty())
      throw RequirementParseError(QStringLiteral("empty class name"), lineNo);
    result.append(t.value);
  }
  return result;
}

bool isValidRisk(const QString& value) {
  const QString l = value.toLower();
  return l == QLatin1String("low") || l == QLatin1String("medium") || l == QLatin1String("high");
}

bool isValidVerifyMethod(const QString& value) {
  const QString l = value.toLower();
  return l == QLatin1String("analysis") || l == QLatin1String("demonstration") ||
         l == QLatin1String("inspection") || l == QLatin1String("test");
}

// --- mermaid `style` lexer condition (style-state tokenizer) ---------------
//
// `style`, `class` and `classDef` each push the lexer into the `style`
// condition (requirementDiagram-R3ZQC5DG.mjs: case 49 `style`, 59 `classDef`,
// 60 `class` all call begin("style")). The WHOLE rest of the directive line is
// then tokenized under that condition, whose rules (the `style` entry in the
// lexer's conditions table) are:
//     \w+ -> Word        ':' -> Colon       ';' -> Semicolon
//     '-' -> Minus       '#' -> Hash        ',' -> Comma
//     ' ' (single space) -> eaten (no token, case 56)
//     '"' -> opens a qString      '\n' -> pops the state
//     '%' -> PERCENT (rejected by the grammar -> Parse error)
// Any other char ('(', ')', '.', '/', '+' tab, ...) has no matching rule and is
// a Lexical error — this is exactly how mermaid surfaces rgb()/hsl()/
// opacity/fill-opacity/decimal-em/'%' as Parse errors. The grammar accepts
// Word/qString as ids, a run of Word/COLON/SEMICOLON/MINUS/HASH as one
// `styleComponent` (concatenated, spaces eaten), and COMMA as the only
// separator. qString is NOT a styleComponent, so a quoted value inside
// stylesOpt (e.g. font-family:"Courier New") is a Parse error.
enum class StyleTok { Word, Colon, Semicolon, Minus, Hash, Comma, QString };
struct StyleToken { StyleTok type; QString text; };

// Tokenizes `s` under the style condition. Throws RequirementParseError on any
// char with no matching style-state rule. Single spaces (0x20) are eaten.
QVector<StyleToken> tokenizeStyleState(const QString& s, int lineNo) {
  QVector<StyleToken> out;
  for (int i = 0; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if (c == QLatin1Char(' ')) continue;  // eaten (style rule 56, case 56)
    if (c == QLatin1Char(',')) { out.append({StyleTok::Comma, c}); continue; }
    if (c == QLatin1Char(':')) { out.append({StyleTok::Colon, c}); continue; }
    if (c == QLatin1Char(';')) { out.append({StyleTok::Semicolon, c}); continue; }
    if (c == QLatin1Char('-')) { out.append({StyleTok::Minus, c}); continue; }
    if (c == QLatin1Char('#')) { out.append({StyleTok::Hash, c}); continue; }
    if (isWordChar(c)) {
      int j = i + 1;
      while (j < s.size() && isWordChar(s.at(j))) ++j;
      out.append({StyleTok::Word, s.mid(i, j - i)});
      i = j - 1;
      continue;
    }
    if (c == QLatin1Char('"')) {
      const int close = s.indexOf(QLatin1Char('"'), i + 1);
      if (close < 0)
        throw RequirementParseError(
            QStringLiteral("unterminated quoted string in style"), lineNo);
      out.append({StyleTok::QString, s.mid(i + 1, close - i - 1)});
      i = close;
      continue;
    }
    // '%', '(', ')', '.', '/', tab, etc. — no style-state rule -> Lexical error.
    throw RequirementParseError(
        QStringLiteral("invalid character '%1' in style/class/classDef").arg(c), lineNo);
  }
  return out;
}

// Reads a greedy idList `id (',' id)*` from `toks` starting at `pos`; an id is
// a Word or qString token. Stops at the first non-Comma token after an id (the
// style state eats the separating space, so the idList/stylesOpt boundary is
// grammatical, not a delimiter token). Throws on a trailing comma. Returns the
// (possibly empty) ids and advances `pos`.
QStringList extractStyleIdList(const QVector<StyleToken>& toks, int& pos, int lineNo) {
  QStringList ids;
  const auto isIdTok = [](const StyleToken& t) {
    return t.type == StyleTok::Word || t.type == StyleTok::QString;
  };
  // An empty qString ("") is not a valid id — mermaid rejects `class A ""` etc.
  // as a Parse error, in every idList position (node / class / classDef).
  const auto takeId = [&lineNo](const StyleToken& t) -> QString {
    if (t.type == StyleTok::QString && t.text.isEmpty())
      throw RequirementParseError(
          QStringLiteral("empty quoted id is not allowed"), lineNo);
    return t.text;
  };
  if (pos >= toks.size() || !isIdTok(toks.at(pos))) return ids;
  ids.append(takeId(toks.at(pos)));
  ++pos;
  while (pos < toks.size() && toks.at(pos).type == StyleTok::Comma) {
    if (pos + 1 >= toks.size() || !isIdTok(toks.at(pos + 1)))
      throw RequirementParseError(
          QStringLiteral("malformed id list (trailing/empty comma)"), lineNo);
    ids.append(takeId(toks.at(pos + 1)));
    pos += 2;
  }
  return ids;
}

// Reads stylesOpt `component (',' component)*` from `toks` starting at `pos`.
// A component is the concatenated text of a run of Word/Colon/Semicolon/Minus/
// Hash tokens (spaces already eaten). A qString anywhere, or an empty component
// (leading/trailing/double comma), is a Parse error. Returns the (possibly
// empty) component strings and advances `pos`.
QStringList extractStylesOpt(const QVector<StyleToken>& toks, int& pos, int lineNo) {
  QStringList comps;
  const auto isCompTok = [](const StyleToken& t) {
    return t.type == StyleTok::Word || t.type == StyleTok::Colon ||
           t.type == StyleTok::Semicolon || t.type == StyleTok::Minus ||
           t.type == StyleTok::Hash;
  };
  while (pos < toks.size()) {
    if (toks.at(pos).type == StyleTok::QString)
      throw RequirementParseError(
          QStringLiteral("quoted string is not valid in a style value"), lineNo);
    if (!isCompTok(toks.at(pos)))
      throw RequirementParseError(
          QStringLiteral("malformed style list (leading/empty comma)"), lineNo);
    QString comp;
    while (pos < toks.size() && isCompTok(toks.at(pos))) {
      comp += toks.at(pos).text;
      ++pos;
    }
    comps.append(comp);
    if (pos >= toks.size()) break;  // end of stream
    if (toks.at(pos).type != StyleTok::Comma)
      throw RequirementParseError(QStringLiteral("malformed style list"), lineNo);
    ++pos;  // consume the comma
    if (pos >= toks.size())
      throw RequirementParseError(
          QStringLiteral("malformed style list (trailing comma)"), lineNo);
  }
  return comps;
}

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {}

  RequirementDiagramData parse() {
    // Split on \n; the lexer treats \r\n and \n uniformly. Strip comments +
    // trim each line; preserve order. Brace-delimited bodies are collected
    // by tracking an explicit in-body flag so the outer dispatch is flat.
    lines_ = source_.split(QLatin1Char('\n'));
    bool headerSeen = false;
    bool inBody = false;
    bool inMultilineAccDescr = false;
    // The body's owner is identified by name (not a raw pointer into a QVector,
    // which would dangle if the vector reallocates while the body is open).
    // DiscardRequirement/DiscardElement: a duplicate declaration whose body must
    // still be VALIDATED (mermaid errors on invalid fields/enums) but not stored
    // (the FIRST definition wins).
    enum class BodyOwner { None, Requirement, Element, DiscardRequirement, DiscardElement };
    BodyOwner bodyOwner = BodyOwner::None;
    QString bodyOwnerName;
    int bodyStartLine = 0;  // 1-based source line of the `{` opener (for errors)
    QStringList bodyBuffer;

    for (qsizetype i = 0; i < lines_.size(); ++i) {
      const QString rawLine = lines_.at(i);
      // In multiline accDescr, raw text (minus the closing brace) is captured.
      if (inMultilineAccDescr) {
        const QString accLine = rawLine.trimmed();
        if (accLine == QLatin1String("}")) {
          inMultilineAccDescr = false;
        } else {
          if (!data_.accDescription.isEmpty()) data_.accDescription += QLatin1Char('\n');
          data_.accDescription += accLine;
        }
        continue;
      }
      // Comments are lexer-state: a `#`/`%` starts a comment only at a token
      // boundary (a whole comment line), never inside a name/value/endpoint
      // token — `X#Y`, `text: 50% complete`, `requirement X # c {` (name "X # c")
      // are all preserved. So do NOT strip mid-line; only skip whole comment lines.
      QString line = rawLine.trimmed();
      if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('%')))
        continue;
      if (line.isEmpty()) continue;

      if (inBody) {
        if (line == QLatin1String("}")) {
          // Finalize body — look up the owner by name so the reference is stable
          // even if the requirements/elements vectors reallocated mid-body.
          if (bodyOwner == BodyOwner::Requirement) {
            RequirementNode* target = findRequirement(bodyOwnerName);
            if (target) applyRequirementBody(*target, bodyBuffer, bodyStartLine);
          } else if (bodyOwner == BodyOwner::Element) {
            ElementNode* target = findElement(bodyOwnerName);
            if (target) applyElementBody(*target, bodyBuffer, bodyStartLine);
          } else if (bodyOwner == BodyOwner::DiscardRequirement) {
            RequirementNode dummy;  // validate only; the first definition stands.
            applyRequirementBody(dummy, bodyBuffer, bodyStartLine);
          } else if (bodyOwner == BodyOwner::DiscardElement) {
            ElementNode dummy;
            applyElementBody(dummy, bodyBuffer, bodyStartLine);
          }
          inBody = false;
          bodyOwner = BodyOwner::None;
          bodyOwnerName.clear();
          bodyBuffer.clear();
        } else {
          bodyBuffer.append(line);
        }
        continue;
      }

      // The first meaningful line must be exactly the `requirementDiagram` header
      // keyword (mermaid's RD token), optionally followed by whitespace and a
      // `#`/`%` comment. A bare trailing space, or any non-comment text after the
      // keyword, is a Parse error (only `requirementDiagram`,
      // `requirementDiagram # c`, `requirementDiagram % c` are valid).
      if (!headerSeen) {
        // Examine the RAW line (left-trimmed, trailing \r stripped) so a trailing
        // space or non-comment text after the keyword is detected — the loop's
        // full trim would hide it. Only `requirementDiagram`, or the keyword plus
        // whitespace and a `#`/`%` comment, is valid; trailing whitespace alone or
        // any non-comment text is a Parse error.
        QString raw = rawLine;
        while (!raw.isEmpty() && raw.at(0).isSpace()) raw.remove(0, 1);
        if (raw.endsWith(QLatin1Char('\r'))) raw.chop(1);
        const QString lower = raw.toLower();
        bool validHeader = false;
        if (lower.startsWith(QStringLiteral("requirementdiagram"))) {
          const QString suffix = lower.mid(18);
          if (suffix.isEmpty()) {
            validHeader = true;
          } else {
            int j = 0;
            while (j < suffix.size() && suffix.at(j).isSpace()) ++j;
            if (j < suffix.size() &&
                (suffix.at(j) == QLatin1Char('#') || suffix.at(j) == QLatin1Char('%')))
              validHeader = true;  // whitespace then a comment
            // else: whitespace-only, or non-comment text -> rejected
          }
        }
        if (validHeader) {
          headerSeen = true;
          continue;
        }
        throw RequirementParseError(
            QStringLiteral("Expected 'requirementDiagram' header, got: %1").arg(line), i + 1);
      }

      // direction TB|BT|RL|LR (may be prefixed with other text per the lexer
      // `.*direction\s+TB` rule, but the common form is bare).
      {
        static const QRegularExpression dirRe(
            QStringLiteral(R"(direction\s+(TB|BT|RL|LR))"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = dirRe.match(line);
        if (m.hasMatch() && m.capturedStart(0) >= 0) {
          data_.direction = m.captured(1).toUpper();
          continue;
        }
      }

      // accTitle: value
      // (There is intentionally NO `title:` handler — an inline `title` line is
      // rejected by the Requirement parser: mermaid's lexer returns a `title`
      // token but the grammar does not accept it, so it Parse-errors. Requirement
      // titles come only from frontmatter, handled by the preprocessor.)
      if (line.startsWith(QStringLiteral("accTitle"), Qt::CaseInsensitive)) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
          data_.accTitle = unwrapValue(line.mid(colon + 1));
          continue;
        }
      }
      // accDescr: value  OR  accDescr { multiline }
      if (line.startsWith(QStringLiteral("accDescr"), Qt::CaseInsensitive)) {
        const int brace = line.indexOf(QLatin1Char('{'));
        if (brace >= 0) {
          inMultilineAccDescr = true;
          // Any text after `{` on the same line (rare) is captured.
          const QString after = line.mid(brace + 1).trimmed();
          if (!after.isEmpty() && after != QLatin1String("}"))
            data_.accDescription = after;
          continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
          data_.accDescription = unwrapValue(line.mid(colon + 1));
          continue;
        }
      }

      // Requirement type keyword.
      {
        bool matched = false;
        for (auto it = typeKeywordMap().constBegin(); it != typeKeywordMap().constEnd(); ++it) {
          const QString& keyword = it.key();
          // Word-boundary match at line start (case-insensitive, like the lexer).
          if (line.size() >= keyword.size() &&
              line.left(keyword.size()).compare(keyword, Qt::CaseInsensitive) == 0 &&
              (line.size() == keyword.size() ||
               line.at(keyword.size()).isSpace() ||
               line.at(keyword.size()) == QLatin1Char('{'))) {
            const NameAndClass nc = parseNameTail(line.mid(keyword.size()), i + 1);
            if (nc.name.isEmpty())
              throw RequirementParseError(
                  QStringLiteral("requirement declaration requires a name"), i + 1);
            if (nc.body == BodyKind::None)
              throw RequirementParseError(
                  QStringLiteral("requirement '%1' requires a multi-line body { ... }").arg(nc.name),
                  i + 1);
            if (nc.body != BodyKind::MultiLine)
              throw RequirementParseError(
                  QStringLiteral(
                      "requirement declaration is malformed — the body opener `{` must "
                      "immediately follow the name / `:::` idList and be the last token on its line"),
                  i + 1);
            // Map semantics: one node per name; the FIRST definition wins. A
            // duplicate declaration's body is still validated but not stored.
            const bool duplicate = findRequirement(nc.name) != nullptr;
            if (!duplicate) {
              RequirementNode node;
              node.name = nc.name;
              node.type = it.value();
              // `:::` binds classes at declaration time (same path as `class`):
              // the class name is recorded and its currently-defined styles are
              // folded in. A class defined later still applies via defineClass's
              // retroactive update (it pushes to every node already bound).
              for (const QString& cid : nc.styleClasses) {
                node.cssClasses.append(cid);
                node.cssStyles.append(data_.classDefs.value(cid));
              }
              data_.requirements.append(std::move(node));
            }
            inBody = true;
            bodyOwner = duplicate ? BodyOwner::DiscardRequirement : BodyOwner::Requirement;
            bodyOwnerName = nc.name;
            bodyStartLine = i + 1;
            bodyBuffer.clear();
            matched = true;
            break;
          }
        }
        if (matched) continue;
      }

      // element keyword (7 chars: e-l-e-m-e-n-t).
      if (line.size() >= 7 &&
          line.left(7).compare(QStringLiteral("element"), Qt::CaseInsensitive) == 0 &&
          (line.size() == 7 || line.at(7).isSpace() || line.at(7) == QLatin1Char('{'))) {
        const NameAndClass nc = parseNameTail(line.mid(7), i + 1);
        if (nc.name.isEmpty())
          throw RequirementParseError(
              QStringLiteral("element declaration requires a name"), i + 1);
        if (nc.body == BodyKind::None)
          throw RequirementParseError(
              QStringLiteral("element '%1' requires a multi-line body { ... }").arg(nc.name), i + 1);
        if (nc.body != BodyKind::MultiLine)
          throw RequirementParseError(
              QStringLiteral(
                  "element declaration is malformed — the body opener `{` must "
                  "immediately follow the name / `:::` idList and be the last token on its line"),
              i + 1);
        const bool duplicate = findElement(nc.name) != nullptr;
        if (!duplicate) {
          ElementNode node;
          node.name = nc.name;
          for (const QString& cid : nc.styleClasses) {
            node.cssClasses.append(cid);
            node.cssStyles.append(data_.classDefs.value(cid));
          }
          data_.elements.append(std::move(node));
        }
        inBody = true;
        bodyOwner = duplicate ? BodyOwner::DiscardElement : BodyOwner::Element;
        bodyOwnerName = nc.name;
        bodyStartLine = i + 1;
        bodyBuffer.clear();
        continue;
      }

      // Relationship: `<name> <- <type> - <name>` or `<name> - <type> -> <name>`.
      {
        Relationship rel;
        if (parseRelationship(line, rel, i + 1)) {
          data_.relations.append(std::move(rel));
          continue;
        }
      }

      // classDef / class / style — tokenized under the style lexer condition
      // (the keyword switches the lexer to `style` state for the rest of the
      // line). Each is resolved into the node/event model at parse time:
      //   classDef <classList> <styles> — append styles per class + retroactive.
      //   class <nodeIdList> <classList> — bind nodes to classes (skip missing).
      //   style <nodeIdList> <styles>    — fold styles (return-abort on missing).
      // See applyClasses / applyStyles / defineClasses for the DB semantics.
      if (line.startsWith(QStringLiteral("classDef"), Qt::CaseInsensitive) &&
          (line.size() == 8 || line.at(8).isSpace())) {
        const QVector<StyleToken> toks = tokenizeStyleState(line.mid(8), i + 1);
        int pos = 0;
        const QStringList classIds = extractStyleIdList(toks, pos, i + 1);
        const QStringList styles = extractStylesOpt(toks, pos, i + 1);
        defineClasses(classIds, styles);
        continue;
      }
      if (line.startsWith(QStringLiteral("class"), Qt::CaseInsensitive) &&
          (line.size() == 5 || line.at(5).isSpace())) {
        const QVector<StyleToken> toks = tokenizeStyleState(line.mid(5), i + 1);
        int pos = 0;
        const QStringList nodeIds = extractStyleIdList(toks, pos, i + 1);
        const QStringList classIds = extractStyleIdList(toks, pos, i + 1);
        if (classIds.isEmpty())
          throw RequirementParseError(
              QStringLiteral("class requires at least one class name"), i + 1);
        applyClasses(nodeIds, classIds);
        continue;
      }
      if (line.startsWith(QStringLiteral("style"), Qt::CaseInsensitive) &&
          (line.size() == 5 || line.at(5).isSpace())) {
        const QVector<StyleToken> toks = tokenizeStyleState(line.mid(5), i + 1);
        int pos = 0;
        const QStringList nodeIds = extractStyleIdList(toks, pos, i + 1);
        const QStringList styles = extractStylesOpt(toks, pos, i + 1);
        applyStyles(nodeIds, styles);
        continue;
      }

      // Unrecognized line — mermaid returns a Parse error rather than silently
      // producing a (wrong) Ready scene. Report the 1-based source line.
      throw RequirementParseError(
          QStringLiteral("Unrecognized requirementDiagram syntax: %1").arg(line), i + 1);
    }
    // An open body at end-of-source is an unclosed block — also a parse error.
    if (inBody)
      throw RequirementParseError(
          QStringLiteral("Unclosed requirement/element body"), lines_.size());
    return std::move(data_);
  }

private:
  QString source_;
  QStringList lines_;
  RequirementDiagramData data_;

  // Validates + applies a requirement body. Mermaid errors on an unparseable
  // line, an unknown field, or an invalid risk/verifyMethod enum — we surface
  // those as RequirementParseError (with the 1-based source line).
  void applyRequirementBody(RequirementNode& node, const QStringList& body, int bodyStartLine) {
    for (int idx = 0; idx < body.size(); ++idx) {
      const QString& raw = body.at(idx);
      const int lineNo = bodyStartLine + 1 + idx;
      QString key, rawValue;
      if (!parseFieldLine(raw, key, rawValue))
        throw RequirementParseError(
            QStringLiteral("invalid requirement body line: %1").arg(raw), lineNo);
      const QString value = parseValue(rawValue, lineNo);
      if (key == QLatin1String("id")) node.requirementId = value;
      else if (key == QLatin1String("text")) node.text = value;
      else if (key == QLatin1String("risk")) {
        if (!isValidRisk(value))
          throw RequirementParseError(
              QStringLiteral("invalid risk '%1' (expected low/medium/high)").arg(value), lineNo);
        node.risk = RequirementEnumDisplay::risk(value);
      } else if (key == QLatin1String("verifymethod")) {
        if (!isValidVerifyMethod(value))
          throw RequirementParseError(
              QStringLiteral("invalid verifyMethod '%1' "
                             "(expected analysis/demonstration/inspection/test)").arg(value),
              lineNo);
        node.verifyMethod = RequirementEnumDisplay::verifyMethod(value);
      } else {
        throw RequirementParseError(
            QStringLiteral("unknown requirement body field '%1'").arg(key), lineNo);
      }
    }
  }

  void applyElementBody(ElementNode& node, const QStringList& body, int bodyStartLine) {
    for (int idx = 0; idx < body.size(); ++idx) {
      const QString& raw = body.at(idx);
      const int lineNo = bodyStartLine + 1 + idx;
      QString key, rawValue;
      if (!parseFieldLine(raw, key, rawValue))
        throw RequirementParseError(
            QStringLiteral("invalid element body line: %1").arg(raw), lineNo);
      const QString value = parseValue(rawValue, lineNo);
      if (key == QLatin1String("type")) node.type = value;
      else if (key == QLatin1String("docref")) node.docRef = value;
      else
        throw RequirementParseError(
            QStringLiteral("unknown element body field '%1'").arg(key), lineNo);
    }
  }

  // defineClass (RequirementDB.defineClass): for each named class, APPEND the
  // styles to that class's accumulated declarations, then RETROACTIVELY push
  // the same styles onto every node already bound to the class (so a classDef
  // after a `class`/`:::` binding still applies). `styles` is the stylesOpt
  // component list (already comma-split by the style tokenizer).
  void defineClasses(const QStringList& classIds, const QStringList& styles) {
    for (const QString& cid : classIds) {
      data_.classDefs[cid].append(styles);
      for (RequirementNode& n : data_.requirements)
        if (n.cssClasses.contains(cid)) n.cssStyles.append(styles);
      for (ElementNode& n : data_.elements)
        if (n.cssClasses.contains(cid)) n.cssStyles.append(styles);
    }
  }

  // setClass (RequirementDB.setClass): for each node id, if the node exists,
  // record each class name on it and fold in that class's CURRENT accumulated
  // declarations. A missing node is SKIPPED (upstream `if (node) {…}`), so the
  // remaining ids are still processed.
  void applyClasses(const QStringList& nodeIds, const QStringList& classIds) {
    for (const QString& nid : nodeIds) {
      RequirementNode* rn = findRequirement(nid);
      ElementNode* en = rn ? nullptr : findElement(nid);
      if (!rn && !en) continue;  // missing node — skip (setClass keeps going)
      for (const QString& cid : classIds) {
        const QStringList decls = data_.classDefs.value(cid);
        if (rn) {
          rn->cssClasses.append(cid);
          rn->cssStyles.append(decls);
        } else {
          en->cssClasses.append(cid);
          en->cssStyles.append(decls);
        }
      }
    }
  }

  // setCssStyle (RequirementDB.setCssStyle): for each node id in order, look it
  // up; if it does not exist, RETURN immediately — aborting the WHOLE id list
  // (upstream `if (!styles || !node) return`, evaluated per id). Nodes processed
  // before the first missing id are still styled.
  void applyStyles(const QStringList& nodeIds, const QStringList& styles) {
    for (const QString& nid : nodeIds) {
      RequirementNode* rn = findRequirement(nid);
      ElementNode* en = rn ? nullptr : findElement(nid);
      if (!rn && !en) return;  // missing node — abort the whole list
      if (rn) rn->cssStyles.append(styles);
      else en->cssStyles.append(styles);
    }
  }

  RequirementNode* findRequirement(const QString& name) {
    for (RequirementNode& n : data_.requirements)
      if (n.name == name) return &n;
    return nullptr;
  }

  ElementNode* findElement(const QString& name) {
    for (ElementNode& n : data_.elements)
      if (n.name == name) return &n;
    return nullptr;
  }

  // Matches `src <- type - dst` (← form, src=right) or `src - type -> dst`
  // (→ form, src=left). Returns true and fills `rel` on match. Each endpoint is
  // validated through the unified token API (consumeToken must fully consume it),
  // so an endpoint like `A-B` or `.x` is rejected like any other token.
  bool parseRelationship(const QString& line, Relationship& rel, int lineNo) const {
    static const QRegularExpression re(
        QStringLiteral(
            R"((.+?)\s*(<-|->|-)\s*(contains|copies|derives|satisfies|verifies|refines|traces)\s*(<-|->|-)\s*(.+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return false;
    const QString connectorBefore = m.captured(2);
    const QString type = m.captured(3).toLower();
    const QString connectorAfter = m.captured(4);
    // Endpoints: one token each, fully consumed + non-empty (decode quoted,
    // validate unquoted). An empty quoted endpoint is rejected like any other.
    const QString leftRaw = m.captured(1).trimmed();
    const Token leftTok = consumeToken(leftRaw, 0, lineNo);
    if (leftTok.end != leftRaw.size() || leftTok.value.isEmpty()) return false;
    const QString rightRaw = m.captured(5).trimmed();
    const Token rightTok = consumeToken(rightRaw, 0, lineNo);
    if (rightTok.end != rightRaw.size() || rightTok.value.isEmpty()) return false;
    const QString left = leftTok.value;
    const QString right = rightTok.value;
    // Validate the connector pairing: only `<- ... -` and `- ... ->` are legal.
    const bool formArrowRight =
        connectorBefore == QLatin1String("-") && connectorAfter == QLatin1String("->");
    const bool formArrowLeft =
        connectorBefore == QLatin1String("<-") && connectorAfter == QLatin1String("-");
    if (!formArrowRight && !formArrowLeft) return false;
    if (!relationshipKeywords().contains(type)) return false;
    rel.type = type;
    if (formArrowRight) {
      rel.src = left;
      rel.dst = right;
    } else {
      // `<- type -` means the arrow points left, so the right id is the source.
      rel.src = right;
      rel.dst = left;
    }
    return true;
  }
};

}  // namespace

RequirementDiagram RequirementDiagram::parse(const QString& source) {
  Parser parser(source);
  RequirementDiagram diagram;
  diagram.data_ = parser.parse();
  return diagram;
}

}  // namespace muffin::mermaid::requirement
