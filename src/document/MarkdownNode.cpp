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
  const qsizetype boundedIndex = std::clamp<qsizetype>(index, 0, static_cast<qsizetype>(children_.size()));
  children_.insert(children_.begin() + boundedIndex, std::move(child));
  relinkChildren();
  return *children_[boundedIndex];
}

std::unique_ptr<MarkdownNode> MarkdownNode::detachChild(qsizetype index) {
  if (index < 0 || index >= static_cast<qsizetype>(children_.size())) {
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
  const qsizetype count = static_cast<qsizetype>(children_.size());
  for (qsizetype i = 0; i < count; ++i) {
    children_[i]->parent_ = this;
    children_[i]->previous_ = i > 0 ? children_[i - 1].get() : nullptr;
    children_[i]->next_ = i + 1 < count ? children_[i + 1].get() : nullptr;
  }
}

QVector<InlineNode>& MarkdownNode::inlines() {
  return metadata_.inlines;
}

const QVector<InlineNode>& MarkdownNode::inlines() const {
  return metadata_.inlines;
}

QString MarkdownNode::literal() const {
  return metadata_.literal;
}

void MarkdownNode::setLiteral(QString text) {
  metadata_.literal = std::move(text);
}

int MarkdownNode::headingLevel() const {
  return metadata_.heading.level;
}

void MarkdownNode::setHeadingLevel(int level) {
  metadata_.heading.level = level;
}

bool MarkdownNode::setext() const {
  return metadata_.heading.setext;
}

void MarkdownNode::setSetext(bool setext) {
  metadata_.heading.setext = setext;
}

ListKind MarkdownNode::listKind() const {
  return metadata_.list.kind;
}

void MarkdownNode::setListKind(ListKind kind) {
  metadata_.list.kind = kind;
}

int MarkdownNode::listStart() const {
  return metadata_.list.start;
}

void MarkdownNode::setListStart(int start) {
  metadata_.list.start = start;
}

bool MarkdownNode::listTight() const {
  return metadata_.list.tight;
}

void MarkdownNode::setListTight(bool tight) {
  metadata_.list.tight = tight;
}

bool MarkdownNode::taskChecked() const {
  return metadata_.list.taskChecked;
}

void MarkdownNode::setTaskChecked(bool checked) {
  metadata_.list.taskChecked = checked;
}

bool MarkdownNode::isTaskItem() const {
  return metadata_.list.taskItem;
}

void MarkdownNode::setTaskItem(bool taskItem) {
  metadata_.list.taskItem = taskItem;
}

QString MarkdownNode::codeLanguage() const {
  return metadata_.code.language;
}

void MarkdownNode::setCodeLanguage(QString language) {
  metadata_.code.language = std::move(language);
}

bool MarkdownNode::isIndentedCode() const {
  return metadata_.code.indented;
}

void MarkdownNode::setIndentedCode(bool indented) {
  metadata_.code.indented = indented;
}

MathDelimiter MarkdownNode::mathDelimiter() const {
  return metadata_.mathDelimiter;
}

void MarkdownNode::setMathDelimiter(MathDelimiter delimiter) {
  metadata_.mathDelimiter = delimiter;
}

FrontMatterFormat MarkdownNode::frontMatterFormat() const {
  return metadata_.frontMatterFormat;
}

void MarkdownNode::setFrontMatterFormat(FrontMatterFormat format) {
  metadata_.frontMatterFormat = format;
}

DefinitionBlock MarkdownNode::definition() const {
  return metadata_.definition;
}

void MarkdownNode::setDefinition(DefinitionBlock definition) {
  metadata_.definition = std::move(definition);
}

QVector<TableAlignment> MarkdownNode::tableAlignments() const {
  return metadata_.table.alignments;
}

void MarkdownNode::setTableAlignments(QVector<TableAlignment> alignments) {
  metadata_.table.alignments = std::move(alignments);
}

bool MarkdownNode::tableRowIsHeader() const {
  return metadata_.table.rowIsHeader;
}

void MarkdownNode::setTableRowIsHeader(bool header) {
  metadata_.table.rowIsHeader = header;
}

SourceRange MarkdownNode::sourceRange() const {
  return metadata_.sourceRange;
}

void MarkdownNode::setSourceRange(SourceRange range) {
  metadata_.sourceRange = std::move(range);
}

std::unique_ptr<MarkdownNode> MarkdownNode::clone(CloneMode mode) const {
  auto copy = std::make_unique<MarkdownNode>(
      type_, mode == CloneMode::PreserveIds ? id_ : NodeId::create());
  // A single aggregate copy carries every domain field, so adding a field can no longer be
  // silently dropped here (the flat layout previously let clone() miss taskItem_).
  copy->metadata_ = metadata_;
  for (const auto& child : children_) {
    copy->appendChild(child->clone(mode));
  }
  return copy;
}

}  // namespace muffin
