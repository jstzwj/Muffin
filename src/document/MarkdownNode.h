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
  // All per-block domain state in one copyable aggregate, so clone() copies it in a single
  // assignment and adding a field can never be silently dropped — the flat layout previously let
  // clone() miss taskItem_, which shipped as a round-trip bug.
  struct BlockMetadata {
    QVector<InlineNode> inlines;
    QString literal;
    HeadingInfo heading;
    ListInfo list;
    CodeInfo code;
    TableInfo table;
    MathDelimiter mathDelimiter = MathDelimiter::Dollar;
    FrontMatterFormat frontMatterFormat = FrontMatterFormat::None;
    DefinitionBlock definition;
    SourceRange sourceRange;
  };

  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  MarkdownNode* parent_ = nullptr;
  MarkdownNode* previous_ = nullptr;
  MarkdownNode* next_ = nullptr;
  std::vector<std::unique_ptr<MarkdownNode>> children_;
  BlockMetadata metadata_;
};

}  // namespace muffin
