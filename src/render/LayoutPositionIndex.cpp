#include "render/LayoutPositionIndex.h"

#include <QtGlobal>

namespace muffin {

qsizetype LayoutPositionIndex::nodeSize(const LayoutPositionToken* node) {
  return node ? node->subtreeSize : 0;
}

void LayoutPositionIndex::apply(LayoutPositionToken* node, qreal delta) {
  if (node) {
    node->value += delta;
    node->lazy += delta;
  }
}

void LayoutPositionIndex::push(LayoutPositionToken* node) {
  if (!node || qFuzzyIsNull(node->lazy)) {
    return;
  }
  apply(node->left.get(), node->lazy);
  apply(node->right.get(), node->lazy);
  node->lazy = 0.0;
}

void LayoutPositionIndex::update(LayoutPositionToken* node) {
  if (!node) {
    return;
  }
  node->subtreeSize = 1 + nodeSize(node->left.get()) + nodeSize(node->right.get());
  if (node->left) {
    node->left->parent = node;
  }
  if (node->right) {
    node->right->parent = node;
  }
}

std::pair<std::unique_ptr<LayoutPositionToken>, std::unique_ptr<LayoutPositionToken>>
LayoutPositionIndex::split(std::unique_ptr<LayoutPositionToken> root, qsizetype leftCount) {
  if (!root) {
    return {};
  }
  push(root.get());
  const qsizetype leftSize = nodeSize(root->left.get());
  if (leftCount <= leftSize) {
    auto [left, middle] = split(std::move(root->left), leftCount);
    root->left = std::move(middle);
    update(root.get());
    root->parent = nullptr;
    if (left) {
      left->parent = nullptr;
    }
    return {std::move(left), std::move(root)};
  }
  auto [middle, right] = split(std::move(root->right), leftCount - leftSize - 1);
  root->right = std::move(middle);
  update(root.get());
  root->parent = nullptr;
  if (right) {
    right->parent = nullptr;
  }
  return {std::move(root), std::move(right)};
}

std::unique_ptr<LayoutPositionToken> LayoutPositionIndex::merge(
    std::unique_ptr<LayoutPositionToken> left,
    std::unique_ptr<LayoutPositionToken> right) {
  if (!left) {
    if (right) {
      right->parent = nullptr;
    }
    return right;
  }
  if (!right) {
    left->parent = nullptr;
    return left;
  }
  if (left->priority < right->priority) {
    push(left.get());
    left->right = merge(std::move(left->right), std::move(right));
    update(left.get());
    left->parent = nullptr;
    return left;
  }
  push(right.get());
  right->left = merge(std::move(left), std::move(right->left));
  update(right.get());
  right->parent = nullptr;
  return right;
}

quint32 LayoutPositionIndex::nextPriority() {
  randomState_ ^= randomState_ << 13;
  randomState_ ^= randomState_ >> 17;
  randomState_ ^= randomState_ << 5;
  return randomState_;
}

std::unique_ptr<LayoutPositionToken> LayoutPositionIndex::makeToken() {
  auto token = std::make_unique<LayoutPositionToken>();
  token->priority = nextPriority();
  return token;
}

QVector<LayoutPositionToken*> LayoutPositionIndex::reset(qsizetype count) {
  QVector<LayoutPositionToken*> tokens;
  tokens.reserve(count);
  root_.reset();
  for (qsizetype i = 0; i < count; ++i) {
    auto token = makeToken();
    tokens.push_back(token.get());
    root_ = merge(std::move(root_), std::move(token));
  }
  return tokens;
}

qsizetype LayoutPositionIndex::size() const {
  return nodeSize(root_.get());
}

void LayoutPositionIndex::addSuffix(qsizetype first, qreal delta) {
  if (qFuzzyIsNull(delta) || first < 0 || first >= size()) {
    return;
  }
  auto [prefix, suffix] = split(std::move(root_), first);
  apply(suffix.get(), delta);
  root_ = merge(std::move(prefix), std::move(suffix));
}

qreal LayoutPositionIndex::adjustmentFor(const LayoutPositionToken* token) const {
  qreal result = token ? token->value : 0.0;
  for (const LayoutPositionToken* node = token ? token->parent : nullptr; node; node = node->parent) {
    result += node->lazy;
  }
  return result;
}

qsizetype LayoutPositionIndex::rank(const LayoutPositionToken* token) const {
  if (!token) {
    return -1;
  }
  qsizetype result = nodeSize(token->left.get());
  for (const LayoutPositionToken* node = token; node->parent; node = node->parent) {
    if (node == node->parent->right.get()) {
      result += 1 + nodeSize(node->parent->left.get());
    }
  }
  return result;
}

void LayoutPositionIndex::replace(
    qsizetype first,
    qsizetype count,
    std::unique_ptr<LayoutPositionToken> replacements) {
  first = qBound<qsizetype>(0, first, size());
  count = qBound<qsizetype>(0, count, size() - first);
  auto [prefix, tail] = split(std::move(root_), first);
  auto [removed, suffix] = split(std::move(tail), count);
  removed.reset();
  root_ = merge(merge(std::move(prefix), std::move(replacements)), std::move(suffix));
}

}  // namespace muffin
