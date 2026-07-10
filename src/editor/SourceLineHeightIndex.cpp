#include "editor/SourceLineHeightIndex.h"

#include <QtGlobal>

#include <algorithm>

namespace muffin {
namespace {

std::uint32_t nextPriority() {
  static std::uint32_t state = 0x9e3779b9U;
  state ^= state << 13U;
  state ^= state >> 17U;
  state ^= state << 5U;
  return state;
}

}  // namespace

qsizetype SourceLineHeightIndex::countOf(const std::unique_ptr<Node>& node) {
  return node ? node->subtreeCount : 0;
}

qint64 SourceLineHeightIndex::heightOf(const std::unique_ptr<Node>& node) {
  return node ? node->subtreeHeight : 0;
}

void SourceLineHeightIndex::refresh(Node& node) {
  node.subtreeCount = countOf(node.left) + node.count + countOf(node.right);
  node.subtreeHeight = heightOf(node.left) +
      static_cast<qint64>(node.count) * node.height + heightOf(node.right);
}

std::unique_ptr<SourceLineHeightIndex::Node> SourceLineHeightIndex::makeNode(
    qsizetype count, int height) {
  if (count <= 0) {
    return {};
  }
  auto node = std::make_unique<Node>();
  node->count = count;
  node->height = qMax(1, height);
  node->priority = nextPriority();
  refresh(*node);
  return node;
}

std::unique_ptr<SourceLineHeightIndex::Node> SourceLineHeightIndex::merge(
    std::unique_ptr<Node> left, std::unique_ptr<Node> right) {
  if (!left) return right;
  if (!right) return left;
  if (left->priority < right->priority) {
    left->right = merge(std::move(left->right), std::move(right));
    refresh(*left);
    return left;
  }
  right->left = merge(std::move(left), std::move(right->left));
  refresh(*right);
  return right;
}

std::pair<std::unique_ptr<SourceLineHeightIndex::Node>,
          std::unique_ptr<SourceLineHeightIndex::Node>>
SourceLineHeightIndex::split(std::unique_ptr<Node> root, qsizetype leftCount) {
  if (!root) return {};
  const qsizetype before = countOf(root->left);
  if (leftCount < before) {
    auto [left, middle] = split(std::move(root->left), leftCount);
    root->left = std::move(middle);
    refresh(*root);
    return {std::move(left), std::move(root)};
  }
  if (leftCount > before + root->count) {
    auto [middle, right] = split(
        std::move(root->right), leftCount - before - root->count);
    root->right = std::move(middle);
    refresh(*root);
    return {std::move(root), std::move(right)};
  }

  const qsizetype runLeft = qBound<qsizetype>(0, leftCount - before, root->count);
  const qsizetype runRight = root->count - runLeft;
  auto leftTree = std::move(root->left);
  auto rightTree = std::move(root->right);
  const int runHeight = root->height;
  leftTree = merge(std::move(leftTree), makeNode(runLeft, runHeight));
  rightTree = merge(makeNode(runRight, runHeight), std::move(rightTree));
  return {std::move(leftTree), std::move(rightTree)};
}

void SourceLineHeightIndex::reset(qsizetype lineCount, int estimatedHeight) {
  estimatedHeight_ = qMax(1, estimatedHeight);
  root_ = makeNode(qMax<qsizetype>(0, lineCount), estimatedHeight_);
}

qsizetype SourceLineHeightIndex::lineCount() const {
  return countOf(root_);
}

qint64 SourceLineHeightIndex::totalHeight() const {
  return heightOf(root_);
}

int SourceLineHeightIndex::estimatedHeight() const {
  return estimatedHeight_;
}

qint64 SourceLineHeightIndex::yForLine(qsizetype line) const {
  qsizetype remaining = qBound<qsizetype>(0, line, lineCount());
  qint64 y = 0;
  const Node* node = root_.get();
  while (node && remaining > 0) {
    const qsizetype before = countOf(node->left);
    if (remaining <= before) {
      node = node->left.get();
      continue;
    }
    y += heightOf(node->left);
    remaining -= before;
    const qsizetype inRun = qMin(remaining, node->count);
    y += static_cast<qint64>(inRun) * node->height;
    remaining -= inRun;
    if (remaining == 0) break;
    node = node->right.get();
  }
  return y;
}

qsizetype SourceLineHeightIndex::lineAtY(qint64 y) const {
  if (!root_ || lineCount() <= 0) return 0;
  qint64 remaining = qBound<qint64>(0, y, qMax<qint64>(0, totalHeight() - 1));
  qsizetype line = 0;
  const Node* node = root_.get();
  while (node) {
    const qint64 leftHeight = heightOf(node->left);
    if (remaining < leftHeight) {
      node = node->left.get();
      continue;
    }
    remaining -= leftHeight;
    line += countOf(node->left);
    const qint64 runHeight = static_cast<qint64>(node->count) * node->height;
    if (remaining < runHeight) {
      return line + static_cast<qsizetype>(remaining / node->height);
    }
    remaining -= runHeight;
    line += node->count;
    node = node->right.get();
  }
  return qMax<qsizetype>(0, lineCount() - 1);
}

int SourceLineHeightIndex::heightForLine(qsizetype line) const {
  if (!root_ || line < 0 || line >= lineCount()) return estimatedHeight_;
  qsizetype remaining = line;
  const Node* node = root_.get();
  while (node) {
    const qsizetype before = countOf(node->left);
    if (remaining < before) {
      node = node->left.get();
    } else if (remaining < before + node->count) {
      return node->height;
    } else {
      remaining -= before + node->count;
      node = node->right.get();
    }
  }
  return estimatedHeight_;
}

void SourceLineHeightIndex::setHeight(qsizetype line, int height) {
  if (line < 0 || line >= lineCount()) return;
  const int boundedHeight = qMax(1, height);
  if (heightForLine(line) == boundedHeight) return;
  auto [left, tail] = split(std::move(root_), line);
  auto [oldLine, right] = split(std::move(tail), 1);
  Q_UNUSED(oldLine);
  root_ = merge(merge(std::move(left), makeNode(1, boundedHeight)), std::move(right));
}

}  // namespace muffin
