#pragma once

#include "document/MarkdownDocument.h"
#include "document/TopLevelRangeChange.h"
#include "editor/CursorPosition.h"
#include "render/BlockLayout.h"
#include "render/BlockLayoutBuilder.h"
#include "theme/RenderTheme.h"

#include <QHash>
#include <QPair>
#include <QRectF>
#include <QVector>

#include <memory>
#include <vector>

namespace muffin {

class CodeFenceScrollController;

class DocumentLayout {
 public:
  // Eager (default) builds every block's full detail up front — today's behavior, used by
  // tests and print. Lazy builds only cheap estimated heights up front and promotes blocks to
  // full detail on demand (ensureBuilt) as they scroll into view.
  enum class BuildPolicy { Eager, Lazy };

  struct BlockRebuildResult {
    bool rebuilt = false;
    NodeId blockId;
    QRectF oldRect;
    QRectF newRect;
    QRectF shiftedRect;
    qreal heightDelta = 0;
  };

  struct RangeRebuildResult {
    bool rebuilt = false;
    qsizetype first = -1;
    qsizetype oldCount = 0;
    qsizetype newCount = 0;
    QRectF oldRect;
    QRectF newRect;
    QRectF shiftedRect;
    qreal heightDelta = 0;
  };

  // One entry per top-level document child. Always present; detail is null until the slot
  // is promoted (built). top/height are estimated until measured==true.
  struct BlockSlot {
    NodeId nodeId;
    BlockType type = BlockType::Unknown;
    qreal top = 0.0;
    qreal height = 0.0;
    bool measured = false;
    std::unique_ptr<BlockLayout> detail;
  };

  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, QString documentPath = {});
  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection, QString documentPath = {});
  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection, QString documentPath, BuildPolicy policy);
  void setEditingHtmlBlock(NodeId id);
  // Per-code-fence horizontal scroll state; forwarded to the builder (writes longest-line width)
  // and to BlockLayout hit-test (reads the offset). Owned by EditorController.
  void setCodeFenceScroll(CodeFenceScrollController* controller);
  // Forward the active IME composition to the builder (spliced into the caret block's inline layout).
  void setPreedit(QString text, QVector<QTextLayout::FormatRange> formats, int cursor);
  CodeFenceScrollController* codeFenceScroll() const;
  bool relayoutForViewportWidth(const RenderTheme& theme, qreal viewportWidth);
  BlockRebuildResult rebuildBlock(NodeId blockId, const MarkdownDocument& document, const RenderTheme& theme, SelectionRange selection);
  RangeRebuildResult rebuildTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document, const RenderTheme& theme, SelectionRange selection);

  qreal pageLeft() const;   // content left
  qreal pageWidth() const;  // content width
  qreal pageOuterLeft() const;
  qreal pageOuterWidth() const;
  QRectF pageRect(const RenderTheme& theme, qreal viewportHeight = 0) const;
  qreal totalHeight() const;
  BuildPolicy buildPolicy() const;

  // Slot access (work for both policies; promote-on-demand where detail is needed).
  qsizetype slotCount() const;
  qreal slotTop(qsizetype index) const;
  qreal slotHeight(qsizetype index) const;
  NodeId slotNodeId(qsizetype index) const;
  qsizetype topLevelIndexFor(NodeId id) const;  // slot index of the top-level block owning id, or -1
  // Inclusive [first,last] slot range whose vertical extent overlaps [yTop,yBottom]; returns
  // {-1,-1} when nothing overlaps (e.g. empty document).
  QPair<qsizetype, qsizetype> slotRangeOverlappingY(qreal yTop, qreal yBottom) const;
  void ensureBuilt(qsizetype first, qsizetype last, const RenderTheme& theme);  // promote null-detail slots in [first,last]
  void buildAll(const RenderTheme& theme);  // promote every slot (print)
  QVector<NodeId> promotedTopLevelIds() const;  // slots currently holding full detail (spell-overlay refresh)

  // Geometry for the always-present virtual trailing empty paragraph below the
  // last block. The caret on this line is the click target for "append a new
  // paragraph"; typing materializes it. Shared by hit-test and cursor rebuild so
  // the trailing caret stays consistent across layout rebuilds.
  static QRectF trailingParagraphCursorRect(const BlockLayout& lastBlock, const RenderTheme& theme, qreal pageLeft);
  static qreal trailingSpaceForVirtualParagraph(const RenderTheme& theme);

  QVector<const BlockLayout*> promotedBlocks() const;  // promoted slot details, in order
  QVector<const BlockLayout*> visibleBlocks(QRectF documentViewport, const RenderTheme& theme);
  const BlockLayout* block(NodeId id, const RenderTheme& theme);  // promotes on demand
  const BlockLayout* block(NodeId id) const;                      // convenience: already-built block, no promotion (tests / read-only UI)
  const BlockLayout* blockIfPromoted(NodeId id) const;            // no promotion; may be null
  NodeId topLevelBlockIdFor(NodeId id) const;
  const BlockLayout* blockAt(QPointF documentPos, const RenderTheme& theme);
  HitTestResult hitTest(QPointF documentPos, const RenderTheme& theme);

 private:
  const MarkdownNode* topLevelBlockFor(NodeId id, const MarkdownDocument& document) const;
  void indexLayoutBlock(const BlockLayout& block);
  void removeLayoutIndexFor(const BlockLayout& block);
  void buildNestedIndex(const MarkdownDocument& document);
  void collectNestedToTopLevel(const MarkdownNode& node, NodeId topLevelId);
  // Walk the AST in document order and resolve each heading's ::before counter()
  // content (e.g. `counter(h1) ". "` → "1. ") against a live counter state machine,
  // storing the result keyed by NodeId. No-op for themes without heading counters.
  void recomputeHeadingCounters(const MarkdownDocument& document, const RenderTheme& theme);
  void rebuildTops();

  void configureBuilder(SelectionRange selection);
  qreal promoteSlot(qsizetype index, const RenderTheme& theme);  // returns height delta
  void shiftSuffixFrom(qsizetype index, qreal delta);
  void recomputeTotalHeight(const RenderTheme& theme);
  qsizetype slotIndexAtY(qreal y) const;  // first slot whose top+height > y

  const MarkdownDocument* document_ = nullptr;
  QString documentPath_;
  NodeId editingHtmlBlockId_;
  CodeFenceScrollController* codeFenceScroll_ = nullptr;
  SelectionRange selection_;
  QString preeditText_;
  QVector<QTextLayout::FormatRange> preeditFormats_;
  int preeditCursor_ = -1;
  qreal viewportWidth_ = 0;
  BuildPolicy buildPolicy_ = BuildPolicy::Eager;

  std::vector<BlockSlot> slots_;
  std::vector<qreal> tops_;                         // slots_[i].top mirror, for binary search
  QHash<NodeId, qsizetype> topLevelIndex_;          // top-level node id -> slot index
  QHash<NodeId, NodeId> nestedToTopLevel_;          // any node id -> top-level node id
  QHash<NodeId, QString> headingCounterText_;       // heading node id -> resolved ::before counter text ("1. ")
  QHash<NodeId, const BlockLayout*> layoutIndex_;   // node id -> built BlockLayout* (lazy-populated)

  qreal pageLeft_ = 0;       // content left
  qreal pageWidth_ = 0;      // content width
  qreal pageOuterLeft_ = 0;  // #write card left
  qreal pageOuterWidth_ = 0; // #write card width
  qreal totalHeight_ = 0;
  BlockLayoutBuilder builder_;
};

}  // namespace muffin
