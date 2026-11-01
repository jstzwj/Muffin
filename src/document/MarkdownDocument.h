#pragma once

#include "document/LineStartOffsetCache.h"
#include "document/NodeIndex.h"
#include "document/OutlineBuilder.h"
#include "document/PieceTable.h"
#include "document/SourcePositionIndex.h"
#include "Export.h"

#include <QObject>

namespace muffin {

class MUFFIN_CORE_EXPORT MarkdownDocument : public QObject {
  Q_OBJECT

public:
  // Plain data with stable node/token addresses, prepared off the GUI thread.
  // No QObject, font, image or layout resource is transferred between threads.
  struct PreparedState {
    PieceTable text;
    std::unique_ptr<MarkdownNode> root;
    NodeIndex index;
    SourcePositionIndex positions;
    QVector<OutlineEntry> outline;
  };
  static std::unique_ptr<PreparedState> prepareState(PieceTable text, std::unique_ptr<MarkdownNode> root);
  // Returns the retired generation. Its pure data may be reclaimed on a worker
  // after synchronous document observers have detached from it.
  std::unique_ptr<PreparedState> installState(std::unique_ptr<PreparedState> state, bool publish = true);
  void publishPreparedState() { emit documentReset(); }
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
  // Install a worker-prepared text index without scanning the original text again.
  void setPreparedMarkdownText(PieceTable text, std::unique_ptr<MarkdownNode> root);
  void replaceTopLevelRange(qsizetype first, qsizetype count, std::vector<std::unique_ptr<MarkdownNode>> replacements,
                            qsizetype sourceStart, qsizetype sourceEnd, const QString& replacementText);
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
  void refreshRootSourceRange();
  void rebuildOutlineIndex();
  void replaceOutlineRange(qsizetype first, qsizetype count,
                           const std::vector<std::unique_ptr<MarkdownNode>>& replacements);

  PieceTable text_; // the edit master: replaceTopLevelRange edits this (O(pieces)); sole source of truth
  LineStartOffsetCache lineOffsets_;
  std::unique_ptr<MarkdownNode> root_;
  NodeIndex index_;
  SourcePositionIndex sourcePositions_;
  mutable QVector<OutlineEntry> outlineEntries_;
  mutable quint64 resolvedOutlineRevision_ = quint64(-1);
  quint64 outlineRevision_ = 0;
  quint64 revision_ = 0;
  bool modified_ = false;
};

} // namespace muffin
