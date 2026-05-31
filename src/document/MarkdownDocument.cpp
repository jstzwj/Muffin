#include "document/MarkdownDocument.h"

#include <utility>

namespace muffin {

MarkdownDocument::MarkdownDocument(QObject* parent)
    : QObject(parent), root_(std::make_unique<MarkdownNode>(BlockType::Document)) {
  index_.rebuild(*root_);
}

MarkdownNode& MarkdownDocument::root() {
  return *root_;
}

const MarkdownNode& MarkdownDocument::root() const {
  return *root_;
}

NodeIndex& MarkdownDocument::index() {
  return index_;
}

const NodeIndex& MarkdownDocument::index() const {
  return index_;
}

QString MarkdownDocument::markdownText() const {
  return markdownText_;
}

void MarkdownDocument::setMarkdownText(QString text, std::unique_ptr<MarkdownNode> root) {
  markdownText_ = std::move(text);
  replaceRoot(std::move(root));
}

quint64 MarkdownDocument::revision() const {
  return revision_;
}

bool MarkdownDocument::isModified() const {
  return modified_;
}

void MarkdownDocument::setModified(bool modified) {
  if (modified_ == modified) {
    return;
  }
  modified_ = modified;
  emit modifiedChanged(modified_);
}

MarkdownNode* MarkdownDocument::node(NodeId id) const {
  return index_.find(id);
}

void MarkdownDocument::replaceRoot(std::unique_ptr<MarkdownNode> root) {
  root_ = std::move(root);
  if (!root_) {
    root_ = std::make_unique<MarkdownNode>(BlockType::Document);
  }
  index_.rebuild(*root_);
  ++revision_;
  emit documentReset();
}

}  // namespace muffin
