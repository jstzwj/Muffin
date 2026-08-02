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

// Returns the (name, styleClass) extracted from a `<type> <name> [::: <class>] [{]`
// declaration tail. `tail` is the text after the type keyword. Trailing `{` is
// stripped. `styleClassOut` is left empty if no `:::` separator is present.
struct NameAndClass {
  QString name;
  QString styleClass;
  bool opensBody = false;  // a `{` was present on this line
};

NameAndClass parseNameTail(QString tail) {
  NameAndClass result;
  // Detect inline body opener `{`.
  const int brace = tail.indexOf(QLatin1Char('{'));
  if (brace >= 0) {
    result.opensBody = true;
    tail = tail.left(brace) + tail.mid(brace + 1);
  }
  tail = tail.trimmed();
  // `:::` style separator.
  const int sep = tail.indexOf(QStringLiteral(":::"));
  if (sep >= 0) {
    result.name = tail.left(sep).trimmed();
    result.styleClass = tail.mid(sep + 3).trimmed();
  } else {
    result.name = tail;
  }
  return result;
}

// Parses `field: value` from a body line. Returns true if the line has a
// recognized `key:` prefix; the value is the remainder (unwrapped).
bool parseFieldLine(const QString& line, QString& keyOut, QString& valueOut) {
  const int colon = line.indexOf(QLatin1Char(':'));
  if (colon <= 0) return false;
  keyOut = line.left(colon).trimmed().toLower();
  valueOut = unwrapValue(line.mid(colon + 1));
  return true;
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
    enum class BodyOwner { None, Requirement, Element };
    BodyOwner bodyOwner = BodyOwner::None;
    QString bodyOwnerName;
    QStringList bodyBuffer;

    for (qsizetype i = 0; i < lines_.size(); ++i) {
      QString line = stripComment(lines_.at(i));
      // In multiline accDescr, raw text (minus the closing brace) is captured.
      if (inMultilineAccDescr) {
        if (line.trimmed() == QLatin1String("}")) {
          inMultilineAccDescr = false;
        } else {
          if (!data_.accDescription.isEmpty()) data_.accDescription += QLatin1Char('\n');
          data_.accDescription += line.trimmed();
        }
        continue;
      }
      line = line.trimmed();
      if (line.isEmpty()) continue;

      if (inBody) {
        if (line == QLatin1String("}")) {
          // Finalize body — look up the owner by name so the reference is stable
          // even if the requirements/elements vectors reallocated mid-body.
          if (bodyOwner == BodyOwner::Requirement) {
            RequirementNode* target = findRequirement(bodyOwnerName);
            if (target) applyRequirementBody(*target, bodyBuffer);
          } else if (bodyOwner == BodyOwner::Element) {
            ElementNode* target = findElement(bodyOwnerName);
            if (target) applyElementBody(*target, bodyBuffer);
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

      // Strip a trailing inline body opener `{` if present on dispatch lines.
      // Header detection (case-insensitive prefix).
      if (!headerSeen) {
        const QString lower = line.toLower();
        if (lower.startsWith(QStringLiteral("requirementdiagram"))) {
          headerSeen = true;
          continue;
        }
        // Be lenient: if the first meaningful line is not the header, still
        // proceed (the detector already confirmed the family).
        headerSeen = true;
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
            const NameAndClass nc = parseNameTail(line.mid(keyword.size()));
            RequirementNode node;
            node.name = nc.name;
            node.type = it.value();
            if (!nc.styleClass.isEmpty()) node.cssClasses.append(nc.styleClass);
            data_.requirements.append(std::move(node));
            if (nc.opensBody) {
              inBody = true;
              bodyOwner = BodyOwner::Requirement;
              bodyOwnerName = nc.name;
              bodyBuffer.clear();
            }
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
        const NameAndClass nc = parseNameTail(line.mid(7));
        ElementNode node;
        node.name = nc.name;
        if (!nc.styleClass.isEmpty()) node.cssClasses.append(nc.styleClass);
        data_.elements.append(std::move(node));
        if (nc.opensBody) {
          inBody = true;
          bodyOwner = BodyOwner::Element;
          bodyOwnerName = nc.name;
          bodyBuffer.clear();
        }
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

      // Unrecognized line — skip leniently (the upstream parser would error,
      // but for the pilot we tolerate stray content).
    }
    return std::move(data_);
  }

private:
  QString source_;
  QStringList lines_;
  RequirementDiagramData data_;

  void applyRequirementBody(RequirementNode& node, const QStringList& body) {
    for (const QString& raw : body) {
      QString key, value;
      if (!parseFieldLine(raw, key, value)) continue;
      if (key == QLatin1String("id")) node.requirementId = value;
      else if (key == QLatin1String("text")) node.text = value;
      else if (key == QLatin1String("risk")) node.risk = RequirementEnumDisplay::risk(value);
      else if (key == QLatin1String("verifymethod"))
        node.verifyMethod = RequirementEnumDisplay::verifyMethod(value);
    }
  }

  void applyElementBody(ElementNode& node, const QStringList& body) {
    for (const QString& raw : body) {
      QString key, value;
      if (!parseFieldLine(raw, key, value)) continue;
      if (key == QLatin1String("type")) node.type = value;
      else if (key == QLatin1String("docref")) node.docRef = value;
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
    const QString left = m.captured(1).trimmed();
    const QString connectorBefore = m.captured(2);
    const QString type = m.captured(3).toLower();
    const QString connectorAfter = m.captured(4);
    const QString right = m.captured(5).trimmed();
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
