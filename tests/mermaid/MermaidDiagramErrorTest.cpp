// Every diagram reports its own parse failures: the diagnostic names the diagram, the kind of
// failure and where in the source it is. ER, pie and quadrant charts used to have no such report,
// so an invalid diagram of theirs showed up as a generic render failure that pointed at nothing.

#include "mermaid/MermaidDiagnostic.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include "../TestUtils.h"

#include <QGuiApplication>
#include <QStringList>

using muffin::mermaid::MermaidDiagnostic;
using muffin::mermaid::editor::MermaidRenderCache;
using muffin::mermaid::editor::MermaidRenderEntry;
using muffin::mermaid::editor::MermaidRenderStatus;

namespace {

MermaidRenderEntry render(const QString& source) {
  MermaidRenderCache cache;
  return cache.getSync(MermaidRenderCache::makeKey(source), source);
}

// Offset where the given line (counted from 1) starts in the source.
qsizetype lineStart(const QString& source, int line) {
  qsizetype offset = 0;
  for (int current = 1; current < line; ++current) {
    offset = source.indexOf(QLatin1Char('\n'), offset);
    require(offset >= 0, "precondition: the source has that many lines");
    ++offset;
  }
  return offset;
}

// The report of a parse failure on `line`: it names the diagram, is not the generic render
// failure, and lies on that line of the source the user wrote. Parsers that report a token place
// the failure on it; the others point at the start of the line.
void requireParseError(const MermaidRenderEntry& entry, const QString& diagramType, const QString& code,
                       const QString& source, int line, bool atLineStart = true) {
  const MermaidDiagnostic& diagnostic = entry.diagnostic;
  const QString what = QStringLiteral("%1 %2 on line %3").arg(diagramType, code).arg(line);
  const QString actual =
      QStringLiteral(" (got '%1' '%2' '%3')").arg(diagnostic.diagramType, diagnostic.stage, diagnostic.code);
  require(entry.status == MermaidRenderStatus::Error, what + ": the diagram should not render");
  require(diagnostic.diagramType == diagramType, what + ": the diagnostic should name the diagram" + actual);
  require(diagnostic.code == code, what + ": the code should be " + code + actual);
  require(diagnostic.stage != QStringLiteral("render") && diagnostic.code != QStringLiteral("native-render-failed"),
          what + ": a parse failure should not read as a render failure");
  require(!diagnostic.message.isEmpty(), what + ": the diagnostic should say what is wrong");
  const qsizetype lineOffset = lineStart(source, line);
  const qsizetype lineEnd = source.indexOf(QLatin1Char('\n'), lineOffset);
  require(diagnostic.span.line == line && diagnostic.span.offset >= lineOffset &&
              diagnostic.span.offset < (lineEnd < 0 ? source.size() : lineEnd),
          what + ": the position should be on the offending line");
  if (atLineStart) {
    require(diagnostic.span.offset == lineOffset && diagnostic.span.column == 1,
            what + ": the position should be the start of the offending line");
  }
}

} // namespace

void testErParseFailureIsReported() {
  const QString source = QStringLiteral("erDiagram\nCUSTOMER xxx ORDER : places");
  // The ER parser names the token it did not expect.
  requireParseError(render(source), QStringLiteral("er"), QStringLiteral("unexpected-token"), source, 2, false);
}

void testPieParseFailureIsReported() {
  const QString source = QStringLiteral("pie\n\"Bad\" : -1");
  requireParseError(render(source), QStringLiteral("pie"), QStringLiteral("pie-parse-error"), source, 2);
}

void testQuadrantParseFailureIsReported() {
  const QString source = QStringLiteral("quadrantChart\n\"P\": [0.5, 0.5] width: 3");
  requireParseError(render(source), QStringLiteral("quadrantChart"), QStringLiteral("quadrant-parse-error"), source, 2);
}

// A failure in a diagram that already reported it keeps its diagnostic. The positions are mapped
// around the front matter and directives the parser never sees.
void testJourneyParseFailureKeepsItsDiagnostic() {
  const QString source = QStringLiteral("---\ntitle: t\n---\n%%{init: {\"theme\": \"default\"}}%%\n"
                                        "journey\nsection S\ninvalid journey line");
  requireParseError(render(source), QStringLiteral("journey"), QStringLiteral("journey-parse-error"), source, 7);
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testErParseFailureIsReported);
  RUN_TEST(testPieParseFailureIsReported);
  RUN_TEST(testQuadrantParseFailureIsReported);
  RUN_TEST(testJourneyParseFailureKeepsItsDiagnostic);
#undef RUN_TEST
  return 0;
}
