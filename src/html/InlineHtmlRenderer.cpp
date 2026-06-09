#include "html/InlineHtmlRenderer.h"
#include "html/HtmlParser.h"
#include "html/HtmlBoxBuilder.h"
#include "html/HtmlStyleResolver.h"

namespace muffin::html {

InlineHtmlFormatResult InlineHtmlRenderer::render(const QString& htmlFragment, qreal baseFontSize) const {
  InlineHtmlFormatResult result;

  if (htmlFragment.trimmed().isEmpty()) {
    return result;
  }

  // 1. Parse HTML fragment
  HtmlDocument document;
  if (!document.parse(htmlFragment) || !document.valid()) {
    return result;
  }

  // 2. Build box tree
  HtmlBoxBuilder boxBuilder;
  auto root = boxBuilder.build(document);
  if (!root) {
    return result;
  }

  // 3. Resolve styles (tag defaults + inline styles)
  HtmlStyleResolver styleResolver;
  styleResolver.resolve(*root, baseFontSize);

  // 4. Collect inline text + format spans from the box tree.
  //    The root is a Body box; iterate its children to find inline content.
  //    If the first child is an inline element, collect from the root as a block container.
  //    If the first child is a block element, collect from its inline children.
  const HtmlBox* container = root.get();
  if (!root->children().empty()) {
    const auto& first = root->children().front();
    // If the first child is a block-level element, it might be a wrapper (e.g. <p>).
    // In that case, collect from it as the block container.
    if (isBlockTag(first->tag()) && !first->children().empty()) {
      container = first.get();
    }
  }

  QString text;
  std::vector<TextFormatSpan> spans;
  std::vector<HtmlTextLayout::LinkSpan> links;
  int offset = 0;
  HtmlTextMeasurer measurer;
  for (const auto& child : container->children()) {
    measurer.collectInlineTextFromRoot(
        *child, text, spans, links, offset,
        false, false, false, HtmlTextDecoration::None,
        QColor(), QColor(), QTextCharFormat::AlignNormal,
        QString(), baseFontSize, baseFontSize);
  }

  result.text = std::move(text);
  result.formatSpans = std::move(spans);
  result.links = std::move(links);
  return result;
}

bool InlineHtmlRenderer::isRenderableTag(QStringView tagName) {
  // Well-known inline HTML tags that we can render with formatting.
  static const QStringView known[] = {
      u"b",       u"i",     u"u",       u"s",       u"code",  u"kbd",
      u"mark",    u"sub",   u"sup",     u"a",       u"span",  u"br",
      u"small",   u"big",   u"strong",  u"em",      u"del",   u"ins",
      u"q",       u"abbr",  u"bold",    u"italic",  u"underline",
  };
  for (const auto& tag : known) {
    if (tagName.compare(tag, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace muffin::html
