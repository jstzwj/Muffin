#pragma once

#include "document/InlineNode.h"
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
  ListKind listKind_ = ListKind::None;
  int listStart_ = 1;
  bool listTight_ = false;
  bool taskChecked_ = false;
  QString codeLanguage_;
  QVector<TableAlignment> tableAlignments_;
  bool tableRowIsHeader_ = false;
  SourceRange sourceRange_;
};

}  // namespace muffin
