// Heading CSS counter() auto-numbering: `#write h1:before { counter-increment: h1;
// content: counter(h1) ". " }` must render the *evaluated* outline number ("1. "),
// not the literal string "counter(h1) .". Mirrors tests/render/RenderListMarkerTest's
// counter(list-item) coverage, generalized to the heading ::before path.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/BlockLayout.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"
#include "theme/NodeCssElement.h"
#include "theme/DocumentCounterCache.h"
#include "diagnostics/ThemePerformance.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Collect every Heading node in document order (DFS, recursing into containers so a
// heading nested in a blockquote/list is counted at its true document position).
void collectHeadings(const MarkdownNode& node, QVector<const MarkdownNode*>& out) {
  if (node.type() == BlockType::Heading) {
    out.push_back(&node);
  }
  for (const auto& child : node.children()) {
    collectHeadings(*child, out);
  }
}

// phycat-style auto-numbering: a `--autonum-h1` variable expands to `counter(h1) ". "`,
// the document root resets h1, and each h1::before increments it. Two h1 → "1. "/"2. ".
void testCounterInHeadingBeforeVar() {
  const QString css = QStringLiteral("#write { color:#000000; counter-reset: h1; }"
                                     ":root { --autonum-h1: counter(h1) \". \"; }"
                                     "#write h1:before { counter-increment: h1; content: var(--autonum-h1); }");
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hctr"), QString()));
  require(theme.decorations().hasHeadingCounters,
          QStringLiteral("a counter() in h1::before content sets hasHeadingCounters"));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# First\n# Second\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 2, QStringLiteral("fixture should parse two h1"));
  require(layout.block(headings.at(0)->id())->headingBeforeText() == QStringLiteral("1. "),
          QStringLiteral("first h1 counter text '1. ' (got '%1')")
              .arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2. "),
          QStringLiteral("second h1 counter text '2. ' (got '%1')")
              .arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

// Outline numbering: h1 increments h1 and resets h2; h2 increments h2. An h2 ordinal
// restarts at 1 after each h1.
void testCounterOutlineResetsOnHigherLevel() {
  const QString css = QStringLiteral("#write { color:#000000; counter-reset: h1; }"
                                     "#write h1:before { counter-increment: h1; content: counter(h1) \". \"; }"
                                     "h1 { counter-reset: h2; }"
                                     "#write h2:before { counter-increment: h2; content: counter(h2) \". \"; }");
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hctr2"), QString()));
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
  const QString css = QStringLiteral("#write { color:#000000; }"
                                     "h1:before { content: \"§ \"; }");
  const ThemeDefinition def = CssThemeMapper::fromCss(css, QStringLiteral("hlit"), QString());
  require(!def.decorations.hasHeadingCounters,
          QStringLiteral("a literal-only h1::before must not set hasHeadingCounters"));
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
  const QString css = QStringLiteral("#write { color:#000000; counter-reset: h1; }"
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
          QStringLiteral("single-colon first h1 → '1.' (got '%1')")
              .arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2."),
          QStringLiteral("single-colon second h1 → '2.' (got '%1')")
              .arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

// A heading nested in a blockquote is still counted in document order (the recompute
// walk recurses into containers), so it receives the next ordinal.
void testNestedHeadingInBlockquoteCounted() {
  const QString css = QStringLiteral("#write { color:#000000; counter-reset: h1; }"
                                     "#write h1:before { counter-increment: h1; content: counter(h1) \". \"; }");
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hnested"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Top\n\n> # Nested\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 2, QStringLiteral("fixture should parse a top-level and a blockquote-nested h1"));
  require(
      layout.block(headings.at(0)->id())->headingBeforeText() == QStringLiteral("1. "),
      QStringLiteral("top-level h1 → '1. ' (got '%1')").arg(layout.block(headings.at(0)->id())->headingBeforeText()));
  require(layout.block(headings.at(1)->id())->headingBeforeText() == QStringLiteral("2. "),
          QStringLiteral("blockquote-nested h1 → '2. ' (got '%1')")
              .arg(layout.block(headings.at(1)->id())->headingBeforeText()));
}

void testMultiLevelCounterUsesInlineLayout() {
  const QString css = QStringLiteral(
      "#write { color:#000; counter-reset:h2 h3 h4 h5 h6; }"
      "h2 { counter-reset:h3 h4 h5 h6; } h3 { counter-reset:h4 h5 h6; }"
      "h4 { counter-reset:h5 h6; } h5 { counter-reset:h6; }"
      "h2:before { counter-increment:h2; content:counter(h2); margin-right:1.2em; }"
      "h3:before { counter-increment:h3; content:counter(h2) '.' counter(h3); margin-right:1.2em; }"
      "h4:before { counter-increment:h4; content:counter(h2) '.' counter(h3) '.' counter(h4); margin-right:1.2em; }"
      "h5:before { counter-increment:h5; content:counter(h2) '.' counter(h3) '.' counter(h4) '.' counter(h5); margin-right:1.2em; }"
      "h6:before { counter-increment:h6; content:counter(h2) '.' counter(h3) '.' counter(h4) '.' counter(h5) '.' counter(h6); margin-right:1.2em; }");
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("latex-counters"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("## A\n### B\n#### C\n##### D\n###### E\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  require(headings.size() == 5, QStringLiteral("fixture should parse five headings"));

  const BlockLayout* h6 = layout.block(headings.last()->id());
  require(h6->headingBeforeText() == QStringLiteral("1.1.1.1.1"),
          QStringLiteral("h6 should resolve the full counter chain"));
  const qreal advance = h6->inlineLayout()->cursorRect(0).left();
  const qreal textWidth = QFontMetricsF(theme.headingFont(6)).horizontalAdvance(h6->headingBeforeText());
  require(advance > textWidth, QStringLiteral("counter marker advance must fit measured text plus its margin"));

  int commonPaintLeft = -1;
  for (int i = 0; i < headings.size(); ++i) {
    const int level = i + 2;
    const BlockLayout* block = layout.block(headings.at(i)->id());
    qreal marginRight = 0.0;
    for (const PseudoElementRule& rule : theme.decorations().pseudos) {
      if (rule.host == QStringLiteral("h%1").arg(level) && rule.pseudo == QStringLiteral("before")) {
        marginRight = rule.marginRight;
        break;
      }
    }
    const qreal actualAdvance = block->inlineLayout()->cursorRect(0).left();
    const qreal expectedAdvance =
        QFontMetricsF(theme.headingFont(level)).horizontalAdvance(block->headingBeforeText()) + marginRight;
    require(qAbs(actualAdvance - expectedAdvance) < 0.05,
            QStringLiteral("h%1 counter column should use exact text width plus margin").arg(level));

    QImage markerImage(140, 50, QImage::Format_ARGB32_Premultiplied);
    markerImage.fill(Qt::transparent);
    {
      QPainter painter(&markerImage);
      block->inlineLayout()->paint(painter, QPointF(20, 5));
    }
    int paintedLeft = markerImage.width();
    for (int y = 0; y < markerImage.height(); ++y) {
      for (int x = 0; x < markerImage.width(); ++x) {
        if (qAlpha(markerImage.pixel(x, y)) > 0) {
          paintedLeft = qMin(paintedLeft, x);
        }
      }
    }
    require(paintedLeft < markerImage.width(), QStringLiteral("h%1 counter should paint pixels").arg(level));
    if (commonPaintLeft < 0)
      commonPaintLeft = paintedLeft;
    require(qAbs(paintedLeft - commonPaintLeft) <= 1, QStringLiteral("h2-h6 counter text should share one left edge"));
  }
}

void testCounterBeforeAndAfterUseDocumentOrder() {
  const auto theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss("#write{counter-reset:n}h4{font-size:16px;font-weight:400}"
                                                          "h4::before{counter-increment:n;content:counter(n) '.'}"
                                                          "h4::after{counter-increment:n;content:'.' counter(n)}",
                                                          "two-sided-counters", {}));
  DocumentSession session;
  session.setMarkdownText("#### A\n\n#### B\n", false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 400);
  const auto& nodes = session.document().root().children();
  for (int i = 0; i < 2; ++i) {
    const auto* block = layout.block(nodes[i]->id());
    const auto prefix = QString::number(2 * i + 1) + '.';
    const auto suffix = '.' + QString::number(2 * i + 2);
    require(block->headingBeforeText() == prefix, "::before resolves before the ::after increment");
    const auto actual = block->inlineLayout()->intrinsicWidths().second;
    const auto expected = QFontMetricsF(theme.headingFont(4)).horizontalAdvance(prefix + (i ? "B" : "A") + suffix);
    require(qAbs(actual - expected) < .1, "both generated counter strings participate in intrinsic measurement");
    require(block->inlineLayout()->visibleText() == (i ? "B" : "A"), "counter strings remain outside editable source");
  }
}

void testSparseCounterCandidates() {
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{counter-reset:n}h1::before{counter-increment:n;content:counter(n)}"
                              ".navigation a::before{counter-increment:n;content:counter(n)}"
                              ".ty-menu .label::before{content:var(--label)}",
                              "sparse-counters", {}));
  DocumentSession session;
  session.setMarkdownText(
      "# First\n\n" + QString("**bold** *italic* [link](https://example.com)\n\n").repeated(1000) + "# Last\n", false);
  resetThemePerformance();
  enableThemePerformance(true);
  theme.resolveDocumentCounters(session.document());
  enableThemePerformance(false);
  require(themePerformanceSamples()[size_t(ThemePerfStage::Counters)].work == 6,
          "unmatched navigation and UI candidates do not compute paragraph/inline styles");
  NodeCssElementBuilder tree;
  const auto* root = tree.build(session.document().root());
  require(!tree.hasTag(*root, "img", false) && !tree.hasClass(*root, "missing", false),
          "negative semantic descendant queries");
  require(tree.materializedElementCount() <= session.document().root().children().size() + 4,
          "negative :has probes do not materialize Markdown inline trees");
  theme.invalidateDocumentStyles();
  session.setMarkdownText("# First\n\ntext <span class='navigation'><a href='x'>link</a></span>\n\n# Last\n", false);
  theme.resolveDocumentCounters(session.document());
  const auto& nodes = session.document().root().children();
  require(theme.pseudoForNode(*nodes.back(), "before")->content == "3",
          "authored HTML ancestor still activates link counters");
  CssComputedStyle style;
  style.customProperties_["--x"] = "one";
  const auto old = style;
  const auto fingerprint = style.fingerprint();
  require(old.fingerprint() == fingerprint, "shared variable snapshot retains fingerprint");
  style.customProperties_["--x"] = "two";
  require(style.fingerprint() != fingerprint && old.fingerprint() == fingerprint,
          "variable mutation invalidates memoized hash without changing old snapshot");
}

void testCounterBlockAncestorCandidates() {
  const QString css = "#write{counter-reset:n}"
                      "h3 > span:first-of-type::before{counter-increment:n;content:counter(n)}"
                      "h4 strong em::before{counter-increment:n;content:counter(n)}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "block-ancestors", {}));
  DocumentSession session;
  session.setMarkdownText(QString("ordinary **bold** *italic* [link](x)\n\n").repeated(1000) +
                              "### **Title**\n\n#### **outer *Nested***\n\n",
                          false);
  resetThemePerformance();
  enableThemePerformance(true);
  theme.resolveDocumentCounters(session.document());
  enableThemePerformance(false);
  require(themePerformanceSamples()[size_t(ThemePerfStage::Counters)].work < 20 &&
              theme.styleCacheStats().elements < 40,
          "heading inline counters must not materialize ordinary paragraph trees");
  QVector<const MarkdownNode*> headings;
  collectHeadings(session.document().root(), headings);
  const auto* span = theme.cssInlineElement(*headings[0], 7, "span");
  const auto* em = theme.cssInlineElement(*headings[1], 16, "em");
  const auto first = span ? theme.pseudoForElement(*span, "before") : std::nullopt;
  const auto second = em ? theme.pseudoForElement(*em, "before") : std::nullopt;
  require(first && first->content == "1", QString("direct heading wrapper counter survives ancestor filtering: %1")
                                              .arg(first ? first->content : "missing"));
  require(second && second->content == "2",
          QString("nested inline ancestors remain conservative: %1").arg(second ? second->content : "missing"));

  // A preceding sibling is not an ancestor of the subject. Its tag cannot
  // become an ancestor requirement, including in mixed combinator chains.
  auto sibling = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{counter-reset:n}h3 + p span::before{counter-increment:n;content:counter(n)}", "sibling", {}));
  session.setMarkdownText("### Title\n\n**Following**\n\n", false);
  sibling.resolveDocumentCounters(session.document());
  const auto* following = sibling.cssInlineElement(*session.document().root().children()[1], 3, "span");
  require(following && sibling.pseudoForElement(*following, "before")->content == "1",
          "sibling subjects must not be rejected by ancestor tag constraints");
}

void testIncrementalCounterPrograms() {
  const QString css =
      "#write{counter-reset:chapter item}h1::before{counter-increment:chapter;content:counter(chapter) ':'}"
      "h1{counter-reset:item}ul{counter-reset:item}li{counter-increment:item}li::before{content:counters(item,'.')}"
      "span.seed{counter-set:chapter 7}span.seed::after{content:counter(chapter)}"
      "strong::before{content:counter(chapter) '/' counter(item)}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "incremental-counter", {}));
  DocumentSession session;
  session.setMarkdownText("# A\n\nordinary\n\n- One\n  - Inner\n- Two\n\n# B\n\n**bold**\n\n# C", false);
  theme.resolveDocumentCounters(session.document());
  const auto compare = [&] {
    auto full = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-oracle", {}));
    const auto actual = theme.resolveDocumentCounters(session.document());
    require(actual == full.resolveDocumentCounters(session.document()),
            "incremental block content must equal a fresh full counter walk");
    std::function<void(const CssElement&, const CssElement&)> check = [&](const auto& a, const auto& b) {
      for (const auto& side : {QStringLiteral("before"), QStringLiteral("after")}) {
        const auto ar = theme.pseudoForElement(a, side), br = full.pseudoForElement(b, side);
        require(bool(ar) == bool(br) && (!ar || ar->content == br->content),
                "inline and nested counter chains must equal the full walk");
      }
    };
    std::function<void(const MarkdownNode&)> blocks = [&](const auto& node) {
      const auto *a = theme.cssElementForNode(node), *b = full.cssElementForNode(node);
      check(*a, *b);
      for (const auto& inlineNode : node.inlines()) {
        const auto* ai = theme.cssInlineElement(node, inlineNode.sourceStart());
        const auto* bi = full.cssInlineElement(node, inlineNode.sourceStart());
        if (ai && bi)
          check(*ai, *bi);
      }
      for (const auto& child : node.children())
        blocks(*child);
    };
    blocks(session.document().root());
  };
  const auto edit = [&](qsizetype start, qsizetype count, const QString& value) {
    require(session.applyTextEdit(start, count, value) != muffin::TextEditResult::Rejected, "incremental counter edit applied");
    const auto range = session.lastLocalTopLevelRangeChange();
    require(range.isValid(), "edit supplies a precise replacement range");
    theme.invalidateDocumentStyles(range);
    compare();
  };
  edit(0, 0, "# Inserted\n\n");
  edit(0, 12, "");
  auto text = session.document().markdownText().toString();
  edit(text.indexOf("ordinary"), 8, "ordinary <span class='seed'>seed</span>");
  text = session.document().markdownText().toString();
  edit(text.indexOf("class='seed'"), 12, "class='plain'");
  text = session.document().markdownText().toString();
  edit(text.indexOf("  - Inner"), 9, "  - Inner\n  - Another");
  text = session.document().markdownText().toString();
  edit(text.indexOf("# B"), 3, "## B");
}

void testPlainEditRetainsCounterScope() {
  const auto css = "#write{counter-reset:n}h1::before{counter-increment:n;content:counter(n)}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-reuse", {}));
  DocumentSession session;
  session.setMarkdownText("paragraph\n\n" + QString("# Heading\n\nbody\n\n").repeated(1000), false);
  const auto before = theme.resolveDocumentCounters(session.document());
  require(session.applyTextEdit(2, 0, "edit") != muffin::TextEditResult::Rejected, "plain text edit applied");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange());
  enableThemePerformance(true);
  resetThemePerformance();
  const auto after = theme.resolveDocumentCounters(session.document());
  const auto samples = themePerformanceSamples();
  enableThemePerformance(false);
  require(before == after, "ordinary text edits retain all generated counter strings");
  require(samples[size_t(ThemePerfStage::Css)].work == 0 && samples[size_t(ThemePerfStage::Counters)].work <= 4,
          "ordinary text edits must not recompile or execute unchanged numbered blocks");
  auto copy = theme;
  require(session.applyTextEdit(0, 0, "# New\n\n") != muffin::TextEditResult::Rejected, "heading insertion applied");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange());
  theme.resolveDocumentCounters(session.document());
  require(copy.hasGeneratedCounters(), "retained theme snapshots keep their previous counter results");
}

void testCounterStructuralContextInvalidation() {
  const QString css =
      "#write{counter-reset:n}p:nth-child(2n){counter-increment:n}p::before{content:counter(n)}"
      "#write:has(strong){--step:3}#write{--step:1}h1{counter-increment:n var(--step)}h1::before{content:counter(n)}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-context", {}));
  DocumentSession session;
  session.setMarkdownText("one\n\ntwo\n\n# Heading\n\nthree", false);
  theme.resolveDocumentCounters(session.document());
  for (const auto& value : {QString("prefix\n\n"), QString("**bold**\n\n")}) {
    require(session.applyTextEdit(0, 0, value) != muffin::TextEditResult::Rejected, "contextual counter edit applied");
    theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange());
    auto full = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-full", {}));
    require(theme.resolveDocumentCounters(session.document()) == full.resolveDocumentCounters(session.document()),
            "position and ancestor :has variable changes must invalidate compiled programs");
  }
}

void testCounterPropagationStopsAtReset() {
  const auto css = "h1{counter-reset:n}h2::before{counter-increment:n;content:counter(n)}"
                   ".sidebar h1:first-child{counter-reset:n}.sidebar li:nth-child(2n){--step:2}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-convergence", {}));
  DocumentSession session;
  session.setMarkdownText("# First\n\n## A\n\n## B\n\n# Reset\n\n" + QString("## Later\n\n").repeated(1000), false);
  const auto before = theme.resolveDocumentCounters(session.document());
  require(session.applyTextEdit(9, 0, "## Inserted\n\n") != muffin::TextEditResult::Rejected, "insert numbered heading before the scope boundary");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange());
  enableThemePerformance(true);
  resetThemePerformance();
  const auto actual = theme.resolveDocumentCounters(session.document());
  const auto work = themePerformanceSamples()[size_t(ThemePerfStage::Counters)].work;
  enableThemePerformance(false);
  auto full = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "counter-convergence-full", {}));
  require(actual == full.resolveDocumentCounters(session.document()),
          "scope reset converges to the full-walk counter state");
  require(work < 10 && actual.size() == before.size() + 1,
          "a reset boundary must preserve the thousand unchanged later heading results");
}

void testEquivalentContextDeclarationsReusePrograms() {
  const auto css = "#write{counter-reset:n}h1::before,h6:first-child~h1:not(:last-child)::before"
                   "{counter-increment:n;content:counter(n)}";
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "equivalent-counter-rules", {}));
  DocumentSession session;
  session.setMarkdownText(QString("# Title\n\ntext\n\n").repeated(1000), false);
  theme.resolveDocumentCounters(session.document());
  require(session.applyTextEdit(0, 0, "# New\n\n") != muffin::TextEditResult::Rejected, "insert heading in a sheet with structural alternatives");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange());
  enableThemePerformance(true);
  resetThemePerformance();
  const auto actual = theme.resolveDocumentCounters(session.document());
  const auto cssWork = themePerformanceSamples()[size_t(ThemePerfStage::Css)].work;
  enableThemePerformance(false);
  auto full = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "equivalent-counter-full", {}));
  require(actual == full.resolveDocumentCounters(session.document()),
          "structural alternatives preserve numbering after insertion");
  require(cssWork < 20, "unchanged actual counter declarations must not recompile the thousand following programs");
}

void testSharedCounterSnapshotStorage() {
  DocumentCounterCache pool;
  const auto first = pool.retainSemantic(QByteArray("shape"));
  const auto second = pool.retainSemantic(QByteArray("shape"));
  require(first.constData() == second.constData() && pool.uniqueSemanticCount() == 1,
          "identical full signatures share their payload");
  auto retained = pool;
  pool.releaseSemantic(first);
  require(pool.uniqueSemanticCount() == 1, "a referenced signature must stay alive");
  pool.releaseSemantic(second);
  require(pool.uniqueSemanticCount() == 0 && retained.uniqueSemanticCount() == 1,
          "the last removed block releases its signature without changing retained snapshots");

  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{counter-reset:n}p::before{content:counter(n)}", "shared-counter-state", {}));
  DocumentSession session;
  session.setMarkdownText(QString("same **bold**\n\n").repeated(2000), false);
  theme.resolveDocumentCounters(session.document());
  const auto stats = theme.documentCounterStats();
  require(stats.blocks == qsizetype(session.document().root().children().size()) && stats.semanticShapes <= 2 &&
              stats.unsharedSemanticBytes > stats.semanticBytes * 500,
          "ordinary repeated blocks share signatures, including the parser's trailing empty paragraph");
  require(stats.memoizedBlocks == stats.blocks && stats.sharedStates <= 2,
          "numbered blocks with identical input/output states share immutable snapshots");
  for (int i = 0; i < 20; ++i) {
    const bool adding = session.document().markdownText().mid(0, 7) != "**new**";
    require(session.applyTextEdit(0, adding ? 0 : 8, adding ? "**new** " : "") != muffin::TextEditResult::Rejected, "signature replacement applies");
    theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange(), &session.document());
    theme.resolveDocumentCounters(session.document());
    const auto updated = theme.documentCounterStats();
    require(updated.semanticShapes == stats.semanticShapes + (adding ? 1 : 0) && updated.sharedStates <= 2,
            "replaced signature and state variants are released rather than accumulated");
  }
}

void testThemeSwitchReusesOnlyDocumentSemantics() {
  const auto first =
      CssThemeMapper::fromCss("#write{counter-reset:n}h1::before{counter-increment:n;content:counter(n)}", "first", {});
  const auto second =
      CssThemeMapper::fromCss("#write{counter-reset:n 10}h1::before{counter-increment:n 2;content:counter(n)}"
                              "li{counter-reset:m}li::before{counter-increment:m;content:counters(m,'.')}"
                              "h1:nth-child(3)::after{counter-set:n 30;content:counter(n)}"
                              "strong::before{content:attr(class) counter(n)}",
                              "second", {});
  DocumentSession session;
  session.setMarkdownText("# First\n\nordinary **bold**\n\n# Second\n\n- outer\n  - inner\n\n" +
                              QString("ordinary text\n\n").repeated(2000),
                          false);
  auto old = RenderTheme::fromDefinition(first);
  const auto oldText = old.resolveDocumentCounters(session.document());
  auto switched = RenderTheme::fromDefinition(second);
  switched.reuseDocumentSemantics(old);
  switched.updateForViewport(800, 600);
  switched.invalidateDocumentStyles();
  const auto actual = switched.resolveDocumentCounters(session.document());
  auto full = RenderTheme::fromDefinition(second);
  full.updateForViewport(800, 600);
  require(actual == full.resolveDocumentCounters(session.document()),
          "theme change recomputes counter CSS, scopes and generated content");
  require(actual != oldText, "new theme cannot reuse the previous theme's numbered text");
  const auto stats = switched.documentCounterStats();
  require(stats.semanticBuilds == 0,
          "unchanged document reuses exact semantic shapes across themes and viewport projection");
  require(stats.candidateScans < stats.blocks / 10, "repeated negative shapes avoid redundant candidate walks");
  require(old.resolveDocumentCounters(session.document()) == oldText,
          "theme switch leaves old counter snapshot immutable");
  const auto ordinary = session.document().markdownText().toString().indexOf("ordinary");
  require(session.applyTextEdit(ordinary, 1, "O") != muffin::TextEditResult::Rejected, "plain edit after theme change applies");
  switched.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange(), &session.document());
  require(switched.hasLocalStyleChange(), "prepared projection must not share a live view's mutable CSS cache");
  auto plainFull = RenderTheme::fromDefinition(second);
  plainFull.updateForViewport(800, 600);
  require(switched.resolveDocumentCounters(session.document()) == plainFull.resolveDocumentCounters(session.document()),
          "plain edit after theme change agrees with a full counter walk");
  require(session.applyTextEdit(0, 0, "# Inserted\n\n") != muffin::TextEditResult::Rejected, "structural edit after theme switch applies");
  auto edited = RenderTheme::fromDefinition(second);
  edited.reuseDocumentSemantics(switched);
  auto fresh = RenderTheme::fromDefinition(second);
  require(edited.resolveDocumentCounters(session.document()) == fresh.resolveDocumentCounters(session.document()),
          "a revision change rejects stale semantic seeds");
  require(edited.documentCounterStats().semanticBuilds > 0, "structural edits rescan document semantics");
  DocumentSession another;
  another.setMarkdownText("# Different\n", false);
  edited.invalidateDocumentStyles();
  require(edited.resolveDocumentCounters(another.document()) == fresh.resolveDocumentCounters(another.document()),
          "a different document cannot reuse semantic seeds");
}

} // namespace

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
  RUN_TEST(testMultiLevelCounterUsesInlineLayout);
  RUN_TEST(testCounterBeforeAndAfterUseDocumentOrder);
  RUN_TEST(testSparseCounterCandidates);
  RUN_TEST(testCounterBlockAncestorCandidates);
  RUN_TEST(testIncrementalCounterPrograms);
  RUN_TEST(testPlainEditRetainsCounterScope);
  RUN_TEST(testCounterStructuralContextInvalidation);
  RUN_TEST(testCounterPropagationStopsAtReset);
  RUN_TEST(testEquivalentContextDeclarationsReusePrograms);
  RUN_TEST(testSharedCounterSnapshotStorage);
  RUN_TEST(testThemeSwitchReusesOnlyDocumentSemantics);
#undef RUN_TEST
  return 0;
}
