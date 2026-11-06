#include "document/HtmlContainerStructure.h"
#include <QRegularExpression>
#include <QSet>

namespace muffin {
std::optional<HtmlContainerBoundary> htmlContainerBoundary(const MarkdownNode& node) {
  if (node.type() != BlockType::HtmlBlock)
    return std::nullopt;
  const QString source = node.literal().trimmed();
  static const QRegularExpression token(R"(^<(\/?)([A-Za-z][A-Za-z0-9-]*)(?:\s+(?:[^<>"']|"[^"]*"|'[^']*')*)?\s*>$)");
  const auto match = token.match(source);
  if (!match.hasMatch() || source.endsWith("/>"))
    return std::nullopt;
  static const QSet<QString> tags{"div",    "section", "article", "aside",      "main",  "header",
                                  "footer", "nav",     "figure",  "blockquote", "center"};
  const QString tag = match.captured(2).toLower();
  if (!tags.contains(tag))
    return std::nullopt;
  const bool closing = !match.captured(1).isEmpty();
  if (closing && source.mid(2, source.size() - 3).trimmed().compare(tag, Qt::CaseInsensitive) != 0)
    return std::nullopt;
  return HtmlContainerBoundary{tag, closing};
}

QHash<size_t, size_t> htmlContainerPairs(const MarkdownNode& owner) {
  QHash<size_t, size_t> pairs;
  auto task = htmlContainerPairSteps(owner, pairs);
  task.resumeFor(-1);
  return pairs;
}

CooperativeTask htmlContainerPairSteps(const MarkdownNode& owner, QHash<size_t, size_t>& pairs) {
  std::vector<std::pair<size_t, QString>> stack;
  pairs.clear();
  const auto& children = owner.children();
  for (size_t i = 0; i < children.size(); ++i) {
    if (const auto token = htmlContainerBoundary(*children[i])) {
      if (!token->closing && stack.size() < 64)
        stack.emplace_back(i, token->tag);
      else if (token->closing && !stack.empty() && stack.back().second == token->tag) {
        pairs.insert(stack.back().first, i);
        stack.pop_back();
      } else {
        stack.clear();
        pairs.clear();
      }
    }
    co_yield nullptr;
  }
}
} // namespace muffin
