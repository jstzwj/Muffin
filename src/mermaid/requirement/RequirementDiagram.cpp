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

// Strips a `#` or `%` line comment, honoring double-quoted strings (a `#` or `%`
// inside quotes is literal). Mermaid's lexer treats both as line comments.
QString stripComment(const QString& line) {
  bool inQuote = false;
  for (qsizetype i = 0; i < line.size(); ++i) {
    const QChar c = line.at(i);
    if (c == QLatin1Char('"')) inQuote = !inQuote;
    else if (!inQuote && (c == QLatin1Char('#') || c == QLatin1Char('%')))
      return line.left(i);
  }
  return line;
}

// Strips surrounding double-quotes from a bareword/quoted value and trims.
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

// Forward declaration: parseIdList is defined with the other token helpers below.
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
  QString rest;
  if (tail.startsWith(QLatin1Char('"'))) {
    const int end = tail.indexOf(QLatin1Char('"'), 1);
    if (end > 0) {
      result.name = tail.mid(1, end - 1);
      rest = tail.mid(end + 1);
    } else {
      result.name = tail.mid(1);  // unterminated quote
      rest.clear();
    }
  } else {
    // Unquoted name: up to the first `:::` or `{` (NOT whitespace — multi-word
    // names are valid). Mermaid trims.
    int nameEnd = tail.size();
    const int brace = tail.indexOf(QLatin1Char('{'));
    const int sep = tail.indexOf(QStringLiteral(":::"));
    if (sep >= 0) nameEnd = std::min(nameEnd, sep);
    if (brace >= 0) nameEnd = std::min(nameEnd, brace);
    result.name = tail.left(nameEnd).trimmed();
    rest = tail.mid(nameEnd);
  }
  rest = rest.trimmed();
  // `::: <idList>` up to `{` — parseIdList rejects empty/malformed lists.
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

// unqString may not contain any of these structural/special chars (verified vs
// mermaid 11.16.0: `- = < > { } : ,` are rejected; letters/digits/space/. _ / %
// # ; etc. are accepted). Only a quoted qString may contain them.
bool isUnqStringSpecial(QChar c) {
  return c == QLatin1Char('-') || c == QLatin1Char('=') || c == QLatin1Char('<') ||
         c == QLatin1Char('>') || c == QLatin1Char('{') || c == QLatin1Char('}') ||
         c == QLatin1Char(':') || c == QLatin1Char(',');
}

// Parses a single string token: either a quoted qString ("...") that must span
// the WHOLE raw text (a closing quote with anything after — `"a" junk`,
// `"a" "b"` — is malformed), or an unquoted bareword.
struct ParsedString {
  QString value;
  bool quoted = false;
};
ParsedString parseStringToken(const QString& raw, int lineNo) {
  ParsedString result;
  if (!raw.startsWith(QLatin1Char('"'))) {
    result.value = raw;
    return result;
  }
  result.quoted = true;
  const int close = raw.indexOf(QLatin1Char('"'), 1);
  if (close < 0)
    throw RequirementParseError(QStringLiteral("unterminated quoted string"), lineNo);
  if (close != raw.size() - 1)
    throw RequirementParseError(
        QStringLiteral("unexpected text after quoted string"), lineNo);
  result.value = raw.mid(1, close - 1);
  return result;
}

// Validates + unwraps a body field value. Non-empty; unquoted values may not
// contain a special char (quote them instead). Multi-word unquoted values
// (`text: hello world`) and `%`/`#` (`text: 50% complete`) stay valid.
QString parseValue(const QString& raw, int lineNo) {
  const ParsedString token = parseStringToken(raw, lineNo);
  if (token.value.isEmpty())
    throw RequirementParseError(QStringLiteral("empty field value"), lineNo);
  if (!token.quoted) {
    for (const QChar c : token.value)
      if (isUnqStringSpecial(c))
        throw RequirementParseError(
            QStringLiteral("unquoted value '%1' contains a special char — quote it").arg(token.value),
            lineNo);
  }
  return token.value;
}

// Parses a `::: <idList>`: comma-separated, whitespace-tolerant class tokens.
// Empty (no classes), leading/trailing comma, and double comma are all rejected
// (mermaid's STR list does not allow empty tokens).
QStringList parseIdList(const QString& raw, int lineNo) {
  if (raw.trimmed().isEmpty())
    throw RequirementParseError(QStringLiteral("empty class list after ':::'"), lineNo);
  QStringList result;
  for (const QString& token : raw.split(QLatin1Char(','))) {
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty())
      throw RequirementParseError(
          QStringLiteral("malformed class list (empty/leading/trailing/double comma)"), lineNo);
    result.append(trimmed);
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
      QString line;
      if (inBody) {
        // Inside a body, `#`/`%` are part of values (NOT comments) — do not
        // strip mid-line. A whole comment line (first char `#`/`%`) is skipped.
        line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('%')))
          continue;
      } else {
        // Top-level: strip a trailing `#`/`%` comment (a token-boundary comment),
        // then trim. (Body value lines preserve `#`/`%` — see parseValue.)
        line = stripComment(rawLine).trimmed();
      }
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

      // The first meaningful line must be the `requirementDiagram` header
      // (exactly — mermaid's grammar requires the RD token; `requirement` alone
      // or any other opener is a Parse error).
      if (!headerSeen) {
        if (line.toLower() == QStringLiteral("requirementdiagram")) {
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

      // title (rest-of-line).
      if (line.startsWith(QStringLiteral("title"), Qt::CaseInsensitive) &&
          (line.size() == 5 || line.at(5).isSpace())) {
        data_.title = unwrapValue(line.mid(5));
        continue;
      }
      // accTitle: value
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
              node.cssClasses.append(nc.styleClasses);
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
          node.cssClasses.append(nc.styleClasses);
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
        if (parseRelationship(line, rel)) {
          data_.relations.append(std::move(rel));
          continue;
        }
      }

      // classDef / class / style — parsed minimally (stored, not fully applied).
      if (line.startsWith(QStringLiteral("classDef"), Qt::CaseInsensitive)) {
        const QString rest = line.mid(8).trimmed();
        const int space = rest.indexOf(QLatin1Char(' '));
        if (space > 0) {
          const QString id = rest.left(space).trimmed();
          const QString style = rest.mid(space + 1).trimmed();
          data_.classDefs[id] = style.split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
        continue;
      }
      if (line.startsWith(QStringLiteral("class"), Qt::CaseInsensitive) &&
          (line.size() == 5 || line.at(5).isSpace())) {
        // class <node> <classname> — record the class on the node.
        const QStringList parts = line.mid(5).trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() >= 2) {
          const QString nodeId = parts.at(0);
          const QString className = parts.at(1);
          applyClassToNode(nodeId, className);
        }
        continue;
      }
      if (line.startsWith(QStringLiteral("style"), Qt::CaseInsensitive) &&
          (line.size() == 5 || line.at(5).isSpace())) {
        // style <node> <css> — fold declarations into the node's cssStyles.
        const QString rest = line.mid(5).trimmed();
        const int space = rest.indexOf(QLatin1Char(' '));
        if (space > 0) {
          const QString nodeId = rest.left(space).trimmed();
          const QString css = rest.mid(space + 1).trimmed();
          applyStyleToNode(nodeId, css);
        }
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

  void applyClassToNode(const QString& nodeId, const QString& className) {
    for (RequirementNode& n : data_.requirements)
      if (n.name == nodeId) n.cssClasses.append(className);
    for (ElementNode& n : data_.elements)
      if (n.name == nodeId) n.cssClasses.append(className);
  }

  void applyStyleToNode(const QString& nodeId, const QString& css) {
    const QStringList decls = css.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (RequirementNode& n : data_.requirements)
      if (n.name == nodeId) n.cssStyles.append(decls);
    for (ElementNode& n : data_.elements)
      if (n.name == nodeId) n.cssStyles.append(decls);
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
  // (→ form, src=left). Returns true and fills `rel` on match.
  bool parseRelationship(const QString& line, Relationship& rel) const {
    static const QRegularExpression re(
        QStringLiteral(
            R"((.+?)\s*(<-|->|-)\s*(contains|copies|derives|satisfies|verifies|refines|traces)\s*(<-|->|-)\s*(.+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return false;
    const QString left = unwrapValue(m.captured(1));
    const QString connectorBefore = m.captured(2);
    const QString type = m.captured(3).toLower();
    const QString connectorAfter = m.captured(4);
    const QString right = unwrapValue(m.captured(5));
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
