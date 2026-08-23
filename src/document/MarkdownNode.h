#pragma once

#include "document/InlineNode.h"
#include "document/DefinitionBlock.h"
#include "document/NodeId.h"
#include "document/SourceRange.h"

#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace muffin {

class MarkdownDocument;
class SourcePositionIndex;
struct SourcePositionToken;

class MarkdownNode {
public:
  explicit MarkdownNode(BlockType type, NodeId id = NodeId::create());
  MarkdownNode(const MarkdownNode&) = delete;
  MarkdownNode& operator=(const MarkdownNode&) = delete;
  MarkdownNode(MarkdownNode&&) = default;
  MarkdownNode& operator=(MarkdownNode&&) = default;

  NodeId id() const;
  void setId(NodeId id);
  BlockType type() const;
  void setType(BlockType type);

  MarkdownNode* parent() const;
  MarkdownNode* previousSibling() const;
  MarkdownNode* nextSibling() const;
  // Zero-based CSS sibling ordinals. Direct document children use the persistent top-level
  // order index (O(log n)); nested block-local sibling groups use their short linked lists.
  int siblingIndex() const;
  int siblingTypeIndex() const;

  std::vector<std::unique_ptr<MarkdownNode>>& children();
  const std::vector<std::unique_ptr<MarkdownNode>>& children() const;

  MarkdownNode& appendChild(std::unique_ptr<MarkdownNode> child);
  MarkdownNode& insertChild(qsizetype index, std::unique_ptr<MarkdownNode> child);
  std::unique_ptr<MarkdownNode> detachChild(qsizetype index);
  void clearChildren();

  QVector<InlineNode>& inlines();
  const QVector<InlineNode>& inlines() const;

  QString literal() const;
  void setLiteral(QString text);

  int headingLevel() const;
  void setHeadingLevel(int level);
  // True when the heading was authored in Setext style (text followed by an
  // `===`/`---` underline) rather than ATX (`# `). Setext underlines only apply
  // to levels 1-2; levels 3-6 are always ATX.
  bool setext() const;
  void setSetext(bool setext);

  ListKind listKind() const;
  void setListKind(ListKind kind);
  int listStart() const;
  void setListStart(int start);
  bool listTight() const;
  void setListTight(bool tight);
  bool taskChecked() const;
  void setTaskChecked(bool checked);
  // True only for genuine task-list items (GFM `- [ ]`/`- [x]`). A plain bullet
  // and an unchecked task item both report taskChecked()==false, so this flag is
  // the only way to tell them apart and round-trip the checkbox faithfully.
  bool isTaskItem() const;
  void setTaskItem(bool taskItem);

  QString codeLanguage() const;
  void setCodeLanguage(QString language);
  // True for an indented (4-space) code block as opposed to a fenced (```/~~~) one.
  // cmark collapses both into CMARK_NODE_CODE_BLOCK, so the original style must be
  // recorded at parse time to round-trip edits without rewriting indented code as fenced.
  bool isIndentedCode() const;
  void setIndentedCode(bool indented);

  MathDelimiter mathDelimiter() const;
  void setMathDelimiter(MathDelimiter delimiter);

  // GitHub-style alert kind when this blockquote's first line is `[!NOTE]`/`[!TIP]`/...; None for a
  // plain quote. Annotated in a post-parse pass so the renderer can draw a themed card.
  AlertKind alertKind() const;
  void setAlertKind(AlertKind kind);

  FrontMatterFormat frontMatterFormat() const;
  void setFrontMatterFormat(FrontMatterFormat format);

  DefinitionBlock definition() const;
  void setDefinition(DefinitionBlock definition);

  QVector<TableAlignment> tableAlignments() const;
  void setTableAlignments(QVector<TableAlignment> alignments);
  bool tableRowIsHeader() const;
  void setTableRowIsHeader(bool header);

  SourceRange sourceRange() const;
  // Byte delta converting an offset stored on inline nodes of THIS subtree between their storage
  // frame and ABSOLUTE document coordinates. All inlines under a top-level block (including its
  // own) share one frame: ABSOLUTE before the block is relativized (parser/demote windows),
  // relative to the owning top-level block's resolved byteStart after — the SAME conditional
  // semantics as sourceRange(). Single-sourced so inline consumers stop hand-rolling
  // `topLevelBlock()->sourceRange().byteStart`, which is wrong in the pre-relativize window
  // (it adds a base to offsets that are already absolute).
  qsizetype inlineAbsoluteDelta() const;
  void setSourceRange(SourceRange range);
  // Shift this node's OWN stored sourceRange in place by (byteDelta, lineDelta). Used by the
  // per-keystroke suffix shift on top-level blocks (whose stored range is absolute): mutates
  // metadata_ directly, avoiding the copy-out / copy-in of the SourceRange struct that
  // sourceRange()/setSourceRange() would do for every block in the suffix.
  void shiftOwnSourceRange(qsizetype byteDelta, int lineDelta);

  // --- Block-relative offset model ------------------------------------------
  // Descendant offsets (child block SourceRange, InlineNode ranges, DefinitionBlock fields, and
  // the top-level block's OWN definition fields) are stored RELATIVE to the owning top-level
  // block's sourceRange().byteStart (bytes) / .lineStart (lines). The top-level block's own
  // sourceRange stays ABSOLUTE. sourceRange()/definition() resolve to absolute on read.
  // Owning top-level block (the node whose parent is the document root; returns this if already
  // top-level or this is the root). O(1) after the first call: memoized, since a live node's
  // top-level block never changes (within-block surgery preserves it; cross-block edits destroy +
  // re-parse rather than reparent). The relativize walk seeds the cache for the whole subtree.
  const MarkdownNode* topLevelBlock() const;
  // Relativize this top-level block's subtree (its own sourceRange is left absolute). Call once
  // after the subtree is built with absolute offsets. The base is read from this block's OWN
  // current byteStart, so for a slice-assembled block call BEFORE adding slice.sourceStart to it.
  void relativizeDescendants();

  // Replace inline-owned strings that exactly match immutable source slices with shared views.
  // The source coordinates are captured now, before block-relative offset conversion.
  qsizetype bindSharedInlineText(const std::shared_ptr<const QString>& source);

  std::unique_ptr<MarkdownNode> clone(CloneMode mode = CloneMode::PreserveIds) const;

  // Generic subtree traversal, replacing hand-written recursive walks scattered across the
  // editor/parser/render code. Depth-first preorder; visits descendants of *this (not *this).
  template <typename F>
  void forEachDescendant(F&& fn) {
    for (const auto& child : children_) {
      fn(*child);
      child->forEachDescendant(fn);
    }
  }
  template <typename F>
  void forEachDescendant(F&& fn) const {
    for (const auto& child : children_) {
      fn(*child);
      child->forEachDescendant(fn);
    }
  }

  // First descendant (DFS preorder) for which pred(*node) is true, or nullptr.
  template <typename F>
  MarkdownNode* findDescendant(F&& pred) {
    for (const auto& child : children_) {
      if (pred(*child)) return child.get();
      if (MarkdownNode* found = child->findDescendant(pred)) return found;
    }
    return nullptr;
  }
  template <typename F>
  const MarkdownNode* findDescendant(F&& pred) const {
    for (const auto& child : children_) {
      if (pred(*child)) return child.get();
      if (const MarkdownNode* found = child->findDescendant(pred)) return found;
    }
    return nullptr;
  }

  MarkdownNode* firstChildByType(BlockType type) {
    for (const auto& child : children_) {
      if (child->type() == type) return child.get();
    }
    return nullptr;
  }
  const MarkdownNode* firstChildByType(BlockType type) const {
    for (const auto& child : children_) {
      if (child->type() == type) return child.get();
    }
    return nullptr;
  }

  MarkdownNode* findDescendantByType(BlockType type) {
    return findDescendant([type](const MarkdownNode& node) { return node.type() == type; });
  }
  const MarkdownNode* findDescendantByType(BlockType type) const {
    return findDescendant([type](const MarkdownNode& node) { return node.type() == type; });
  }

private:
  friend class MarkdownDocument;

  // Code fences, tables, and definitions are a minority of block nodes. Their owning Qt handles
  // live in one payload allocated only when one of those block kinds needs it.
  struct ExtendedMetadata {
    ExtendedMetadata() = default;
    ExtendedMetadata(const ExtendedMetadata&);
    ExtendedMetadata& operator=(const ExtendedMetadata&);
    ExtendedMetadata(ExtendedMetadata&&) noexcept = default;
    ExtendedMetadata& operator=(ExtendedMetadata&&) noexcept = default;

    QString codeLanguage;
    QVector<TableAlignment> tableAlignments;
    std::unique_ptr<DefinitionBlock> definition;

    bool isEmpty() const;
  };

  // Type-specific scalar values still have independent accessor semantics, but only need one word.
  struct BlockFlags {
    quint32 setext : 1 = 0;
    quint32 listKind : 2 = 0;
    quint32 listTight : 1 = 0;
    quint32 taskChecked : 1 = 0;
    quint32 taskItem : 1 = 0;
    quint32 indentedCode : 1 = 0;
    quint32 alertKind : 3 = 0;
    quint32 mathDelimiter : 1 = 0;
    quint32 frontMatterFormat : 2 = 0;
    quint32 tableRowIsHeader : 1 = 0;
    quint32 offsetsRelative : 1 = 0;
  };
  // All per-block domain state in one copyable aggregate, so clone() copies it in a single
  // assignment and adding a field can never be silently dropped — the flat layout previously let
  // clone() miss taskItem_, which shipped as a round-trip bug.
  struct BlockMetadata {
    // The lazy payload owns a DefinitionBlock, so this copy is defined out of line and deep-copies
    // both ownership layers. Keep the field list in sync with MarkdownNode.cpp.
    BlockMetadata() = default;
    BlockMetadata(const BlockMetadata&);
    BlockMetadata& operator=(const BlockMetadata&);
    BlockMetadata(BlockMetadata&&) noexcept = default;
    BlockMetadata& operator=(BlockMetadata&&) noexcept = default;
    QVector<InlineNode> inlines;
    QString literal;
    SourceRange sourceRange;
    std::unique_ptr<ExtendedMetadata> extended;
    int headingLevel = 0;
    int listStart = 1;
    BlockFlags flags;
    // Set on a top-level block by relativizeDescendants() once its subtree's offsets have been
    // converted to block-relative. Lives IN this aggregate on purpose: clone() copies metadata_ in
    // a single assignment, so a standalone member would be silently dropped (it once was), leaving a
    // cloned subtree with relative-stored offsets but the flag false — accessors would then return
    // unresolved relative values. Only the owning top-level block's flag is ever read (descendants
    // read it via topLevelBlock()->metadata_); their own flag stays false and is unused.
  };

  ExtendedMetadata& ensureExtendedMetadata();
  void pruneExtendedMetadata();

  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  MarkdownNode* parent_ = nullptr;
  MarkdownNode* previous_ = nullptr;
  MarkdownNode* next_ = nullptr;
  std::vector<std::unique_ptr<MarkdownNode>> children_;
  BlockMetadata metadata_;
  // Cached owning top-level block (the node whose parent is the document root). A LIVE node's
  // top-level block never changes: within-block surgery (e.g. table cell moves) keeps the same
  // top-level ancestor, and cross-block edits replace subtrees via destroy + re-parse rather than
  // reparenting. So a value computed once stays valid for the node's lifetime, letting
  // sourceRange()/definition() resolve in O(1) instead of climbing parent_ on every call. null
  // defers to the parent_ climb (lazy memoization) — the relativize walk seeds it for free.
  // mutable: set by the const topLevelBlock() accessor. NOT in metadata_: a clone's top-level
  // differs from the original's, so clone() must leave it null (lazy recompute).
  mutable const MarkdownNode* topLevelCache_ = nullptr;

  // On the document root this stores the owning position index; on a direct root child it stores
  // that block's slot. Descendants use neither. The role-dependent union keeps the lazy source
  // position binding to one machine word per block node.
  union {
    SourcePositionIndex* sourcePositionIndex_;
    SourcePositionToken* sourcePositionToken_;
  };

  // Block-relative offset helpers (see public relativizeDescendants).
  void relativizeNodeAndDescendants(const MarkdownNode* topLevel, qsizetype byteBase, int lineBase);
  static void subtractDefinitionFields(DefinitionBlock& def, qsizetype byteBase);
  qsizetype bindSharedInlineTextRecursive(const std::shared_ptr<const QString>& source);
  quint8 siblingKind() const;
};

}  // namespace muffin
