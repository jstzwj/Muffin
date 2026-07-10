#pragma once

#include <QtTypes>
#include <QVector>

#include <memory>
#include <utility>

namespace muffin {

struct LayoutPositionToken {
  std::unique_ptr<LayoutPositionToken> left;
  std::unique_ptr<LayoutPositionToken> right;
  LayoutPositionToken* parent = nullptr;
  quint32 priority = 0;
  qsizetype subtreeSize = 1;
  qreal value = 0.0;
  qreal lazy = 0.0;
};

// Stable implicit sequence for layout slots. The tree owns position tokens while BlockSlot keeps
// raw pointers to them. Split/merge changes ownership without moving tokens, so suffix shifts and
// structural splices stay O(log n) and callers can recover a slot's current rank from its token.
class LayoutPositionIndex {
 public:
  LayoutPositionIndex() = default;
  LayoutPositionIndex(const LayoutPositionIndex&) = delete;
  LayoutPositionIndex& operator=(const LayoutPositionIndex&) = delete;

  QVector<LayoutPositionToken*> reset(qsizetype count);
  qsizetype size() const;
  void addSuffix(qsizetype first, qreal delta);
  qreal adjustmentFor(const LayoutPositionToken* token) const;
  qsizetype rank(const LayoutPositionToken* token) const;
  std::unique_ptr<LayoutPositionToken> makeToken();
  void replace(
      qsizetype first,
      qsizetype count,
      std::unique_ptr<LayoutPositionToken> replacements);

  static std::unique_ptr<LayoutPositionToken> merge(
      std::unique_ptr<LayoutPositionToken> left,
      std::unique_ptr<LayoutPositionToken> right);

 private:
  static qsizetype nodeSize(const LayoutPositionToken* node);
  static void apply(LayoutPositionToken* node, qreal delta);
  static void push(LayoutPositionToken* node);
  static void update(LayoutPositionToken* node);
  static std::pair<std::unique_ptr<LayoutPositionToken>, std::unique_ptr<LayoutPositionToken>> split(
      std::unique_ptr<LayoutPositionToken> root,
      qsizetype leftCount);
  quint32 nextPriority();

  std::unique_ptr<LayoutPositionToken> root_;
  quint32 randomState_ = 0x85ebca6bU;
};

}  // namespace muffin
