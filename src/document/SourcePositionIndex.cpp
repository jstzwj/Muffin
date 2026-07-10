#include "document/SourcePositionIndex.h"

#include <QtGlobal>

namespace muffin {

qsizetype SourcePositionIndex::nodeSize(const std::unique_ptr<SourcePositionToken>& node) {
  return node ? node->subtreeSize : 0;
}

void SourcePositionIndex::apply(SourcePositionToken* node, SourcePositionDelta delta) {
  if (node) {
    node->value.bytes += delta.bytes;
    node->value.lines += delta.lines;
    node->lazy.bytes += delta.bytes;
    node->lazy.lines += delta.lines;
  }
}

void SourcePositionIndex::push(SourcePositionToken* node) {
  if (!node || (node->lazy.bytes == 0 && node->lazy.lines == 0)) {
    return;
  }
  apply(node->left.get(), node->lazy);
  apply(node->right.get(), node->lazy);
  node->lazy = {};
}

void SourcePositionIndex::update(SourcePositionToken* node) {
  if (!node) {
    return;
  }
  node->subtreeSize = 1 + nodeSize(node->left) + nodeSize(node->right);
  if (node->left) {
    node->left->parent = node;
  }
  if (node->right) {
    node->right->parent = node;
  }
  node->subtreeKindCounts.fill(0);
  for (quint8 kind = 0; kind < SourcePositionToken::kSiblingKindCount; ++kind) {
    node->subtreeKindCounts[kind] =
        (node->left ? node->left->subtreeKindCounts[kind] : 0) +
        (node->right ? node->right->subtreeKindCounts[kind] : 0);
  }
  if (node->siblingKind < SourcePositionToken::kSiblingKindCount) {
    ++node->subtreeKindCounts[node->siblingKind];
  }
}

std::pair<std::unique_ptr<SourcePositionToken>, std::unique_ptr<SourcePositionToken>>
SourcePositionIndex::split(std::unique_ptr<SourcePositionToken> root, qsizetype leftCount) {
  if (!root) {
    return {};
  }
  push(root.get());
  const qsizetype leftSize = nodeSize(root->left);
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

std::unique_ptr<SourcePositionToken> SourcePositionIndex::merge(
    std::unique_ptr<SourcePositionToken> left,
    std::unique_ptr<SourcePositionToken> right) {
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

quint32 SourcePositionIndex::nextPriority() {
  randomState_ ^= randomState_ << 13;
  randomState_ ^= randomState_ >> 17;
  randomState_ ^= randomState_ << 5;
  return randomState_;
}

std::unique_ptr<SourcePositionToken> SourcePositionIndex::makeToken(quint8 siblingKind) {
  auto token = std::make_unique<SourcePositionToken>();
  token->priority = nextPriority();
  token->siblingKind = qMin(siblingKind, quint8(SourcePositionToken::kSiblingKindCount - 1));
  token->subtreeKindCounts[token->siblingKind] = 1;
  return token;
}

QVector<SourcePositionToken*> SourcePositionIndex::reset(qsizetype count) {
  return reset(QVector<quint8>(count, 0));
}

QVector<SourcePositionToken*> SourcePositionIndex::reset(const QVector<quint8>& siblingKinds) {
  QVector<SourcePositionToken*> tokens;
  tokens.reserve(siblingKinds.size());
  root_.reset();
  for (quint8 siblingKind : siblingKinds) {
    auto token = makeToken(siblingKind);
    tokens.push_back(token.get());
    root_ = merge(std::move(root_), std::move(token));
  }
  return tokens;
}

qsizetype SourcePositionIndex::size() const {
  return nodeSize(root_);
}

void SourcePositionIndex::addSuffix(qsizetype first, qsizetype byteDelta, int lineDelta) {
  if ((byteDelta == 0 && lineDelta == 0) || first < 0 || first >= size()) {
    return;
  }
  auto [prefix, suffix] = split(std::move(root_), first);
  apply(suffix.get(), {byteDelta, lineDelta});
  root_ = merge(std::move(prefix), std::move(suffix));
}

SourcePositionDelta SourcePositionIndex::adjustmentFor(const SourcePositionToken* token) const {
  SourcePositionDelta result = token ? token->value : SourcePositionDelta{};
  for (const SourcePositionToken* node = token ? token->parent : nullptr; node; node = node->parent) {
    result.bytes += node->lazy.bytes;
    result.lines += node->lazy.lines;
  }
  return result;
}

qsizetype SourcePositionIndex::rank(const SourcePositionToken* token) const {
  if (!token) {
    return -1;
  }
  qsizetype result = token->left ? token->left->subtreeSize : 0;
  for (const SourcePositionToken* node = token; node->parent; node = node->parent) {
    if (node == node->parent->right.get()) {
      result += 1 + (node->parent->left ? node->parent->left->subtreeSize : 0);
    }
  }
  return result;
}

qsizetype SourcePositionIndex::typeRank(const SourcePositionToken* token) const {
  if (!token || token->siblingKind >= SourcePositionToken::kSiblingKindCount) {
    return -1;
  }
  const quint8 kind = token->siblingKind;
  qsizetype result = token->left ? token->left->subtreeKindCounts[kind] : 0;
  for (const SourcePositionToken* node = token; node->parent; node = node->parent) {
    if (node == node->parent->right.get()) {
      result += node->parent->left ? node->parent->left->subtreeKindCounts[kind] : 0;
      if (node->parent->siblingKind == kind) {
        ++result;
      }
    }
  }
  return result;
}

void SourcePositionIndex::replace(
    qsizetype first,
    qsizetype count,
    std::unique_ptr<SourcePositionToken> replacements) {
  first = qBound<qsizetype>(0, first, size());
  count = qBound<qsizetype>(0, count, size() - first);
  auto [prefix, tail] = split(std::move(root_), first);
  auto [removed, suffix] = split(std::move(tail), count);
  removed.reset();
  root_ = merge(merge(std::move(prefix), std::move(replacements)), std::move(suffix));
}

}  // namespace muffin
