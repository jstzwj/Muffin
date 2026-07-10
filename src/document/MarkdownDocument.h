#pragma once

#include "document/LineStartOffsetCache.h"
#include "document/NodeIndex.h"
#include "document/OutlineBuilder.h"
#include "document/PieceTable.h"
#include "document/SourcePositionIndex.h"

#include <QObject>

namespace muffin {

class MarkdownDocument : public QObject {
  Q_OBJECT

public:
  explicit MarkdownDocument(QObject* parent = nullptr);

  MarkdownNode& root();
  const MarkdownNode& root() const;

  NodeIndex& index();
  const NodeIndex& index() const;

  // The whole document text as the zero-copy piece-table view. Callers that only need size/mid/at/
  // isEmpty/indexOf(QChar) read it directly (no materialization); callers needing a real QString
  // (save/export/find/countWords/regex) call .toString() and pay O(n) -- all occasional paths. The
  // per-keystroke edit path goes through replaceTopLevelRange, which mutates text_ directly.
  const PieceTable& markdownText() const { return text_; }
  const PieceTable& pieceText() const { return text_; }
  const LineStartOffsetCache& lineOffsets() const;
  void setMarkdownText(QString text, std::unique_ptr<MarkdownNode> root);
  void replaceTopLevelRange(
      qsizetype first,
      qsizetype count,
      std::vector<std::unique_ptr<MarkdownNode>> replacements,
      qsizetype sourceStart,
      qsizetype sourceEnd,
      const QString& replacementText);
  void shiftTopLevelSuffix(qsizetype first, qsizetype byteDelta, int lineDelta);
  QVector<OutlineEntry> outline() const;
  quint64 outlineRevision() const { return outlineRevision_; }

  quint64 revision() const;
  bool isModified() const;
  void setModified(bool modified);

  MarkdownNode* node(NodeId id) const;
  MarkdownNode* topLevelBlockAtOffset(qsizetype offset) const;
  void replaceRoot(std::unique_ptr<MarkdownNode> root);

signals:
  void documentReset();
  void modifiedChanged(bool modified);

private:
  void bindSourcePositionSlots();
  void rebuildOutlineIndex();
  void replaceOutlineRange(
      qsizetype first,
      qsizetype count,
      const std::vector<std::unique_ptr<MarkdownNode>>& replacements);

  PieceTable text_;  // the edit master: replaceTopLevelRange edits this (O(pieces)); sole source of truth
  LineStartOffsetCache lineOffsets_;
  std::unique_ptr<MarkdownNode> root_;
  NodeIndex index_;
  SourcePositionIndex sourcePositions_;
  QVector<OutlineEntry> outlineEntries_;
  quint64 outlineRevision_ = 0;
  quint64 revision_ = 0;
  bool modified_ = false;
};

}  // namespace muffin
