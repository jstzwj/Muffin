#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>
#include <QObject>

using namespace muffin;

// Phase 0 foundation for the Markdown preferences page: ParseOptions::operator== (skip-reparse
// guard), the parser honoring enableMath (previously a dead field — math was attached
// unconditionally), and DocumentSession::setParseOptions reparsing only on change.

void testParseOptionsEquality() {
  ParseOptions a;
  ParseOptions b;
  require(a == b, "default ParseOptions should be equal");
  b.enableMath = false;
  require(!(a == b), "differing enableMath should be unequal");
  b = a;
  b.enableAutolink = false;
  require(!(a == b), "differing enableAutolink should be unequal");
  b = a;
  b.enableTable = false;
  require(!(a == b), "differing enableTable should be unequal");
}

void testEnableMathGatesMathBlockParsing() {
  CmarkGfmParser parser;
  ParseOptions withMath;  // enableMath defaults to true
  ParseOptions noMath;
  noMath.enableMath = false;
  const QString markdown = "$$\ny = x\n$$\n";

  ParseResult withResult = parser.parseDocument(markdown, withMath);
  ParseResult noResult = parser.parseDocument(markdown, noMath);
  require(withResult.root != nullptr, "parse with math should produce a root");
  require(noResult.root != nullptr, "parse without math should produce a root");
  require(countMathBlocks(*withResult.root) >= 1, "math block should parse when enableMath=true");
  require(countMathBlocks(*noResult.root) == 0, "math block should NOT parse when enableMath=false");
}

void testSetParseOptionsReparsesOnlyOnChange() {
  DocumentSession session;
  int parsedCount = 0;
  QObject::connect(&session, &DocumentSession::parsed, [&parsedCount](qint64) { ++parsedCount; });

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  const int afterInit = parsedCount;
  require(afterInit >= 1, "setMarkdownText should parse and emit parsed");

  // Same (default) options → operator== equal → no re-parse, no signal.
  session.setParseOptions(ParseOptions{});
  require(parsedCount == afterInit, "unchanged options should not trigger a re-parse");

  // Different options → re-parse + signal.
  ParseOptions changed;
  changed.enableMath = false;
  session.setParseOptions(changed);
  require(parsedCount > afterInit, "changed options should trigger a re-parse");
  require(session.markdownText() == QStringLiteral("Hello world"), "re-parse must preserve text");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("MarkdownParseOptionsTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testParseOptionsEquality);
  RUN_TEST(testEnableMathGatesMathBlockParsing);
  RUN_TEST(testSetParseOptionsReparsesOnlyOnChange);
#undef RUN_TEST
  return 0;
}
