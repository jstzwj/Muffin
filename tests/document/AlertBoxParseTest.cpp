#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>

using namespace muffin;

// GitHub-style alerts: a blockquote whose first line is `[!NOTE]`/`[!TIP]`/`[!IMPORTANT]`/
// `[!WARNING]`/`[!CAUTION]` is tagged with an AlertKind in a post-parse pass (gated on
// enableAlertBox), so the renderer can draw a themed card instead of a plain quote bar.

namespace {
const MarkdownNode* firstBlockOfType(const MarkdownNode& root, BlockType type) {
  for (const auto& child : root.children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  return nullptr;
}
}  // namespace

void testAlertKindsRecognized() {
  CmarkGfmParser parser;
  ParseOptions options;
  struct Case {
    const char* marker;
    AlertKind kind;
  };
  const Case cases[] = {
      {"NOTE", AlertKind::Note},
      {"TIP", AlertKind::Tip},
      {"IMPORTANT", AlertKind::Important},
      {"WARNING", AlertKind::Warning},
      {"CAUTION", AlertKind::Caution},
  };
  for (const Case& c : cases) {
    const QString markdown = QStringLiteral("> [!%1]\n> body text").arg(QString::fromLatin1(c.marker));
    ParseResult result = parser.parseDocument(markdown, options);
    require(result.root != nullptr, "parse should produce a root");
    const MarkdownNode* quote = firstBlockOfType(*result.root, BlockType::BlockQuote);
    require(quote != nullptr, "should parse as a blockquote");
    require(quote->alertKind() == c.kind, "alert kind should be recognized");
  }
}

void testAlertMarkerCaseInsensitive() {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult result = parser.parseDocument(QStringLiteral("> [!note]\n> body"), options);
  const MarkdownNode* quote = firstBlockOfType(*result.root, BlockType::BlockQuote);
  require(quote != nullptr, "should parse as a blockquote");
  require(quote->alertKind() == AlertKind::Note, "alert marker should match case insensitively");
}

void testNonAlertBlockquoteIsNone() {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult result = parser.parseDocument(QStringLiteral("> just a quote"), options);
  const MarkdownNode* quote = firstBlockOfType(*result.root, BlockType::BlockQuote);
  require(quote != nullptr, "should parse as a blockquote");
  require(quote->alertKind() == AlertKind::None, "non-alert blockquote should be None");
}

void testUnknownAlertKindIsNone() {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult result = parser.parseDocument(QStringLiteral("> [!BOGUS]\n> body"), options);
  const MarkdownNode* quote = firstBlockOfType(*result.root, BlockType::BlockQuote);
  require(quote != nullptr, "should parse as a blockquote");
  require(quote->alertKind() == AlertKind::None, "unknown alert kind should be None");
}

void testEnableAlertBoxFalseLeavesQuotePlain() {
  CmarkGfmParser parser;
  ParseOptions options;
  options.enableAlertBox = false;
  ParseResult result = parser.parseDocument(QStringLiteral("> [!NOTE]\n> body"), options);
  const MarkdownNode* quote = firstBlockOfType(*result.root, BlockType::BlockQuote);
  require(quote != nullptr, "should parse as a blockquote");
  require(quote->alertKind() == AlertKind::None,
          "with enableAlertBox=false the marker must not be recognized");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("AlertBoxParseTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testAlertKindsRecognized);
  RUN_TEST(testAlertMarkerCaseInsensitive);
  RUN_TEST(testNonAlertBlockquoteIsNone);
  RUN_TEST(testUnknownAlertKindIsNone);
  RUN_TEST(testEnableAlertBoxFalseLeavesQuotePlain);
#undef RUN_TEST
  return 0;
}
