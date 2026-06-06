#include "math/MathLayoutTree.h"

#include <QtGlobal>

namespace muffin::math {
namespace {

std::unique_ptr<MathLayoutNode> emptyLayoutNode() {
  return std::make_unique<MathLayoutNode>();
}

qreal entryHeightDepth(const MathVListEntry& entry) {
  if (entry.kern || !entry.child.elem) {
    return entry.size;
  }
  return entry.child.elem->height + entry.child.elem->depth;
}

std::unique_ptr<MathLayoutNode> makeLayoutVListFromEntries(std::vector<MathVListEntry> entries, qreal depth) {
  auto vlist = std::make_unique<MathLayoutNode>();
  vlist->kind = MathLayoutKind::VList;
  vlist->renderKind = MathRenderKind::VList;
  if (entries.empty()) {
    return vlist;
  }

  qreal minPos = depth;
  qreal maxPos = depth;
  qreal currPos = depth;
  for (MathVListEntry& entry : entries) {
    if (entry.kern) {
      currPos += entry.size;
    } else if (entry.child.elem) {
      MathLayoutNode& elem = *entry.child.elem;
      elem.xOffset += entry.child.marginLeft;
      elem.shift += -(currPos + elem.depth);
      vlist->width = qMax(vlist->width, elem.xOffset + elem.width + entry.child.marginRight);
      const qreal elemTotalHeight = elem.height + elem.depth;
      vlist->children.push_back(std::move(entry.child.elem));
      currPos += elemTotalHeight;
    }
    minPos = qMin(minPos, currPos);
    maxPos = qMax(maxPos, currPos);
  }

  vlist->height = maxPos;
  vlist->depth = -minPos;
  return vlist;
}

std::vector<MathVListEntry> entriesFromIndividualShift(std::vector<MathVListChild> children, qreal& depth) {
  std::vector<MathVListEntry> entries;
  if (children.empty()) {
    depth = 0.0;
    return entries;
  }

  if (!children.front().elem) {
    depth = 0.0;
    return entries;
  }

  depth = -children.front().shift - children.front().elem->depth;
  qreal currPos = depth;
  qreal previousHeightDepth = children.front().elem->height + children.front().elem->depth;
  entries.push_back(makeLayoutVListElem(std::move(children.front())));
  for (size_t i = 1; i < children.size(); ++i) {
    if (!children.at(i).elem) {
      continue;
    }
    const qreal diff = -children.at(i).shift - currPos - children.at(i).elem->depth;
    const qreal size = diff - previousHeightDepth;
    currPos += diff;
    previousHeightDepth = children.at(i).elem->height + children.at(i).elem->depth;
    entries.push_back(makeLayoutVListKern(size));
    entries.push_back(makeLayoutVListElem(std::move(children.at(i))));
  }
  return entries;
}

}  // namespace

std::unique_ptr<MathLayoutNode> layoutFromRenderNode(std::unique_ptr<MathRenderNode> node) {
  if (!node) {
    return emptyLayoutNode();
  }
  auto layout = std::make_unique<MathLayoutNode>();
  layout->kind = node->kind == MathRenderKind::Symbol ? MathLayoutKind::Symbol : MathLayoutKind::Span;
  layout->renderKind = node->kind;
  layout->text = std::move(node->text);
  layout->atomClass = std::move(node->atomClass);
  layout->fontClass = std::move(node->fontClass);
  layout->pathName = std::move(node->pathName);
  layout->svgPath = std::move(node->svgPath);
  layout->imageSource = std::move(node->imageSource);
  layout->viewBox = node->viewBox;
  layout->font = node->font;
  layout->color = node->color;
  layout->width = node->width;
  layout->height = node->height;
  layout->depth = node->depth;
  layout->shift = node->shift;
  layout->ruleThickness = node->ruleThickness;
  layout->italic = node->italic;
  layout->italicMarginRight = node->italicMarginRight;
  layout->xOffset = node->xOffset;
  layout->yOffset = node->yOffset;
  layout->allowBreak = node->allowBreak;
  layout->tightSpacing = node->tightSpacing;
  layout->phantom = node->phantom;
  layout->children.reserve(node->children.size());
  for (auto& child : node->children) {
    layout->children.push_back(layoutFromRenderNode(std::move(child)));
  }
  return layout;
}

std::unique_ptr<MathRenderNode> renderNodeFromLayout(const MathLayoutNode& node) {
  auto render = std::make_unique<MathRenderNode>();
  render->kind = node.renderKind;
  render->text = node.text;
  render->atomClass = node.atomClass;
  render->fontClass = node.fontClass;
  render->pathName = node.pathName;
  render->svgPath = node.svgPath;
  render->imageSource = node.imageSource;
  render->viewBox = node.viewBox;
  render->font = node.font;
  render->color = node.color;
  render->width = node.width;
  render->height = node.height;
  render->depth = node.depth;
  render->shift = node.shift;
  render->ruleThickness = node.ruleThickness;
  render->italic = node.italic;
  render->italicMarginRight = node.italicMarginRight;
  render->xOffset = node.xOffset;
  render->yOffset = node.yOffset;
  render->allowBreak = node.allowBreak;
  render->tightSpacing = node.tightSpacing;
  render->phantom = node.phantom;
  render->children.reserve(node.children.size());
  for (const auto& child : node.children) {
    render->children.push_back(renderNodeFromLayout(*child));
  }
  return render;
}

std::unique_ptr<MathLayoutNode> makeLayoutSpan(std::vector<std::unique_ptr<MathLayoutNode>> children) {
  auto span = std::make_unique<MathLayoutNode>();
  span->kind = MathLayoutKind::Span;
  span->renderKind = MathRenderKind::Span;
  span->children = std::move(children);
  for (const auto& child : span->children) {
    span->width += child->width;
    span->height = qMax(span->height, child->height - child->shift);
    span->depth = qMax(span->depth, child->depth + child->shift);
  }
  return span;
}

std::unique_ptr<MathLayoutNode> makeLayoutVListIndividualShift(std::vector<MathVListChild> children) {
  qreal depth = 0.0;
  auto entries = entriesFromIndividualShift(std::move(children), depth);
  return makeLayoutVListFromEntries(std::move(entries), depth);
}

std::unique_ptr<MathLayoutNode> makeLayoutVListPositioned(std::vector<MathVListChild> children) {
  auto vlist = std::make_unique<MathLayoutNode>();
  vlist->kind = MathLayoutKind::VList;
  vlist->renderKind = MathRenderKind::VList;
  if (children.empty()) {
    return vlist;
  }

  for (MathVListChild& child : children) {
    if (!child.elem) {
      continue;
    }
    child.elem->xOffset += child.marginLeft;
    child.elem->shift += child.shift;
    vlist->width = qMax(vlist->width, child.elem->xOffset + child.elem->width + child.marginRight);
    vlist->height = qMax(vlist->height, child.elem->height - child.elem->shift);
    vlist->depth = qMax(vlist->depth, child.elem->depth + child.elem->shift);
    vlist->children.push_back(std::move(child.elem));
  }
  return vlist;
}

std::unique_ptr<MathLayoutNode> makeLayoutVListShift(qreal shift, std::vector<MathVListEntry> entries) {
  if (entries.empty() || entries.front().kern || !entries.front().child.elem) {
    return makeLayoutVListFromEntries(std::move(entries), 0.0);
  }
  const qreal depth = -entries.front().child.elem->depth - shift;
  return makeLayoutVListFromEntries(std::move(entries), depth);
}

std::unique_ptr<MathLayoutNode> makeLayoutVListTop(qreal top, std::vector<MathVListEntry> entries) {
  qreal bottom = top;
  for (const MathVListEntry& entry : entries) {
    bottom -= entryHeightDepth(entry);
  }
  return makeLayoutVListFromEntries(std::move(entries), bottom);
}

std::unique_ptr<MathLayoutNode> makeLayoutVListBottom(qreal bottom, std::vector<MathVListEntry> entries) {
  return makeLayoutVListFromEntries(std::move(entries), -bottom);
}

std::unique_ptr<MathLayoutNode> makeLayoutVListFirstBaseline(std::vector<MathVListEntry> entries) {
  if (entries.empty() || entries.front().kern || !entries.front().child.elem) {
    return makeLayoutVListFromEntries(std::move(entries), 0.0);
  }
  return makeLayoutVListFromEntries(std::move(entries), -entries.front().child.elem->depth);
}

MathVListEntry makeLayoutVListElem(MathVListChild child) {
  MathVListEntry entry;
  entry.kern = false;
  entry.child = std::move(child);
  return entry;
}

MathVListEntry makeLayoutVListKern(qreal size) {
  MathVListEntry entry;
  entry.kern = true;
  entry.size = size;
  return entry;
}

}  // namespace muffin::math
