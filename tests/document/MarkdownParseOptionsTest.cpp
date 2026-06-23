#include "app/MarkdownSettings.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>
#include <QObject>
#include <QSettings>

using namespace muffin;

// Phase 0 foundation for the Markdown preferences page: ParseOptions::operator== (skip-reparse
// guard), the parser honoring enableMath (previously a dead field — math was attached
// unconditionally), and DocumentSession::setParseOptions reparsing only on change.

namespace {
int countBlocks(const MarkdownNode& node, BlockType type) {
  int count = node.type() == type ? 1 : 0;
  for (const auto& child : node.children()) {
    count += countBlocks(*child, type);
  }
  return count;
}

int countInlineType(const QVector<InlineNode>& inlines, InlineType type) {
  int count = 0;
  for (const InlineNode& inlineNode : inlines) {
    if (inlineNode.type() == type) {
      ++count;
    }
    count += countInlineType(inlineNode.children(), type);
  }
  return count;
}

int countInlineType(const MarkdownNode& node, InlineType type) {
  int count = countInlineType(node.inlines(), type);
  for (const auto& child : node.children()) {
    count += countInlineType(*child, type);
  }
  return count;
}

// Mirrors what markdownParseOptions() builds when markdown/strictMode is true: only the always-on
// GFM extensions with no individual switch (tables, strikethrough, task lists) are off. Individually
// toggleable extensions (math, autolinks, highlight, alert boxes, sub/superscript) stay at their
// defaults — Strict Mode never overrides an explicit switch.
ParseOptions strictOptions() {
  ParseOptions options;
  options.enableTable = false;
  options.enableStrikethrough = false;
  options.enableTaskList = false;
  return options;
}
}  // namespace

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

// Strict mode (markdown/strictMode) disables the always-on GFM block extensions (tables,
// strikethrough, task lists) for plain CommonMark structure. Math and other individually-toggleable
// extensions follow their own switches and are NOT disabled by strict mode — so a user who enables
// Inline Formula still gets rendered math even with Strict Mode on.
void testStrictOptionsDisableExtensions() {
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\ny = x\n$$\n\n"
      "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n"
      "~~struck~~\n\n"
      "- [ ] task\n");

  ParseResult gfm = parser.parseDocument(markdown, ParseOptions{});
  require(gfm.root != nullptr, "default parse should produce a root");
  require(countMathBlocks(*gfm.root) >= 1, "math block should parse with extensions on");
  require(countBlocks(*gfm.root, BlockType::Table) >= 1, "table should parse with extensions on");
  require(countInlineType(*gfm.root, InlineType::Strikethrough) >= 1,
          "strikethrough should parse with extensions on");

  ParseResult strict = parser.parseDocument(markdown, strictOptions());
  require(strict.root != nullptr, "strict parse should produce a root");
  require(countMathBlocks(*strict.root) >= 1,
          "strict mode must NOT disable math — it follows its own switch");
  require(countBlocks(*strict.root, BlockType::Table) == 0, "strict mode should disable tables");
  require(countInlineType(*strict.root, InlineType::Strikethrough) == 0,
          "strict mode should disable strikethrough");
}

// The real markdownParseOptions() funnel (reads QSettings). With strictMode=true AND inlineMath
// explicitly on, math must stay enabled — Strict Mode must not silently override an explicit switch.
// Only the checkbox-less GFM defaults (tables, strikethrough, task lists) go off. This is the exact
// gap that let the original bug through (tests used ParseOptions{} instead of this funnel). Settings
// are written to this test's isolated org/app (MuffinTest), never the user's real Muffin settings.
void testMarkdownParseOptionsStrictModeDoesNotOverrideSwitches() {
  QSettings().setValue(QStringLiteral("markdown/strictMode"), true);
  QSettings().setValue(QStringLiteral("markdown/inlineMath"), true);
  QSettings().setValue(QStringLiteral("markdown/autoLink"), true);
  QSettings().setValue(QStringLiteral("markdown/subscript"), true);
  QSettings().setValue(QStringLiteral("markdown/highlight"), true);

  const ParseOptions opts = markdownParseOptions();
  require(opts.enableMath, "strict mode must NOT disable explicitly-enabled inline math");
  require(opts.enableAutolink, "strict mode must NOT disable explicitly-enabled autolinks");
  require(opts.enableSubscript, "strict mode must NOT disable explicitly-enabled subscript");
  require(opts.enableHighlight, "strict mode must NOT disable explicitly-enabled highlight");
  require(!opts.enableTable, "strict mode should still disable tables (no individual switch)");
  require(!opts.enableStrikethrough, "strict mode should still disable strikethrough");
  require(!opts.enableTaskList, "strict mode should still disable task lists");

  // And with strictMode off, an explicit inlineMath=false is honored.
  QSettings().setValue(QStringLiteral("markdown/strictMode"), false);
  QSettings().setValue(QStringLiteral("markdown/inlineMath"), false);
  const ParseOptions off = markdownParseOptions();
  require(!off.enableMath, "inlineMath=false should disable math when strict mode is off");
  require(off.enableTable, "tables should be on when strict mode is off");

  QSettings().remove(QStringLiteral("markdown"));  // clean up isolated test settings
}

// enableUnicodeRemap: Smart Dashes turns a typed `---` horizontal rule into a single em-dash char,
// which cmark no longer recognizes as a thematic break. The byte-length-preserving remap restores
// `---` for the parser only; source ranges still map onto the original em-dash text.
void testEnableUnicodeRemapParsesEmDashAsThematicBreak() {
  CmarkGfmParser parser;
  const QString markdown = QString::fromUtf8("\xe2\x80\x94\n");  // em-dash (smart-dash output for `---`)

  ParseOptions noRemap;
  ParseOptions withRemap;
  withRemap.enableUnicodeRemap = true;

  ParseResult offResult = parser.parseDocument(markdown, noRemap);
  ParseResult onResult = parser.parseDocument(markdown, withRemap);
  require(offResult.root != nullptr, "parse without remap should produce a root");
  require(onResult.root != nullptr, "parse with remap should produce a root");
  require(countBlocks(*offResult.root, BlockType::ThematicBreak) == 0,
          "a lone em-dash line should NOT be a thematic break without remap");
  require(countBlocks(*onResult.root, BlockType::ThematicBreak) >= 1,
          "a lone em-dash line should parse as a thematic break with remap enabled");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("MarkdownParseOptionsTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testParseOptionsEquality);
  RUN_TEST(testEnableMathGatesMathBlockParsing);
  RUN_TEST(testSetParseOptionsReparsesOnlyOnChange);
  RUN_TEST(testStrictOptionsDisableExtensions);
  RUN_TEST(testMarkdownParseOptionsStrictModeDoesNotOverrideSwitches);
  RUN_TEST(testEnableUnicodeRemapParsesEmDashAsThematicBreak);
#undef RUN_TEST
  return 0;
}
