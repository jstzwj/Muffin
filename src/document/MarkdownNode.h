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
  void relinkChildren();

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

private:
  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  MarkdownNode* parent_ = nullptr;
  MarkdownNode* previous_ = nullptr;
  MarkdownNode* next_ = nullptr;
  std::vector<std::unique_ptr<MarkdownNode>> children_;
  QVector<InlineNode> inlines_;
  QString literal_;
  int headingLevel_ = 0;
  bool setext_ = false;
  ListKind listKind_ = ListKind::None;
  int listStart_ = 1;
  bool listTight_ = false;
  bool taskChecked_ = false;
  QString codeLanguage_;
  bool codeIndented_ = false;
  MathDelimiter mathDelimiter_ = MathDelimiter::Dollar;
  FrontMatterFormat frontMatterFormat_ = FrontMatterFormat::None;
  DefinitionBlock definition_;
  QVector<TableAlignment> tableAlignments_;
  bool tableRowIsHeader_ = false;
  SourceRange sourceRange_;
};

}  // namespace muffin
