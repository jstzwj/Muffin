#include "render/BlockLayoutRules.h"

#include "theme/NodeCssElement.h"
#include "theme/MarkdownFlow.h"
#include "render/TextLayout.h"

namespace muffin::layout_rules {

bool isInsideBlockquote(const MarkdownNode& node) {
  for (const MarkdownNode* p = node.parent(); p; p = p->parent()) {
    if (p->type() == BlockType::BlockQuote) {
      return true;
    }
  }
  return false;
}

QMarginsF blockMarginForFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast, qreal containingWidth) {
  if (node.type() == BlockType::HtmlBlock)
    if (const auto* container = theme.markdownFlow(node)->container(node))
      return theme.elementBoxStyle(container->attributes.tag, &node, containingWidth).margin;
  if (!fast && node.type() == BlockType::Paragraph && isInsideBlockquote(node)) {
    const ThemeElementBoxStyle quoteParagraph =
        theme.elementBoxStyle(QStringLiteral("blockquote p"), &node, containingWidth);
    if (quoteParagraph.marginSpecified) {
      return quoteParagraph.margin;
    }
  }
  return theme.blockMargin(node.type(), node.headingLevel(), fast ? nullptr : &node, containingWidth);
}

bool hasCssFlowMargin(const MarkdownNode& node, const RenderTheme& theme, bool fast, qreal containingWidth) {
  if (node.type() == BlockType::HtmlBlock)
    if (const auto* container = theme.markdownFlow(node)->container(node))
      return theme.elementStyleForNode(node, container->attributes.tag)->box.marginSpecified;
  return theme.hasBlockMargin(node.type(), node.headingLevel(), fast ? nullptr : &node);
}

qreal spacingBeforeInFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast, qreal containingWidth) {
  const QMarginsF margin = blockMarginForFlow(node, theme, fast, containingWidth);
  return !margin.isNull() ? margin.top() : 0.0;
}

qreal spacingAfterInFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast, qreal containingWidth) {
  const QMarginsF margin = blockMarginForFlow(node, theme, fast, containingWidth);
  return !cssTagForNode(node).isEmpty() || theme.markdownFlow(node)->container(node) ? margin.bottom()
                                                                                     : theme.blockSpacing();
}

bool isTightListItemSiblingPair(const MarkdownNode& prev, const MarkdownNode& next) {
  const MarkdownNode* parent = prev.parent();
  return parent && parent == next.parent() && parent->type() == BlockType::List && parent->listTight() &&
         prev.type() == BlockType::ListItem && next.type() == BlockType::ListItem;
}

bool isTightListNestedChildPair(const MarkdownNode& prev, const MarkdownNode& next) {
  if (prev.type() != BlockType::Paragraph || next.type() != BlockType::List) {
    return false;
  }
  const MarkdownNode* item = next.parent();
  const MarkdownNode* list = item ? item->parent() : nullptr;
  return item && item == prev.parent() && item->type() == BlockType::ListItem && list &&
         list->type() == BlockType::List && list->listTight();
}

qreal spacingBetweenInFlow(const MarkdownNode& prev, const MarkdownNode& next, const RenderTheme& theme, bool fast,
                           qreal containingWidth) {
  if (isTightListItemSiblingPair(prev, next) || isTightListNestedChildPair(prev, next)) {
    return 0.0;
  }
  const qreal after = spacingAfterInFlow(prev, theme, fast, containingWidth);
  const qreal before = spacingBeforeInFlow(next, theme, fast, containingWidth);
  return (hasCssFlowMargin(prev, theme, fast, containingWidth) || hasCssFlowMargin(next, theme, fast, containingWidth))
             ? qMax<qreal>(0, qMax(after, before)) + qMin<qreal>(0, qMin(after, before))
             : after + before;
}

bool isVirtualEmptyParagraphNode(const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
}

bool omitVirtualEmptyParagraphInRenderFlow(const MarkdownNode& node, const SelectionRange& selection) {
  if (!isVirtualEmptyParagraphNode(node) || !isInsideBlockquote(node)) {
    return false;
  }
  // Render (a) the quote's TRAILING VEP — Typora shows trailing blank lines, and it's
  // where the caret lands after Enter at the end of the last quote line — and (b) the
  // VEP the caret is currently ON, i.e. the new empty line Enter creates BETWEEN other
  // blocks (e.g. an outer-quote paragraph and a nested quote). Without (b), pressing
  // Enter mid-quote changed the source but the view didn't change at all: the caret's
  // VEP was omitted, so it had no BlockLayout (the caret vanished) and added no height
  // (no new line appeared). Other (inter-paragraph separator) VEPs stay omitted — they
  // are already expressed as paragraph spacing, and rendering them would double-space
  // the quote.
  const MarkdownNode* parent = node.parent();
  const bool trailing = parent != nullptr && !parent->children().empty() && parent->children().back().get() == &node;
  const auto caretOnVep = [&](const CursorPosition& p) {
    return p.text.nodeId == node.id() ||
           (p.text.sourceOffset >= 0 && node.sourceRange().byteStart == p.text.sourceOffset);
  };
  if (trailing || caretOnVep(selection.focus) || caretOnVep(selection.anchor)) {
    return false;
  }
  return true;
}

qreal codeLineNumberGutterWidth(const QString& literal, const QFont& font) {
  const TextFontMetrics metrics(font);
  const int lineCount = literal.isEmpty() ? 1 : int(literal.count(QLatin1Char('\n'))) + 1;
  int digits = 1;
  for (int n = lineCount; n >= 10; n /= 10) {
    ++digits;
  }
  const qreal digitWidth = qMax<qreal>(1.0, metrics.horizontalAdvance(QStringLiteral("8")));
  // Gutter = 1-char left padding + the digits + a 2-char gap to the code. paintCodeLineNumbers
  // right-aligns the number leaving that 2-char gap (numRightX = codeRect.left() - 2*digitWidth).
  return static_cast<qreal>(digits + 1 + 2) * digitWidth;
}

QString displayLiteralFor(const MarkdownNode& node) {
  // Source geometry and edit offsets address the same verbatim literal,
  // including HTML indentation and trailing empty lines.
  return node.literal();
}

QVector<InlineNode> primaryInlinesForListItem(const MarkdownNode& node) {
  if (!node.inlines().isEmpty()) {
    return node.inlines();
  }
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child->inlines();
    }
  }
  return {};
}

} // namespace muffin::layout_rules
