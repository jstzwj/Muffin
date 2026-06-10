#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "html/HtmlBoxBuilder.h"
#include "html/HtmlLayoutEngine.h"
#include "html/HtmlParser.h"
#include "html/HtmlRenderer.h"
#include "html/HtmlStyleResolver.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextLayout>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// ---- HTML helpers (local) ----

const html::HtmlBox* firstChildWithTag(const html::HtmlBox& box, html::HtmlTag tag) {
  for (const auto& child : box.children()) {
    if (child->tag() == tag) {
      return child.get();
    }
    if (const html::HtmlBox* nested = firstChildWithTag(*child, tag)) {
      return nested;
    }
  }
  return nullptr;
}

const html::HtmlBox* firstChildWithText(const html::HtmlBox& box, const QString& text) {
  for (const auto& child : box.children()) {
    if (child->tag() == html::HtmlTag::TextRun && child->text() == text) {
      return child.get();
    }
    if (const html::HtmlBox* nested = firstChildWithText(*child, text)) {
      return nested;
    }
  }
  return nullptr;
}

void collectChildrenWithTag(const html::HtmlBox& box, html::HtmlTag tag, QVector<const html::HtmlBox*>& out) {
  for (const auto& child : box.children()) {
    if (child->tag() == tag) {
      out.push_back(child.get());
    }
    collectChildrenWithTag(*child, tag, out);
  }
}

QRectF absoluteHtmlBoxRect(const html::HtmlBox& box) {
  QPointF origin;
  const html::HtmlBox* current = &box;
  while (current) {
    origin += QPointF(current->geometry().left, current->geometry().top);
    current = current->parent();
  }
  return QRectF(origin, QSizeF(box.geometry().width, box.geometry().height));
}

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

struct HighlightFixture {
  QString language;
  QString code;
  QVector<CodeHighlightRole> roles;
};

void requireHighlightsFixture(const TreeSitterHighlighter& highlighter, const HighlightFixture& fixture) {
  require(highlighter.supportsLanguage(fixture.language), QStringLiteral("%1 highlighting should be registered").arg(fixture.language));
  const QVector<CodeHighlightSpan> spans = highlighter.highlight(fixture.language, fixture.code);
  require(!spans.isEmpty(), QStringLiteral("%1 should produce highlight spans").arg(fixture.language));
  for (CodeHighlightRole role : fixture.roles) {
    require(hasRole(spans, role), QStringLiteral("%1 should highlight requested role %2").arg(fixture.language).arg(static_cast<int>(role)));
  }
}

// ---- test functions ----

void testLiteralBlockWrappedEditingGeometry() {
  RenderTheme theme = RenderTheme::github();
  const QString longLine = QStringLiteral("0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz");
  const QString markdown = QStringLiteral("```text\n%1\n```\n\n$$\n%1\n$$").arg(longLine);

  CmarkGfmParser parser;
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("wrapped literal parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  const MarkdownNode* code = findFirstBlock(document.root(), BlockType::CodeFence);
  const MarkdownNode* math = findFirstBlock(document.root(), BlockType::MathBlock);
  require(code != nullptr, QStringLiteral("wrapped code block should exist"));
  require(math != nullptr, QStringLiteral("wrapped math block should exist"));

  DocumentLayout codeLayout;
  codeLayout.rebuild(document, theme, 360.0);
  const BlockLayout* codeBlock = codeLayout.block(code->id());
  require(codeBlock != nullptr, QStringLiteral("wrapped code layout should exist"));
  const QRectF codeContent = codeBlock->literalContentRect(theme);
  const qreal codeLineHeight = theme.codeLineHeight();
  const HitTestResult codeSecondLineHit =
      codeBlock->hitTest(QPointF(codeContent.left() + 30.0, codeContent.top() + codeLineHeight * 1.4), theme);
  require(codeSecondLineHit.textOffset > 10, QStringLiteral("wrapped code hit-test should map into later visual line"));
  require(codeSecondLineHit.cursorRect.top() > codeContent.top() + codeLineHeight * 0.5,
          QStringLiteral("wrapped code cursor should move to visual wrapped line"));
  require(codeBlock->selectionRectsForOffsets(0, longLine.size(), theme).size() > 1,
          QStringLiteral("wrapped code selection should span multiple visual lines"));

  SelectionRange mathSelection;
  mathSelection.anchor.blockId = math->id();
  mathSelection.anchor.text.nodeId = math->id();
  mathSelection.focus = mathSelection.anchor;
  DocumentLayout mathEditingLayout;
  mathEditingLayout.rebuild(document, theme, 360.0, mathSelection);
  const BlockLayout* mathBlock = mathEditingLayout.block(math->id());
  require(mathBlock != nullptr && mathBlock->literalEditing(), QStringLiteral("wrapped math block should be editing"));
  const QRectF mathSource = mathBlock->literalContentRect(theme);
  const HitTestResult mathSecondLineHit =
      mathBlock->hitTest(QPointF(mathSource.left() + 30.0, mathSource.top() + codeLineHeight * 1.4), theme);
  require(mathSecondLineHit.textOffset > 10, QStringLiteral("wrapped math hit-test should map into later visual line"));
  require(mathSecondLineHit.cursorRect.top() > mathSource.top() + codeLineHeight * 0.5,
          QStringLiteral("wrapped math cursor should move to visual wrapped line"));
}

void testHtmlBlockPreviewAndEditingHitTestContract() {
  RenderTheme theme = RenderTheme::github();
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("<div><a href=\"#\">unsafe link</a><script>alert('blocked')</script></div>"), false);

  const MarkdownNode* html = findFirstBlock(session.document().root(), BlockType::HtmlBlock);
  require(html != nullptr, QStringLiteral("html hit-test fixture should parse"));

  DocumentLayout previewLayout;
  previewLayout.rebuild(session.document(), theme, 520.0);
  const BlockLayout* previewBlock = previewLayout.block(html->id());
  require(previewBlock != nullptr && !previewBlock->literalEditing(), QStringLiteral("html preview block should layout"));
  const HitTestResult previewHit = previewBlock->hitTest(previewBlock->rect().center(), theme);
  require(previewHit.zone == HitTestResult::Zone::Html, QStringLiteral("html preview hit should target html zone"));
  require(previewHit.cursorRect.top() == previewBlock->rect().top(), QStringLiteral("html preview cursor should align to rendered block"));
  require(previewHit.textOffset == 0 || previewHit.textOffset == previewBlock->literal().size(),
          QStringLiteral("html preview hit should snap to block boundary, not raw source text"));
  require(previewBlock->selectionRectsForOffsets(0, previewBlock->literal().size(), theme).size() == 1,
          QStringLiteral("html preview selection should select rendered block as a unit"));

  DocumentLayout editingLayout;
  editingLayout.setEditingHtmlBlock(html->id());
  editingLayout.rebuild(session.document(), theme, 520.0);
  const BlockLayout* editingBlock = editingLayout.block(html->id());
  require(editingBlock != nullptr && editingBlock->literalEditing(), QStringLiteral("html editing block should layout as source"));
  const QRectF sourceRect = editingBlock->literalContentRect(theme);
  const HitTestResult editingHit = editingBlock->hitTest(QPointF(sourceRect.left() + 80.0, sourceRect.top() + theme.codeLineHeight() * 0.5), theme);
  require(editingHit.textOffset > 0 && editingHit.textOffset < editingBlock->literal().size(),
          QStringLiteral("html editing hit should map into raw source text"));
}

void testFrontMatterLayoutPreservesTrailingLiteralNewline() {
  RenderTheme theme = RenderTheme::github();

  DocumentSession withoutTrailingNewline;
  withoutTrailingNewline.setMarkdownText(QStringLiteral("---\ntitle: Muffin\n---"), false);
  DocumentLayout compactLayout;
  compactLayout.rebuild(withoutTrailingNewline.document(), theme, 800.0);
  const MarkdownNode* compactFrontMatter = findFirstBlock(withoutTrailingNewline.document().root(), BlockType::FrontMatter);
  require(compactFrontMatter != nullptr, QStringLiteral("compact front matter should parse"));
  const BlockLayout* compactBlock = compactLayout.block(compactFrontMatter->id());
  require(compactBlock != nullptr, QStringLiteral("compact front matter should layout"));

  DocumentSession withTrailingNewline;
  withTrailingNewline.setMarkdownText(QStringLiteral("---\ntitle: Muffin\n\n---"), false);
  DocumentLayout expandedLayout;
  expandedLayout.rebuild(withTrailingNewline.document(), theme, 800.0);
  const MarkdownNode* expandedFrontMatter = findFirstBlock(withTrailingNewline.document().root(), BlockType::FrontMatter);
  require(expandedFrontMatter != nullptr, QStringLiteral("expanded front matter should parse"));
  const BlockLayout* expandedBlock = expandedLayout.block(expandedFrontMatter->id());
  require(expandedBlock != nullptr, QStringLiteral("expanded front matter should layout"));

  require(expandedFrontMatter->literal().endsWith(QLatin1Char('\n')), QStringLiteral("expanded front matter literal should include user newline"));
  require(expandedBlock->literal() == expandedFrontMatter->literal(), QStringLiteral("front matter layout should preserve trailing literal newline"));
  require(expandedBlock->rect().height() > compactBlock->rect().height() + theme.codeLineHeight() * 0.5,
          QStringLiteral("front matter trailing newline should reserve a visible empty line"));
}

void testDocumentLayoutInlineLayoutContract() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("alpha `code` beta gamma delta epsilon zeta eta theta\n\n| A | B |\n| --- | --- |\n| `one` | two |"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 360.0);

  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(session.document().root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(paragraph != nullptr && table != nullptr && tableCell != nullptr, QStringLiteral("inline layout document fixture missing blocks"));

  const BlockLayout* paragraphLayout = layout.block(paragraph->id());
  const BlockLayout* tableLayout = layout.block(table->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("inline layout paragraph layout missing"));
  require(tableLayout != nullptr && tableLayout->type() == BlockType::Table, QStringLiteral("inline layout table layout missing"));
  const BlockLayout::TableCellLayout* cellLayout = findTableCellLayout(*tableLayout, tableCell->id());
  require(cellLayout != nullptr, QStringLiteral("inline layout table cell layout missing"));

  require(paragraphLayout->inlineLayout()->height() > 0.0, QStringLiteral("paragraph inline layout should have height"));
  require(paragraphLayout->inlineLayout()->size().height() == paragraphLayout->inlineLayout()->height(),
          QStringLiteral("paragraph inline layout size should expose height"));
  require(cellLayout->text.height() > 0.0, QStringLiteral("table cell inline layout should have height"));
  require(cellLayout->text.size().height() == cellLayout->text.height(), QStringLiteral("table cell inline layout size should expose height"));
  for (const BlockLayout::TableRowLayout& row : tableLayout->tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      require(!cell.text.displayText().contains(QLatin1Char('|')), QStringLiteral("table cell layout should hide markdown pipe separators"));
    }
  }

  QImage image(QSize(420, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  for (const auto& block : layout.blocks()) {
    block->paint(painter, theme, 0.0);
  }
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 100, QStringLiteral("document inline layout paint should draw visible pixels"));

  const HitTestResult paragraphHit = layout.hitTest(paragraphLayout->rect().center(), theme);
  require(paragraphHit.isValid() && paragraphHit.zone == HitTestResult::Zone::Text,
          QStringLiteral("document inline layout paragraph hit-test should hit text"));
  require(!paragraphLayout->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("document inline layout paragraph selection should be available"));

  const HitTestResult tableHit = layout.hitTest(cellLayout->rect.center(), theme);
  require(tableHit.isValid() && tableHit.zone == HitTestResult::Zone::TableCell,
          QStringLiteral("document inline layout table hit-test should hit table cell"));
  require(!tableLayout->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("document inline layout table selection should be available"));
}

void testHtmlInlineLayoutOwnershipContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral("<div><h1>HTML block sample</h1><p><a href=\"#\">safe link</a></p><script>alert('blocked')</script></div>")),
          QStringLiteral("html ownership fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html ownership fixture should build box tree"));
  require(!root->collectedText().contains(QStringLiteral("blocked")), QStringLiteral("html script text should not enter box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 320.0, 16.0, textLayouts);

  const html::HtmlBox* heading = firstChildWithTag(*root, html::HtmlTag::Heading1);
  const html::HtmlBox* paragraph = firstChildWithTag(*root, html::HtmlTag::Paragraph);
  const html::HtmlBox* anchor = firstChildWithTag(*root, html::HtmlTag::Anchor);
  require(heading != nullptr && paragraph != nullptr && anchor != nullptr, QStringLiteral("html ownership fixture missing expected nodes"));
  require(!root->ownsTextLayout(), QStringLiteral("html root must not own descendant text layout"));
  require(heading->ownsTextLayout(), QStringLiteral("html heading should own its inline text layout"));
  require(paragraph->ownsTextLayout(), QStringLiteral("html paragraph should own its inline text layout"));
  require(!anchor->ownsTextLayout(), QStringLiteral("html inline descendant must not own duplicate layout"));
  require(textLayouts.size() == 2, QStringLiteral("html fixture should create exactly two inline text layouts"));

  for (const auto& child : heading->children()) {
    require(!child->ownsTextLayout(), QStringLiteral("html text run child must not own duplicate layout"));
  }
}

void testHtmlInlineStyleAndTagSemanticsContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral(
              "<div style=\"border-left: 3px solid #cccccc\">"
              "alpha <q>quoted</q> H<sub>2</sub><small>small</small><span style=\"font-size: 24px\">large</span>"
              "<kbd>Ctrl</kbd>+<kbd>C</kbd>"
              "</div>"
              "<div style=\"display: none\"><span>hidden child</span></div>")),
          QStringLiteral("html style semantics fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html style semantics fixture should build box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  const html::HtmlBox* bordered = firstChildWithTag(*root, html::HtmlTag::Div);
  require(bordered != nullptr, QStringLiteral("html style semantics fixture missing bordered div"));
  require(qFuzzyCompare(bordered->style().borderWidth.left(), 3.0), QStringLiteral("border-left should set left border"));
  require(qFuzzyIsNull(bordered->style().borderWidth.top()), QStringLiteral("border-left should not set top border"));
  require(qFuzzyIsNull(bordered->style().borderWidth.right()), QStringLiteral("border-left should not set right border"));
  require(qFuzzyIsNull(bordered->style().borderWidth.bottom()), QStringLiteral("border-left should not set bottom border"));

  const html::HtmlBox* quote = firstChildWithTag(*root, html::HtmlTag::Quote);
  require(quote != nullptr, QStringLiteral("q tag should map to quote tag"));
  require(quote->style().display == html::HtmlDisplay::Inline, QStringLiteral("q tag should remain inline"));

  const html::HtmlBox* subText = firstChildWithText(*root, QStringLiteral("2"));
  const html::HtmlBox* smallText = firstChildWithText(*root, QStringLiteral("small"));
  const html::HtmlBox* largeText = firstChildWithText(*root, QStringLiteral("large"));
  const html::HtmlBox* keyboard = firstChildWithTag(*root, html::HtmlTag::Kbd);
  require(subText != nullptr && subText->style().fontSize < 16.0, QStringLiteral("sub text should inherit smaller font size"));
  require(smallText != nullptr && smallText->style().fontSize < 16.0, QStringLiteral("small text should inherit smaller font size"));
  require(largeText != nullptr && qFuzzyCompare(largeText->style().fontSize, 24.0), QStringLiteral("inline font-size should propagate to text"));
  require(keyboard != nullptr && keyboard->style().display == html::HtmlDisplay::Inline,
          QStringLiteral("kbd tag should remain inline"));

  const html::HtmlBox* hiddenText = firstChildWithText(*root, QStringLiteral("hidden child"));
  require(hiddenText != nullptr && hiddenText->style().visible, QStringLiteral("hidden child starts with its own computed visibility"));

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 220.0, 16.0, textLayouts);

  require(hiddenText->style().visible, QStringLiteral("layout must not mutate descendant visibility under hidden parent"));
  require(!textLayouts.empty(), QStringLiteral("html style semantics fixture should create text layouts"));

  bool sawSubSize = false;
  bool sawSubBaseline = false;
  bool sawSmallSize = false;
  bool sawLargeSize = false;
  bool sawKeyboard = false;
  for (const auto& textLayout : textLayouts) {
    if (!textLayout || !textLayout->layout) {
      continue;
    }
    for (const html::TextFormatSpan& span : textLayout->formatSpans) {
      const QString spanText = textLayout->text.mid(span.start, span.length);
      if (span.keyboard && (spanText == QStringLiteral("Ctrl") || spanText == QStringLiteral("C"))) {
        sawKeyboard = true;
      }
    }
    for (const QTextLayout::FormatRange& range : textLayout->layout->formats()) {
      const QString spanText = textLayout->text.mid(range.start, range.length);
      const qreal pointSize = range.format.fontPointSize();
      if (spanText == QStringLiteral("2") && pointSize > 0 && pointSize < 16.0) {
        sawSubSize = true;
        if (range.format.verticalAlignment() == QTextCharFormat::AlignSubScript) {
          sawSubBaseline = true;
        }
      } else if (spanText == QStringLiteral("small") && pointSize > 0 && pointSize < 16.0) {
        sawSmallSize = true;
      } else if (spanText == QStringLiteral("large") && qFuzzyCompare(pointSize, 24.0)) {
        sawLargeSize = true;
      }
    }
  }
  require(sawSubSize, QStringLiteral("sub text should emit smaller font format"));
  require(sawSubBaseline, QStringLiteral("sub text should emit subscript baseline format"));
  require(sawSmallSize, QStringLiteral("small text should emit smaller font format"));
  require(sawLargeSize, QStringLiteral("inline font-size should emit font format"));
  require(sawKeyboard, QStringLiteral("kbd text should emit keyboard span format"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLiteralBlockWrappedEditingGeometry);
  RUN_TEST(testHtmlBlockPreviewAndEditingHitTestContract);
  RUN_TEST(testFrontMatterLayoutPreservesTrailingLiteralNewline);
  RUN_TEST(testDocumentLayoutInlineLayoutContract);
  RUN_TEST(testHtmlInlineLayoutOwnershipContract);
  RUN_TEST(testHtmlInlineStyleAndTagSemanticsContract);
#undef RUN_TEST
  return 0;
}

