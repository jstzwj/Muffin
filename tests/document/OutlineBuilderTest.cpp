#include "document/MarkdownDocument.h"
#include "document/OutlineBuilder.h"
#include "parser/CmarkGfmParser.h"

#include <QCoreApplication>
#include <QDebug>

#include <utility>

using namespace muffin;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) {
    fail(message);
  }
}

void parseDocument(MarkdownDocument& document, const QString& markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));
  document.setMarkdownText(markdown, std::move(parsed.root));
}

void testEmptyDocumentHasNoOutline() {
  MarkdownDocument document;
  parseDocument(document, QString());
  require(buildOutline(document).isEmpty(), QStringLiteral("Empty document should not have outline entries"));
}

void testHeadingLevelsAndRanges() {
  const QString markdown = QStringLiteral(
      "# Title\n\n"
      "Text\n\n"
      "## Child\n\n"
      "###### Deep\n");
  MarkdownDocument document;
  parseDocument(document, markdown);
  const QVector<OutlineEntry> outline = buildOutline(document);

  require(outline.size() == 3, QStringLiteral("Unexpected outline size"));
  require(outline[0].title == QStringLiteral("Title"), QStringLiteral("H1 title mismatch"));
  require(outline[0].level == 1, QStringLiteral("H1 level mismatch"));
  require(outline[0].parentIndex == -1, QStringLiteral("H1 parent mismatch"));
  require(outline[0].nodeId.isValid(), QStringLiteral("H1 node id invalid"));
  require(outline[0].sourceRange.byteStart == 0, QStringLiteral("H1 range start mismatch"));

  require(outline[1].title == QStringLiteral("Child"), QStringLiteral("H2 title mismatch"));
  require(outline[1].level == 2, QStringLiteral("H2 level mismatch"));
  require(outline[1].parentIndex == 0, QStringLiteral("H2 parent mismatch"));

  require(outline[2].title == QStringLiteral("Deep"), QStringLiteral("H6 title mismatch"));
  require(outline[2].level == 6, QStringLiteral("H6 level mismatch"));
  require(outline[2].parentIndex == 1, QStringLiteral("H6 parent mismatch"));
}

void testSkippedHeadingLevelsUseNearestLowerParent() {
  MarkdownDocument document;
  parseDocument(
      document,
      QStringLiteral(
          "# Root\n\n"
          "### Grandchild\n\n"
          "## Sibling\n\n"
          "#### Nested\n"));
  const QVector<OutlineEntry> outline = buildOutline(document);

  require(outline.size() == 4, QStringLiteral("Unexpected skipped-level outline size"));
  require(outline[1].parentIndex == 0, QStringLiteral("H3 should attach to H1"));
  require(outline[2].parentIndex == 0, QStringLiteral("H2 should attach to H1"));
  require(outline[3].parentIndex == 2, QStringLiteral("H4 should attach to nearest previous H2"));
}

void testInlineTextFlattening() {
  MarkdownDocument document;
  parseDocument(document, QStringLiteral("# **Bold** [Link](https://example.com) `code` $x$ ![Alt](img.png)\n"));
  const QVector<OutlineEntry> outline = buildOutline(document);

  require(outline.size() == 1, QStringLiteral("Inline heading outline size mismatch"));
  require(outline[0].title == QStringLiteral("Bold Link code x Alt"),
          QStringLiteral("Inline heading text was not flattened: %1").arg(outline[0].title));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testEmptyDocumentHasNoOutline();
  testHeadingLevelsAndRanges();
  testSkippedHeadingLevelsUseNearestLowerParent();
  testInlineTextFlattening();
  return 0;
}
