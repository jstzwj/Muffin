// Heading CSS counter() auto-numbering: `#write h1:before { counter-increment: h1;
// content: counter(h1) ". " }` must render the *evaluated* outline number ("1. "),
// not the literal string "counter(h1) .". Mirrors tests/render/RenderListMarkerTest's
// counter(list-item) coverage, generalized to the heading ::before path.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Collect every Heading node in document order (DFS, recursing into containers so a
// heading nested in a blockquote/list is counted at its true document position).
void collectHeadings(const MarkdownNode& node, QVector<const MarkdownNode*>& out) {
  if (node.type() == BlockType::Heading) { out.push_back(&node); }
  for (const auto& child : node.children()) { collectHeadings(*child, out); }
}

// phycat-style auto-numbering: a `--autonum-h1` variable expands to `counter(h1) ". "`,
// the document root resets h1, and each h1::before increments it. Two h1 → "1. "/"2. ".
void testCounterInHeadingBeforeVar() {
  const QString css = QStringLiteral(
      "#write { color:#000000; counter-reset: h1; }"
      ":root { --autonum-h1: counter(h1) \". \"; }"
      "#write h1:before { counter-increment: h1; content: var(--autonum-h1); }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hctr"), QString()));
  require(theme.decorations().hasHeadingCounters, QStringLiteral("a counter() in h1::before content sets hasHeadingCounters"));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# First\n# Second\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 2, QStringLiteral("fixture should parse two h1"));
  require(layout.block(headings.at(0)->id())->headingBeforeText() == QStringLiteral("1. "),
          QStringLiteral("first h1 counter text '1. ' (got '%1')").arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2. "),
          QStringLiteral("second h1 counter text '2. ' (got '%1')").arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

// Outline numbering: h1 increments h1 and resets h2; h2 increments h2. An h2 ordinal
// restarts at 1 after each h1.
void testCounterOutlineResetsOnHigherLevel() {
  const QString css = QStringLiteral(
      "#write { color:#000000; counter-reset: h1 h2; }"
      "#write h1:before { counter-increment: h1; content: counter(h1) \". \"; }"
      "h1 { counter-reset: h2; }"
      "#write h2:before { counter-increment: h2; content: counter(h2) \". \"; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hctr2"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# A\n## B\n## C\n# D\n## E\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 5, QStringLiteral("fixture should parse five headings"));
  const auto text = [&](int i) { return layout.block(headings.at(i)->id())->headingBeforeText(); };
  require(text(0) == QStringLiteral("1. "), QStringLiteral("h1 A → '1. ' (got '%1')").arg(text(0)));
  require(text(1) == QStringLiteral("1. "), QStringLiteral("h2 B (first under A) → '1. ' (got '%1')").arg(text(1)));
  require(text(2) == QStringLiteral("2. "), QStringLiteral("h2 C → '2. ' (got '%1')").arg(text(2)));
  require(text(3) == QStringLiteral("2. "), QStringLiteral("h1 D → '2. ' (got '%1')").arg(text(3)));
  require(text(4) == QStringLiteral("1. "), QStringLiteral("h2 E (first under D) → '1. ' (got '%1')").arg(text(4)));
}

// Pure-literal ::before content (no counter()) must NOT trigger the subsystem: the
// heading keeps an empty resolved text and the painter draws the rule's literal glyph.
void testLiteralContentIsFastPath() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "h1:before { content: \"§ \"; }");
  const ThemeDefinition def = CssThemeMapper::fromCss(css, QStringLiteral("hlit"), QString());
  require(!def.decorations.hasHeadingCounters, QStringLiteral("a literal-only h1::before must not set hasHeadingCounters"));
  const RenderTheme theme = RenderTheme::fromDefinition(def);
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Heading\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 1, QStringLiteral("fixture should parse one h1"));
  require(layout.block(headings.first()->id())->headingBeforeText().isEmpty(),
          QStringLiteral("literal-only ::before keeps headingBeforeText empty (painter draws the glyph)"));
}

// Single-colon `:before` (CSS2 legacy) is normalized to the ::before channel, so it
// resolves identically to the double-colon form.
void testSingleColonBeforeNormalizes() {
  const QString css = QStringLiteral(
      "#write { color:#000000; counter-reset: h1; }"
      "#write h1:before { counter-increment: h1; content: counter(h1) \".\"; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hsc"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# One\n# Two\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 2, QStringLiteral("fixture should parse two h1"));
  require(layout.block(headings.at(0)->id())->headingBeforeText() == QStringLiteral("1."),
          QStringLiteral("single-colon first h1 → '1.' (got '%1')").arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2."),
          QStringLiteral("single-colon second h1 → '2.' (got '%1')").arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

// A heading nested in a blockquote is still counted in document order (the recompute
// walk recurses into containers), so it receives the next ordinal.
void testNestedHeadingInBlockquoteCounted() {
  const QString css = QStringLiteral(
      "#write { color:#000000; counter-reset: h1; }"
      "#write h1:before { counter-increment: h1; content: counter(h1) \". \"; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hnested"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Top\n\n> # Nested\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 2, QStringLiteral("fixture should parse a top-level and a blockquote-nested h1"));
  require(layout.block(headings.at(0)->id())->headingBeforeText() == QStringLiteral("1. "),
          QStringLiteral("top-level h1 → '1. ' (got '%1')").arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2. "),
          QStringLiteral("blockquote-nested h1 → '2. ' (got '%1')").arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCounterInHeadingBeforeVar);
  RUN_TEST(testCounterOutlineResetsOnHigherLevel);
  RUN_TEST(testLiteralContentIsFastPath);
  RUN_TEST(testSingleColonBeforeNormalizes);
  RUN_TEST(testNestedHeadingInBlockquoteCounted);
#undef RUN_TEST
  return 0;
}
