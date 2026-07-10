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
  struct HeadingInfo {
    int level = 0;
    bool setext = false;
  };
  struct ListInfo {
    ListKind kind = ListKind::None;
    int start = 1;
    bool tight = false;
    bool taskChecked = false;
    bool taskItem = false;
  };
  struct CodeInfo {
    QString language;
    bool indented = false;
  };
  struct TableInfo {
    QVector<TableAlignment> alignments;
    bool rowIsHeader = false;
  };
  struct QuoteInfo {
    AlertKind alertKind = AlertKind::None;
  };
  // All per-block domain state in one copyable aggregate, so clone() copies it in a single
  // assignment and adding a field can never be silently dropped — the flat layout previously let
  // clone() miss taskItem_, which shipped as a round-trip bug.
  struct BlockMetadata {
    // Custom copy: the domain fields above copy trivially, but `definition` is a unique_ptr that
    // must deep-copy. Defined out-of-line in MarkdownNode.cpp; keep the field list in sync there
    // when adding a field (the old flat-layout taskItem_ bug bit us once).
    BlockMetadata() = default;
    BlockMetadata(const BlockMetadata&);
    BlockMetadata& operator=(const BlockMetadata&);
    QVector<InlineNode> inlines;
    QString literal;
    HeadingInfo heading;
    ListInfo list;
    CodeInfo code;
    TableInfo table;
    QuoteInfo quote;
    MathDelimiter mathDelimiter = MathDelimiter::Dollar;
    FrontMatterFormat frontMatterFormat = FrontMatterFormat::None;
    // Null for ~99.99% of blocks (only link/footnote definitions allocate). Heap-allocated on
    // demand instead of inlined 160B in every block node — saves ~152B × every block.
    std::unique_ptr<DefinitionBlock> definition;
    SourceRange sourceRange;
    // Set on a top-level block by relativizeDescendants() once its subtree's offsets have been
    // converted to block-relative. Lives IN this aggregate on purpose: clone() copies metadata_ in
    // a single assignment, so a standalone member would be silently dropped (it once was), leaving a
    // cloned subtree with relative-stored offsets but the flag false — accessors would then return
    // unresolved relative values. Only the owning top-level block's flag is ever read (descendants
    // read it via topLevelBlock()->metadata_); their own copy stays false and is unused.
    bool offsetsRelative = false;
  };

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

  // Block-relative offset helpers (see public relativizeDescendants).
  void relativizeNodeAndDescendants(const MarkdownNode* topLevel, qsizetype byteBase, int lineBase);
  static void subtractDefinitionFields(DefinitionBlock& def, qsizetype byteBase);
  qsizetype bindSharedInlineTextRecursive(const std::shared_ptr<const QString>& source);
};

}  // namespace muffin
