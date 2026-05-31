#include "document/InlineNode.h"

#include <utility>

namespace muffin {

InlineNode::InlineNode(InlineType type) : type_(type) {}

InlineType InlineNode::type() const {
  return type_;
}

QString InlineNode::text() const {
  return text_;
}

void InlineNode::setText(QString text) {
  text_ = std::move(text);
}

QString InlineNode::marker() const {
  return marker_;
}

void InlineNode::setMarker(QString marker) {
  marker_ = std::move(marker);
}

QString InlineNode::href() const {
  return href_;
}

void InlineNode::setHref(QString href) {
  href_ = std::move(href);
}

QString InlineNode::title() const {
  return title_;
}

void InlineNode::setTitle(QString title) {
  title_ = std::move(title);
}

QString InlineNode::alt() const {
  return alt_;
}

void InlineNode::setAlt(QString alt) {
  alt_ = std::move(alt);
}

QVector<InlineNode>& InlineNode::children() {
  return children_;
}

const QVector<InlineNode>& InlineNode::children() const {
  return children_;
}

InlineNode InlineNode::text(QString value) {
  InlineNode node(InlineType::Text);
  node.setText(std::move(value));
  return node;
}

InlineNode InlineNode::softBreak() {
  return InlineNode(InlineType::SoftBreak);
}

InlineNode InlineNode::lineBreak() {
  return InlineNode(InlineType::LineBreak);
}

InlineNode InlineNode::strong(QString marker, QVector<InlineNode> children) {
  InlineNode node(InlineType::Strong);
  node.setMarker(std::move(marker));
  node.children_ = std::move(children);
  return node;
}

InlineNode InlineNode::emphasis(QString marker, QVector<InlineNode> children) {
  InlineNode node(InlineType::Emphasis);
  node.setMarker(std::move(marker));
  node.children_ = std::move(children);
  return node;
}

InlineNode InlineNode::strikethrough(QString marker, QVector<InlineNode> children) {
  InlineNode node(InlineType::Strikethrough);
  node.setMarker(std::move(marker));
  node.children_ = std::move(children);
  return node;
}

InlineNode InlineNode::code(QString value) {
  InlineNode node(InlineType::Code);
  node.setText(std::move(value));
  return node;
}

InlineNode InlineNode::link(QString href, QString title, QVector<InlineNode> label) {
  InlineNode node(InlineType::Link);
  node.setHref(std::move(href));
  node.setTitle(std::move(title));
  node.children_ = std::move(label);
  return node;
}

InlineNode InlineNode::image(QString src, QString alt, QString title) {
  InlineNode node(InlineType::Image);
  node.setHref(std::move(src));
  node.setAlt(std::move(alt));
  node.setTitle(std::move(title));
  return node;
}

InlineNode InlineNode::inlineMath(QString tex) {
  InlineNode node(InlineType::InlineMath);
  node.setText(std::move(tex));
  return node;
}

}  // namespace muffin
