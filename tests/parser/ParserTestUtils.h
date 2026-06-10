#pragma once

#include "../TestUtils.h"

#include "document/LineStartOffsetCache.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownSerializer.h"

#include <QCoreApplication>
#include <QDebug>

inline const muffin::MarkdownNode& childAt(const muffin::MarkdownNode& node, qsizetype index) {
  require(index >= 0 && index < node.children().size(), QStringLiteral("Missing child %1").arg(index));
  return *node.children()[index];
}

inline const muffin::MarkdownNode& definitionByLabel(const muffin::MarkdownNode& root, muffin::BlockType type, const QString& label) {
  for (const auto& child : root.children()) {
    if (child->type() == type && child->definition().label == label) {
      return *child;
    }
  }
  qCritical().noquote() << QStringLiteral("definition block not found: type=%1 label=%2")
                               .arg(static_cast<int>(type))
                               .arg(label);
  std::exit(1);
  return root;
}

inline int countInlineMath(const QVector<muffin::InlineNode>& inlines) {
  int count = 0;
  for (const muffin::InlineNode& inlineNode : inlines) {
    if (inlineNode.type() == muffin::InlineType::InlineMath) {
      ++count;
    }
    count += countInlineMath(inlineNode.children());
  }
  return count;
}

inline int countInlineMath(const muffin::MarkdownNode& node) {
  int count = countInlineMath(node.inlines());
  for (const auto& child : node.children()) {
    count += countInlineMath(*child);
  }
  return count;
}

inline QString sourceTextForNode(const QString& markdown, const muffin::MarkdownNode& node) {
  const muffin::SourceRange range = node.sourceRange();
  require(range.byteStart >= 0, QStringLiteral("node source range start is invalid"));
  require(range.byteEnd >= range.byteStart, QStringLiteral("node source range end is invalid"));
  require(range.byteEnd <= markdown.size(), QStringLiteral("node source range exceeds markdown size"));
  return markdown.mid(range.byteStart, range.byteEnd - range.byteStart);
}

inline bool containsInlineMathText(const QVector<muffin::InlineNode>& inlines, const QString& text) {
  for (const muffin::InlineNode& inlineNode : inlines) {
    if (inlineNode.type() == muffin::InlineType::InlineMath && inlineNode.text() == text) {
      return true;
    }
    if (containsInlineMathText(inlineNode.children(), text)) {
      return true;
    }
  }
  return false;
}

inline bool containsInlineMathText(const muffin::MarkdownNode& node, const QString& text) {
  if (containsInlineMathText(node.inlines(), text)) {
    return true;
  }
  for (const auto& child : node.children()) {
    if (containsInlineMathText(*child, text)) {
      return true;
    }
  }
  return false;
}

inline int countMathBlocks(const muffin::MarkdownNode& node) {
  int count = node.type() == muffin::BlockType::MathBlock ? 1 : 0;
  for (const auto& child : node.children()) {
    count += countMathBlocks(*child);
  }
  return count;
}
