#include "document/OutlineBuilder.h"

#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"

#include <QtGlobal>

namespace muffin {
namespace {

void appendInlineText(const InlineNode& node, QString& text) {
  switch (node.type()) {
    case InlineType::Text:
    case InlineType::Code:
    case InlineType::HtmlInline:
    case InlineType::InlineMath:
    case InlineType::TaskMarker:
      text += node.text();
      return;
    case InlineType::Image:
      text += node.alt();
      return;
    case InlineType::SoftBreak:
    case InlineType::LineBreak:
      text += QLatin1Char(' ');
      return;
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Link:
    case InlineType::Strikethrough:
    case InlineType::Unknown:
      for (const InlineNode& child : node.children()) {
        appendInlineText(child, text);
      }
      return;
  }
}

QString flattenedInlineText(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    appendInlineText(node, text);
  }
  return text.simplified();
}

void collectHeadings(const MarkdownNode& node, QVector<OutlineEntry>& entries, QVector<int>& levelStack) {
  if (node.type() == BlockType::Heading) {
    const int level = qBound(1, node.headingLevel(), 6);
    while (levelStack.size() >= level) {
      levelStack.removeLast();
    }

    QString title = flattenedInlineText(node.inlines());
    if (title.isEmpty()) {
      title = QString(level, QLatin1Char('#'));
    }

    const int parentIndex = levelStack.isEmpty() ? -1 : levelStack.last();
    entries.push_back({title, level, node.id(), node.sourceRange(), parentIndex});
    levelStack.push_back(entries.size() - 1);
  }

  for (const auto& child : node.children()) {
    collectHeadings(*child, entries, levelStack);
  }
}

}  // namespace

QVector<OutlineEntry> buildOutline(const MarkdownDocument& document) {
  QVector<OutlineEntry> entries;
  QVector<int> levelStack;
  collectHeadings(document.root(), entries, levelStack);
  return entries;
}

}  // namespace muffin
