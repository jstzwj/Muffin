#pragma once

#include "document/NodeId.h"
#include "document/PieceTable.h"
#include "document/SourceTextChange.h"
#include "Export.h"
#include <QHash>

namespace muffin {
class MarkdownDocument;
struct BlockTarget {
  NodeId id;
  quint64 revision = 0;
  int index = -1;
  NodeId parent; // Invalid for document-level units; otherwise a direct list scope.
  bool isValid() const { return id.isValid(); }
};
struct BlockSourceUnit {
  NodeId id;
  qsizetype start = 0, contentEnd = 0, end = 0;
  int first = 0, last = 0;
  bool movable = true;
};
struct BlockSourcePlan {
  PieceTable text;
  SourceTextChange change;
  qsizetype cursor = 0;
  bool valid = false;
};
// Revision-local structural units, borrowing no node pointers. Source ranges
// include whole lists/quotes and paired HTML containers with their closing tags.
class MUFFIN_CORE_EXPORT BlockStructure {
public:
  explicit BlockStructure(const MarkdownDocument& document);
  quint64 revision() const { return revision_; }
  const QVector<BlockSourceUnit>& units() const { return units_; }
  int indexFor(const MarkdownDocument& document, NodeId id) const;
  const BlockSourceUnit* resolve(BlockTarget target) const;
  BlockTarget targetAt(int index) const;
  BlockTarget targetFor(NodeId id) const;
  BlockTarget targetInScope(NodeId id, BlockTarget scope) const;
  BlockTarget sibling(BlockTarget target, int direction) const;
  bool canMove(BlockTarget source, BlockTarget destination, bool after) const;
  QString closingSuffix(const PieceTable& text, BlockTarget target) const;
  BlockSourcePlan move(const PieceTable& text, BlockTarget source, BlockTarget destination, bool after,
                       qsizetype cursorOffset = -1, bool sequential = true) const;
  BlockSourcePlan remove(const PieceTable& text, BlockTarget target, bool sequential = true) const;
  BlockSourcePlan duplicate(const PieceTable& text, BlockTarget target, bool sequential = true) const;
  BlockSourcePlan insertEmptyListItem(const PieceTable& text, BlockTarget target, bool after,
                                      bool sequential = true) const;
  BlockSourcePlan insert(const PieceTable& text, BlockTarget target, bool after, QString content) const;

private:
  struct ListScope {
    QVector<BlockSourceUnit> units;
    QHash<NodeId, int> indices;
    bool valid = true;
  };
  enum class ListOperation { Move, Remove, Duplicate, Insert };
  const ListScope* listScope(NodeId id) const;
  const QVector<BlockSourceUnit>* siblings(BlockTarget target) const;
  BlockSourcePlan listPlan(const PieceTable& text, BlockTarget source, BlockTarget destination, bool after,
                           ListOperation operation, bool sequential, qsizetype cursor = -1) const;
  const MarkdownDocument& document_;
  mutable QHash<NodeId, ListScope> lists_;
  QVector<BlockSourceUnit> units_;
  quint64 revision_ = 0;
};
} // namespace muffin
Q_DECLARE_METATYPE(muffin::BlockTarget)
