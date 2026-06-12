#include "document/MarkdownNode.h"

#include <algorithm>
#include <utility>

namespace muffin {

MarkdownNode::MarkdownNode(BlockType type, NodeId id)
    : id_(std::move(id)), type_(type) {}

NodeId MarkdownNode::id() const {
  return id_;
}

void MarkdownNode::setId(NodeId id) {
  id_ = std::move(id);
}

BlockType MarkdownNode::type() const {
  return type_;
}

void MarkdownNode::setType(BlockType type) {
  type_ = type;
}

MarkdownNode* MarkdownNode::parent() const {
  return parent_;
}

MarkdownNode* MarkdownNode::previousSibling() const {
  return previous_;
}

MarkdownNode* MarkdownNode::nextSibling() const {
  return next_;
}

std::vector<std::unique_ptr<MarkdownNode>>& MarkdownNode::children() {
  return children_;
}

const std::vector<std::unique_ptr<MarkdownNode>>& MarkdownNode::children() const {
  return children_;
}

MarkdownNode& MarkdownNode::appendChild(std::unique_ptr<MarkdownNode> child) {
  return insertChild(children_.size(), std::move(child));
}

MarkdownNode& MarkdownNode::insertChild(qsizetype index, std::unique_ptr<MarkdownNode> child) {
  child->parent_ = this;
  const qsizetype boundedIndex = std::clamp<qsizetype>(index, 0, children_.size());
  children_.insert(children_.begin() + boundedIndex, std::move(child));
  relinkChildren();
  return *children_[boundedIndex];
}

std::unique_ptr<MarkdownNode> MarkdownNode::detachChild(qsizetype index) {
  if (index < 0 || index >= children_.size()) {
    return nullptr;
  }

  auto child = std::move(children_[index]);
  children_.erase(children_.begin() + index);
  child->parent_ = nullptr;
  child->previous_ = nullptr;
  child->next_ = nullptr;
  relinkChildren();
  return child;
}

void MarkdownNode::clearChildren() {
  for (auto& child : children_) {
    child->parent_ = nullptr;
    child->previous_ = nullptr;
    child->next_ = nullptr;
  }
  children_.clear();
}

void MarkdownNode::relinkChildren() {
  for (qsizetype i = 0; i < children_.size(); ++i) {
    children_[i]->parent_ = this;
    children_[i]->previous_ = i > 0 ? children_[i - 1].get() : nullptr;
    children_[i]->next_ = i + 1 < children_.size() ? children_[i + 1].get() : nullptr;
  }
}

QVector<InlineNode>& MarkdownNode::inlines() {
  return inlines_;
}

const QVector<InlineNode>& MarkdownNode::inlines() const {
  return inlines_;
}

QString MarkdownNode::literal() const {
  return literal_;
}

void MarkdownNode::setLiteral(QString text) {
  literal_ = std::move(text);
}

int MarkdownNode::headingLevel() const {
  return headingLevel_;
}

void MarkdownNode::setHeadingLevel(int level) {
  headingLevel_ = level;
}

bool MarkdownNode::setext() const {
  return setext_;
}

void MarkdownNode::setSetext(bool setext) {
  setext_ = setext;
}

ListKind MarkdownNode::listKind() const {
  return listKind_;
}

void MarkdownNode::setListKind(ListKind kind) {
  listKind_ = kind;
}

int MarkdownNode::listStart() const {
  return listStart_;
}

void MarkdownNode::setListStart(int start) {
  listStart_ = start;
}

bool MarkdownNode::listTight() const {
  return listTight_;
}

void MarkdownNode::setListTight(bool tight) {
  listTight_ = tight;
}

bool MarkdownNode::taskChecked() const {
  return taskChecked_;
}

void MarkdownNode::setTaskChecked(bool checked) {
  taskChecked_ = checked;
}

QString MarkdownNode::codeLanguage() const {
  return codeLanguage_;
}

void MarkdownNode::setCodeLanguage(QString language) {
  codeLanguage_ = std::move(language);
}

MathDelimiter MarkdownNode::mathDelimiter() const {
  return mathDelimiter_;
}

void MarkdownNode::setMathDelimiter(MathDelimiter delimiter) {
  mathDelimiter_ = delimiter;
}

FrontMatterFormat MarkdownNode::frontMatterFormat() const {
  return frontMatterFormat_;
}

void MarkdownNode::setFrontMatterFormat(FrontMatterFormat format) {
  frontMatterFormat_ = format;
}

DefinitionBlock MarkdownNode::definition() const {
  return definition_;
}

void MarkdownNode::setDefinition(DefinitionBlock definition) {
  definition_ = std::move(definition);
}

QVector<TableAlignment> MarkdownNode::tableAlignments() const {
  return tableAlignments_;
}

void MarkdownNode::setTableAlignments(QVector<TableAlignment> alignments) {
  tableAlignments_ = std::move(alignments);
}

bool MarkdownNode::tableRowIsHeader() const {
  return tableRowIsHeader_;
}

void MarkdownNode::setTableRowIsHeader(bool header) {
  tableRowIsHeader_ = header;
}

SourceRange MarkdownNode::sourceRange() const {
  return sourceRange_;
}

void MarkdownNode::setSourceRange(SourceRange range) {
  sourceRange_ = range;
}

std::unique_ptr<MarkdownNode> MarkdownNode::clone(CloneMode mode) const {
  auto copy = std::make_unique<MarkdownNode>(
      type_, mode == CloneMode::PreserveIds ? id_ : NodeId::create());
  copy->inlines_ = inlines_;
  copy->literal_ = literal_;
  copy->headingLevel_ = headingLevel_;
  copy->setext_ = setext_;
  copy->listKind_ = listKind_;
  copy->listStart_ = listStart_;
  copy->listTight_ = listTight_;
  copy->taskChecked_ = taskChecked_;
  copy->codeLanguage_ = codeLanguage_;
  copy->mathDelimiter_ = mathDelimiter_;
  copy->frontMatterFormat_ = frontMatterFormat_;
  copy->definition_ = definition_;
  copy->tableAlignments_ = tableAlignments_;
  copy->tableRowIsHeader_ = tableRowIsHeader_;
  copy->sourceRange_ = sourceRange_;

  for (const auto& child : children_) {
    copy->appendChild(child->clone(mode));
  }
  return copy;
}

}  // namespace muffin
