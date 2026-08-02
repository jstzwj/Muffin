// requirementDiagram parser unit tests. Parser-only (no Qt GUI), so
// QCoreApplication suffices. Asserts the parsed RequirementDiagramData directly:
// quoted-identifier decoding, `::: class` + body, Map dedup (first definition
// wins), body fields + the 6 requirement types / element / 7 relationship types,
// and that invalid syntax throws RequirementParseError (matching mermaid 11.16.0,
// which requires a multi-line body and errors on no-body/single-line/unknown
// field/invalid enum/missing header) instead of silently producing a Ready scene.
#include "mermaid/requirement/RequirementDiagram.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid::requirement;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

RequirementDiagramData parse(const QString& source) {
  return RequirementDiagram::parse(source).data();
}

// Returns true if parsing throws RequirementParseError.
template <typename Fn>
bool throwsParseError(Fn fn) {
  try {
    fn();
    return false;
  } catch (const RequirementParseError&) {
    return true;
  }
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // 1. Quoted identifier decoded (name + relationship endpoints + body fields).
  {
    const auto d = parse(
        "requirementDiagram\n"
        "requirement \"My Requirement\" {\n id: 1\n text: hello world\n}\n"
        "element \"My Element\" {\n type: Hardware\n}\n"
        "\"My Requirement\" -contains-> \"My Element\"");
    require(d.requirements.size() == 1, QStringLiteral("one requirement"));
    require(d.requirements[0].name == QStringLiteral("My Requirement"),
            QStringLiteral("quoted requirement name decoded, got '%1'").arg(d.requirements[0].name));
    require(d.requirements[0].requirementId == QStringLiteral("1"), QStringLiteral("body id field"));
    require(d.requirements[0].text == QStringLiteral("hello world"), QStringLiteral("body text field"));
    require(d.elements.size() == 1, QStringLiteral("one element"));
    require(d.elements[0].name == QStringLiteral("My Element"),
            QStringLiteral("quoted element name decoded"));
    require(d.elements[0].type == QStringLiteral("Hardware"), QStringLiteral("element type field"));
    require(d.relations.size() == 1, QStringLiteral("one relationship"));
    require(d.relations[0].src == QStringLiteral("My Requirement") &&
                d.relations[0].dst == QStringLiteral("My Element"),
            QStringLiteral("quoted relationship endpoints decoded"));
  }

  // 2. `::: <idList>` (comma-separated classes) + unquoted multi-word name.
  {
    const auto d = parse(
        "requirementDiagram\n"
        "requirement My Req ::: red,blue {\n id: 1\n}\n"
        "element My Elem ::: green, yellow {\n type: t\n}\n"
        "My Req -contains-> My Elem");
    require(d.requirements.at(0).name == QStringLiteral("My Req"),
            QStringLiteral("unquoted multi-word name preserved, got '%1'").arg(d.requirements.at(0).name));
    require(d.requirements.at(0).cssClasses.contains(QStringLiteral("red")) &&
                d.requirements.at(0).cssClasses.contains(QStringLiteral("blue")),
            QStringLiteral("::: idList red,blue -> two classes"));
    require(d.elements.at(0).name == QStringLiteral("My Elem"),
            QStringLiteral("unquoted multi-word element name"));
    require(d.elements.at(0).cssClasses.contains(QStringLiteral("green")) &&
                d.elements.at(0).cssClasses.contains(QStringLiteral("yellow")),
            QStringLiteral("::: idList green, yellow (whitespace-tolerant) -> two classes"));
    require(d.relations.size() == 1, QStringLiteral("relationship between multi-word nodes"));
    require(d.relations.at(0).src == QStringLiteral("My Req") &&
                d.relations.at(0).dst == QStringLiteral("My Elem"),
            QStringLiteral("multi-word relationship endpoints"));
  }

  // 3. Map dedup: a repeated name yields ONE node and the FIRST definition wins.
  {
    const auto d = parse(
        "requirementDiagram\n"
        "requirement Dup {\n id: first\n}\n"
        "requirement Dup {\n id: second\n}\n"
        "element E {\n type: a\n}\n"
        "element E {\n type: b\n}");
    require(d.requirements.size() == 1, QStringLiteral("dedup: one requirement node"));
    require(d.requirements[0].name == QStringLiteral("Dup"), QStringLiteral("dedup name"));
    require(d.requirements[0].requirementId == QStringLiteral("first"),
            QStringLiteral("dedup keeps the FIRST definition's body, got '%1'")
                .arg(d.requirements[0].requirementId));
    require(d.elements.size() == 1, QStringLiteral("dedup: one element node"));
    require(d.elements[0].type == QStringLiteral("a"),
            QStringLiteral("dedup keeps the FIRST element definition"));
  }

  // 4. All 6 requirement types + 7 relationship types + enum fields.
  {
    const auto d = parse(
        "requirementDiagram\n"
        "requirement R1 {\n risk: low\n verifyMethod: analysis\n}\n"
        "functionalRequirement R2 {\n risk: medium\n verifyMethod: demonstration\n}\n"
        "interfaceRequirement R3 {\n risk: high\n verifyMethod: inspection\n}\n"
        "performanceRequirement R4 {\n verifyMethod: test\n}\n"
        "physicalRequirement R5 {\n text: p\n}\n"
        "designConstraint R6 {\n text: d\n}\n"
        "R1 -contains-> R2\nR1 -copies-> R2\nR1 -derives-> R2\n"
        "R1 -satisfies-> R2\nR1 -verifies-> R2\nR1 -refines-> R2\nR1 -traces-> R2\n"
        "R2 <-contains- R1");
    require(d.requirements.size() == 6, QStringLiteral("six requirement types"));
    require(d.requirements[0].type == QStringLiteral("Requirement"), QStringLiteral("type: Requirement"));
    require(d.requirements[1].type == QStringLiteral("Functional Requirement"), QStringLiteral("type: Functional"));
    require(d.requirements[2].type == QStringLiteral("Interface Requirement"), QStringLiteral("type: Interface"));
    require(d.requirements[3].type == QStringLiteral("Performance Requirement"), QStringLiteral("type: Performance"));
    require(d.requirements[4].type == QStringLiteral("Physical Requirement"), QStringLiteral("type: Physical"));
    require(d.requirements[5].type == QStringLiteral("Design Constraint"), QStringLiteral("type: Design"));
    require(d.requirements[0].risk == QStringLiteral("Low"), QStringLiteral("risk Low"));
    require(d.requirements[1].risk == QStringLiteral("Medium"), QStringLiteral("risk Medium"));
    require(d.requirements[2].risk == QStringLiteral("High"), QStringLiteral("risk High"));
    require(d.requirements[0].verifyMethod == QStringLiteral("Analysis"), QStringLiteral("verify Analysis"));
    require(d.requirements[3].verifyMethod == QStringLiteral("Test"), QStringLiteral("verify Test"));
    require(d.relations.size() == 8, QStringLiteral("eight relationships (7 right + 1 left form)"));
    const auto& left = d.relations.last();
    require(left.type == QStringLiteral("contains") && left.src == QStringLiteral("R1") &&
                left.dst == QStringLiteral("R2"),
            QStringLiteral("left-form relationship src/dst"));
  }

  // 5. Strict syntax errors throw RequirementParseError (no silent Ready). All
  //    verified against real mermaid 11.16.0 (Parse error).
  {
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X");  // no body
    }), QStringLiteral("no-body declaration throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X { id: 1 }");  // single-line body
    }), QStringLiteral("single-line body throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n foo: bar\n}");  // unknown field
    }), QStringLiteral("unknown body field throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n risk: critical\n}");  // bad enum
    }), QStringLiteral("invalid risk enum throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirement X {\n id: 1\n}");  // missing header
    }), QStringLiteral("missing header throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirement\nrequirement X {\n id: 1\n}");  // wrong header
    }), QStringLiteral("wrong header (requirement, not requirementDiagram) throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id: 1");  // unclosed body
    }), QStringLiteral("unclosed body throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id: 1\n}\nfoo bar baz");  // stray line
    }), QStringLiteral("unrecognized line throws"));
    // Duplicate with an invalid body must still throw (validation runs even when
    // the first definition wins).
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id: 1\n}\nrequirement X {\n risk: nope\n}");
    }), QStringLiteral("duplicate with invalid body throws"));
    // Empty / unquoted-special field values throw (mermaid's qString rule).
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id:\n}");
    }), QStringLiteral("empty field value throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id: a:b\n}");
    }), QStringLiteral("unquoted value with ':' throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n id: a,b\n}");
    }), QStringLiteral("unquoted value with ',' throws"));
    // Unquoted special chars -, =, <, > rejected (only quoted qString allows them).
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: a-b\n}");
    }), QStringLiteral("unquoted '-' throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: a=b\n}");
    }), QStringLiteral("unquoted '=' throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: a<b\n}");
    }), QStringLiteral("unquoted '<' throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: a>b\n}");
    }), QStringLiteral("unquoted '>' throws"));
    // Malformed qString (trailing/concatenated) rejected.
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: \"a\" junk\n}");
    }), QStringLiteral("malformed qString (trailing junk) throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X {\n text: \"a\" \"b\"\n}");
    }), QStringLiteral("malformed qString (concatenated) throws"));
    // Malformed idList (empty / leading / trailing / double comma) rejected.
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X ::: ,red {\n id: 1\n}");
    }), QStringLiteral("idList leading comma throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X ::: red, {\n id: 1\n}");
    }), QStringLiteral("idList trailing comma throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X ::: red,,blue {\n id: 1\n}");
    }), QStringLiteral("idList double comma throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X ::: {\n id: 1\n}");
    }), QStringLiteral("empty idList throws"));
    // Junk between the name and the body opener rejected.
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement \"X\" junk {\n id: 1\n}");
    }), QStringLiteral("junk before body opener throws"));
    // Declaration opener / name validation.
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement X { junk\n}");
    }), QStringLiteral("content after `{` on the opener line throws"));
    require(throwsParseError([] {
      RequirementDiagram::parse("requirementDiagram\nrequirement {\n id: 1\n}");
    }), QStringLiteral("empty declaration name throws"));
    // Quoted values with ':' / ',' are valid (qString allows them).
    bool quotedThrew = false;
    try {
      parse("requirementDiagram\nrequirement X {\n id: \"a:b,c\"\n}");
    } catch (const RequirementParseError&) { quotedThrew = true; }
    require(!quotedThrew, QStringLiteral("quoted value with ':' / ',' is valid"));
    // Valid source must NOT throw.
    bool validThrew = false;
    try {
      parse("requirementDiagram\nrequirement X {\n id: 1\n}");
    } catch (const RequirementParseError&) { validThrew = true; }
    require(!validThrew, QStringLiteral("valid source must not throw"));
  }

  // 6. Token contract positives: %/# preserved in body values (assert the EXACT
  //    parsed value, not just non-throw), and quoted special chars preserved.
  {
    const auto d = parse(
        "requirementDiagram\n"
        "requirement X {\n text: 50% complete\n}\n"
        "requirement Y {\n text: abc # def\n}\n"
        "requirement Z {\n text: \"a:b,c-d=e\"\n}");
    require(d.requirements.at(0).text == QStringLiteral("50% complete"),
            QStringLiteral("percent preserved in value, got '%1'").arg(d.requirements.at(0).text));
    require(d.requirements.at(1).text == QStringLiteral("abc # def"),
            QStringLiteral("hash preserved in value, got '%1'").arg(d.requirements.at(1).text));
    require(d.requirements.at(2).text == QStringLiteral("a:b,c-d=e"),
            QStringLiteral("quoted special chars preserved, got '%1'").arg(d.requirements.at(2).text));
  }

  // 7. Data-driven token-contract table (valid / invalid / decoded value) — a
  //    concentrated acceptance fixture for the unified lexer, covering names,
  //    body values, relationship endpoints, idList, comment handling, header
  //    suffix, ASCII-\w, and qString-space preservation. All entries verified
  //    against real mermaid 11.16.0.
  struct TokenCase {
    const char* id;
    const char* src;
    bool ok = true;
    const char* expectName = nullptr;
    const char* expectText = nullptr;
    const char* expectClasses = nullptr;  // comma-separated expected user classes
    const char* expectRelSrc = nullptr;   // expected first relationship src
    const char* expectRelDst = nullptr;   // expected first relationship dst
  };
  const TokenCase tokenCases[] = {
      // Names: ASCII-\w first char; } # % allowed; . / ; - = : and Unicode rejected.
      {.id = "name-hash", .src = "requirementDiagram\nrequirement X#Y {\n id: 1\n}", .expectName = "X#Y"},
      {.id = "name-brace", .src = "requirementDiagram\nrequirement X}Y {\n id: 1\n}", .expectName = "X}Y"},
      {.id = "name-dot", .src = "requirementDiagram\nrequirement .abc {\n id: 1\n}", .ok = false},
      {.id = "name-dash", .src = "requirementDiagram\nrequirement A-B {\n id: 1\n}", .ok = false},
      {.id = "name-colon", .src = "requirementDiagram\nrequirement A:B {\n id: 1\n}", .ok = false},
      {.id = "name-unicode", .src = "requirementDiagram\nrequirement 中文 {\n id: 1\n}", .ok = false},
      // Quoted name keeps its exact content (no trim) so the id matches endpoints.
      {.id = "quoted-name-spaces", .src = "requirementDiagram\nrequirement \" X \" {\n id: 1\n}", .expectName = " X "},
      // Values: %/#/} allowed; special chars rejected unquoted, quoted OK.
      {.id = "value-percent", .src = "requirementDiagram\nrequirement X {\n text: 50% complete\n}", .expectName = "X", .expectText = "50% complete"},
      {.id = "value-hash", .src = "requirementDiagram\nrequirement X {\n text: abc # def\n}", .expectName = "X", .expectText = "abc # def"},
      {.id = "value-brace", .src = "requirementDiagram\nrequirement X {\n text: a}b\n}", .expectName = "X", .expectText = "a}b"},
      {.id = "value-dash", .src = "requirementDiagram\nrequirement X {\n text: a-b\n}", .ok = false},
      {.id = "value-equals", .src = "requirementDiagram\nrequirement X {\n text: a=b\n}", .ok = false},
      {.id = "value-quoted-special", .src = "requirementDiagram\nrequirement X {\n text: \"a:b,c-d=e\"\n}", .expectName = "X", .expectText = "a:b,c-d=e"},
      {.id = "value-qstring-junk", .src = "requirementDiagram\nrequirement X {\n text: \"a\" junk\n}", .ok = false},
      // Relationship endpoint validated + decoded via the same token API.
      {.id = "endpoint-hash", .src = "requirementDiagram\nrequirement A#B {\n id: 1\n}\nrequirement C {\n id: 2\n}\nA#B -contains-> C", .expectName = "A#B", .expectRelSrc = "A#B", .expectRelDst = "C"},
      {.id = "endpoint-dash", .src = "requirementDiagram\nrequirement A {\n id: 1\n}\nrequirement C {\n id: 2\n}\nA-B -contains-> C", .ok = false},
      // idList: comma outside quotes; quoted comma is one class; empty/malformed rejected.
      {.id = "idlist-quoted-comma", .src = "requirementDiagram\nrequirement X ::: \"red,blue\" {\n id: 1\n}", .expectName = "X", .expectClasses = "red,blue"},
      {.id = "idlist-mixed", .src = "requirementDiagram\nrequirement X ::: red,\"a,b\" {\n id: 1\n}", .expectName = "X", .expectClasses = "red;a,b"},
      {.id = "idlist-empty", .src = "requirementDiagram\nrequirement X ::: {\n id: 1\n}", .ok = false},
      {.id = "idlist-double-comma", .src = "requirementDiagram\nrequirement X ::: red,,blue {\n id: 1\n}", .ok = false},
      {.id = "idlist-empty-qstring", .src = "requirementDiagram\nrequirement X ::: \"\" {\n id: 1\n}", .ok = false},
      // Header: exactly the keyword, or keyword + whitespace + #/% comment. A bare
      // trailing space or non-comment text is a Parse error.
      {.id = "header-exact", .src = "requirementDiagram\nrequirement X {\n id: 1\n}", .expectName = "X"},
      {.id = "header-hash-comment", .src = "requirementDiagram # c\nrequirement X {\n id: 1\n}", .expectName = "X"},
      {.id = "header-percent-comment", .src = "requirementDiagram % c\nrequirement X {\n id: 1\n}", .expectName = "X"},
      {.id = "header-trailing-space", .src = "requirementDiagram \nrequirement X {\n id: 1\n}", .ok = false},
      {.id = "header-junk", .src = "requirementDiagram junk\nrequirement X {\n id: 1\n}", .ok = false},
      // Comments: whole-line only; # inside a name/value is literal.
      {.id = "name-internal-hash", .src = "requirementDiagram\nrequirement X # c {\n id: 1\n}", .expectName = "X # c"},
      {.id = "body-comment-line", .src = "requirementDiagram\nrequirement X {\n id: 1\n # a body comment\n text: v\n}", .expectName = "X", .expectText = "v"},
  };
  for (const TokenCase& tc : tokenCases) {
    bool threw = false;
    RequirementDiagramData d;
    try {
      d = RequirementDiagram::parse(QString::fromUtf8(tc.src)).data();
    } catch (const RequirementParseError&) {
      threw = true;
    }
    require(threw == !tc.ok,
            QStringLiteral("%1: expected %2").arg(QLatin1String(tc.id), tc.ok ? "OK" : "throw"));
    if (tc.ok) {
      require(!d.requirements.isEmpty(),
              QStringLiteral("%1: expected a requirement").arg(QLatin1String(tc.id)));
      if (tc.expectName)
        require(d.requirements.at(0).name == QLatin1String(tc.expectName),
                QStringLiteral("%1: name '%2'").arg(QLatin1String(tc.id), d.requirements.at(0).name));
      if (tc.expectText)
        require(d.requirements.at(0).text == QLatin1String(tc.expectText),
                QStringLiteral("%1: text '%2'").arg(QLatin1String(tc.id), d.requirements.at(0).text));
      if (tc.expectClasses) {
        // ';' separates expected classes so a class name may itself contain ','.
        const QStringList expected = QString::fromLatin1(tc.expectClasses).split(QLatin1Char(';'));
        for (const QString& cls : expected)
          require(d.requirements.at(0).cssClasses.contains(cls),
                  QStringLiteral("%1: expected class '%2' in [%3]")
                      .arg(QLatin1String(tc.id), cls, d.requirements.at(0).cssClasses.join(QLatin1Char(','))));
      }
      if (tc.expectRelSrc)
        require(!d.relations.isEmpty() &&
                    d.relations.at(0).src == QLatin1String(tc.expectRelSrc) &&
                    d.relations.at(0).dst == QLatin1String(tc.expectRelDst),
                QStringLiteral("%1: relationship src/dst mismatch").arg(QLatin1String(tc.id)));
    }
  }

  return 0;
}
