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

  return 0;
}
