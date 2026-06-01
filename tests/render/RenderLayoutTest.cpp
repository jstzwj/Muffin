#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QFile>

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

QString readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("Could not open fixture: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

const MarkdownNode* findFirstBlock(const MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstBlock(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findBlockWithLiteral(const MarkdownNode& node, BlockType type, const QString& literalPart) {
  if (node.type() == type && node.literal().contains(literalPart)) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findBlockWithLiteral(*child, type, literalPart)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findFirstTableCell(const MarkdownNode& node) {
  if (node.type() == BlockType::TableCell) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstTableCell(*child)) {
      return found;
    }
  }
  return nullptr;
}

void requireUsableRect(const QRectF& rect, const QString& label) {
  require(rect.isValid(), QStringLiteral("%1 rect is invalid").arg(label));
  require(rect.width() > 20.0, QStringLiteral("%1 rect width too small").arg(label));
  require(rect.height() > 10.0, QStringLiteral("%1 rect height too small").arg(label));
}

void testInlineMarkerExpansion() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), QVector<InlineNode>{InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  RenderTheme theme = RenderTheme::github();
  InlineLayout collapsed;
  collapsed.build(inlines, theme, 400.0, theme.paragraphFont());
  require(!collapsed.html().contains(QStringLiteral("**")), QStringLiteral("collapsed inline should hide strong markers"));

  InlineLayout expanded;
  InlineLayout::BuildOptions options;
  options.activeTextOffset = 8;
  options.activeSourceOffset = 10;
  expanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), options);
  require(expanded.html().contains(QStringLiteral("**")), QStringLiteral("active inline should show strong markers"));
  require(expanded.plainText() == QStringLiteral("before bold after"), QStringLiteral("expanded plain text should stay collapsed"));
  require(expanded.hitTestTextOffset(expanded.cursorRect(8).center()) == 8,
          QStringLiteral("expanded hit test should map display marker offsets back to visible offsets"));
  require(expanded.hitTestSourceOffset(expanded.cursorRectForSourceOffset(9).center()) == 9,
          QStringLiteral("expanded hit test should map opener marker display to source offset"));

  QVector<InlineNode> mathInlines;
  mathInlines.push_back(InlineNode::inlineMath(QStringLiteral("a123")));
  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.activeSourceOffset = 2;
  math.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.hitTestSourceOffset(math.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("math cursor rect should round-trip source offset after first char"));
}

bool hasRole(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role) {
      return true;
    }
  }
  return false;
}

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

void testTreeSitterCodeHighlighting() {
  TreeSitterHighlighter highlighter;
  require(highlighter.supportsLanguage(QStringLiteral("python")), QStringLiteral("python highlighting should be registered"));
  const QVector<CodeHighlightSpan> python = highlighter.highlight(
      QStringLiteral("python"),
      QStringLiteral("def greet(name):\n    return \"hello \" + name\n"));
  require(hasRole(python, CodeHighlightRole::Keyword), QStringLiteral("python should highlight keywords"));
  require(hasRole(python, CodeHighlightRole::Function), QStringLiteral("python should highlight functions"));
  require(hasRole(python, CodeHighlightRole::String), QStringLiteral("python should highlight strings"));

  const QVector<CodeHighlightSpan> cpp = highlighter.highlight(
      QStringLiteral("cpp"),
      QStringLiteral("#include <QApplication>\nint main() { return 0; }\n"));
  require(hasRole(cpp, CodeHighlightRole::Preprocessor), QStringLiteral("cpp should highlight preprocessor"));
  require(hasRole(cpp, CodeHighlightRole::Keyword), QStringLiteral("cpp should highlight keywords"));
  require(hasRole(cpp, CodeHighlightRole::Function), QStringLiteral("cpp should highlight functions"));
  const QString cppText = QStringLiteral("#include <QApplication>\nint main() { return 0; }\n");
  const QVector<CodeHighlightSpan> cppExact = highlighter.highlight(QStringLiteral("cpp"), cppText);
  const qsizetype returnStart = cppText.indexOf(QStringLiteral("return"));
  require(hasExactRoleSpan(cppExact, CodeHighlightRole::Keyword, returnStart, returnStart + 6),
          QStringLiteral("cpp keyword span should align exactly with return"));
}

void testLayoutForTheme(const MarkdownDocument& document, const RenderTheme& theme, const QString& themeName) {
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);

  require(layout.pageWidth() > 300.0, QStringLiteral("%1 page width too small").arg(themeName));
  require(layout.totalHeight() > 700.0, QStringLiteral("%1 total height should exceed smoke viewport").arg(themeName));
  require(!layout.blocks().empty(), QStringLiteral("%1 layout produced no blocks").arg(themeName));

  const MarkdownNode* heading = findFirstBlock(document.root(), BlockType::Heading);
  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(document.root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(document.root());
  const MarkdownNode* code = findBlockWithLiteral(document.root(), BlockType::CodeFence, QStringLiteral("return 0"));
  const MarkdownNode* math = findBlockWithLiteral(document.root(), BlockType::MathBlock, QStringLiteral("E = mc"));

  require(heading != nullptr, QStringLiteral("Heading node missing"));
  require(paragraph != nullptr, QStringLiteral("Paragraph node missing"));
  require(table != nullptr, QStringLiteral("Table node missing"));
  require(tableCell != nullptr, QStringLiteral("Table cell node missing"));
  require(code != nullptr, QStringLiteral("Code node missing"));
  require(math != nullptr, QStringLiteral("Math node missing"));

  requireUsableRect(layout.block(heading->id())->rect(), QStringLiteral("%1 heading").arg(themeName));
  requireUsableRect(layout.block(paragraph->id())->rect(), QStringLiteral("%1 paragraph").arg(themeName));
  requireUsableRect(layout.block(table->id())->rect(), QStringLiteral("%1 table").arg(themeName));
  requireUsableRect(layout.block(code->id())->rect(), QStringLiteral("%1 code").arg(themeName));
  requireUsableRect(layout.block(math->id())->rect(), QStringLiteral("%1 math").arg(themeName));
  require(!layout.block(code->id())->literal().endsWith(QLatin1Char('\n')),
          QStringLiteral("%1 code display literal should hide structural trailing newline").arg(themeName));
  require(!layout.block(code->id())->codeHighlightSpans().isEmpty(),
          QStringLiteral("%1 code block should have syntax highlight spans").arg(themeName));
  require(layout.block(math->id())->rect().height() >= 20.0,
          QStringLiteral("%1 math block height should leave room for displayed text").arg(themeName));

  const QRectF paragraphRect = layout.block(paragraph->id())->rect();
  const HitTestResult paragraphHit = layout.hitTest(paragraphRect.center(), theme);
  require(paragraphHit.isValid(), QStringLiteral("%1 paragraph hit invalid").arg(themeName));
  require(paragraphHit.zone == HitTestResult::Zone::Text, QStringLiteral("%1 paragraph hit should be text").arg(themeName));
  require(paragraphHit.blockId == paragraph->id(), QStringLiteral("%1 paragraph hit id mismatch").arg(themeName));

  const QRectF tableRect = layout.block(table->id())->rect();
  const HitTestResult tableHit = layout.hitTest(tableRect.center(), theme);
  require(tableHit.isValid(), QStringLiteral("%1 table hit invalid").arg(themeName));
  require(tableHit.zone == HitTestResult::Zone::TableCell, QStringLiteral("%1 table hit should be cell").arg(themeName));
  require(tableHit.tableRow >= 0 && tableHit.tableColumn >= 0, QStringLiteral("%1 table hit indices missing").arg(themeName));

  const QRectF codeRect = layout.block(code->id())->rect();
  const HitTestResult codeHit = layout.hitTest(codeRect.center(), theme);
  require(codeHit.isValid(), QStringLiteral("%1 code hit invalid").arg(themeName));
  require(codeHit.zone == HitTestResult::Zone::Code, QStringLiteral("%1 code hit should be code").arg(themeName));
  SelectionRange codeSelection;
  codeSelection.anchor.blockId = code->id();
  codeSelection.anchor.text.nodeId = code->id();
  codeSelection.anchor.text.textOffset = 0;
  codeSelection.focus.blockId = code->id();
  codeSelection.focus.text.nodeId = code->id();
  codeSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRects(codeSelection, theme).isEmpty(),
          QStringLiteral("%1 code selection rects should not be empty").arg(themeName));
  SelectionRange crossSelection;
  crossSelection.anchor.blockId = paragraph->id();
  crossSelection.anchor.text.nodeId = paragraph->id();
  crossSelection.anchor.text.textOffset = 0;
  crossSelection.focus.blockId = code->id();
  crossSelection.focus.text.nodeId = code->id();
  crossSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("%1 code cross-block selection rects should not be empty").arg(themeName));
  require(!layout.block(table->id())->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("%1 table cross-block selection rects should not be empty").arg(themeName));

  const QRectF mathRect = layout.block(math->id())->rect();
  const HitTestResult mathHit = layout.hitTest(mathRect.center(), theme);
  require(mathHit.isValid(), QStringLiteral("%1 math hit invalid").arg(themeName));
  require(mathHit.zone == HitTestResult::Zone::Math, QStringLiteral("%1 math hit should be math").arg(themeName));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("offscreen") : qgetenv("QT_QPA_PLATFORM"));
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QString markdown = readFixture(QString::fromLocal8Bit(argv[1]));
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  testInlineMarkerExpansion();
  testTreeSitterCodeHighlighting();
  testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github"));
  testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint"));
  testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night"));
  return 0;
}
