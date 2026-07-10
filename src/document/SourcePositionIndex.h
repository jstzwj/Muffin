#pragma once

#include <QtTypes>
#include <QVector>

#include <memory>
#include <array>
#include <utility>

namespace muffin {

struct SourcePositionDelta {
  qsizetype bytes = 0;
  int lines = 0;
};

struct SourcePositionToken {
  static constexpr quint8 kSiblingKindCount = 17;

  std::unique_ptr<SourcePositionToken> left;
  std::unique_ptr<SourcePositionToken> right;
  SourcePositionToken* parent = nullptr;
  quint32 priority = 0;
  qsizetype subtreeSize = 1;
  SourcePositionDelta value;
  SourcePositionDelta lazy;
  quint8 siblingKind = 0;
  std::array<quint32, kSiblingKindCount> subtreeKindCounts{};
};

// Implicit treap with lazy range deltas. Tokens remain at stable addresses while split/merge moves
// ownership around them, so top-level blocks can resolve their current source adjustment in O(log
// n). Suffix add and structural range replacement are both O(log n + inserted blocks).
class SourcePositionIndex {
 public:
  SourcePositionIndex() = default;
  SourcePositionIndex(const SourcePositionIndex&) = delete;
  SourcePositionIndex& operator=(const SourcePositionIndex&) = delete;

  QVector<SourcePositionToken*> reset(qsizetype count);
  QVector<SourcePositionToken*> reset(const QVector<quint8>& siblingKinds);
  qsizetype size() const;
  void addSuffix(qsizetype first, qsizetype byteDelta, int lineDelta);
  SourcePositionDelta adjustmentFor(const SourcePositionToken* token) const;
  qsizetype rank(const SourcePositionToken* token) const;
  qsizetype typeRank(const SourcePositionToken* token) const;
  std::unique_ptr<SourcePositionToken> makeToken(quint8 siblingKind = 0);
  void replace(
      qsizetype first,
      qsizetype count,
      std::unique_ptr<SourcePositionToken> replacements);

  static std::unique_ptr<SourcePositionToken> merge(
      std::unique_ptr<SourcePositionToken> left,
      std::unique_ptr<SourcePositionToken> right);

 private:
  static qsizetype nodeSize(const std::unique_ptr<SourcePositionToken>& node);
  static void apply(SourcePositionToken* node, SourcePositionDelta delta);
  static void push(SourcePositionToken* node);
  static void update(SourcePositionToken* node);
  static std::pair<std::unique_ptr<SourcePositionToken>, std::unique_ptr<SourcePositionToken>> split(
      std::unique_ptr<SourcePositionToken> root,
      qsizetype leftCount);
  quint32 nextPriority();

  std::unique_ptr<SourcePositionToken> root_;
  quint32 randomState_ = 0x9e3779b9U;
};

}  // namespace muffin
