#pragma once

#include "math/MathRenderNode.h"

#include <QColor>
#include <QString>

#include <memory>
#include <vector>

namespace muffin::math {

enum class MathLayoutKind {
  Span,
  Symbol,
  Rule,
  Strut,
  VList
};

struct MathLayoutNode {
  MathLayoutKind kind = MathLayoutKind::Span;
  MathRenderKind renderKind = MathRenderKind::Span;
  QString text;
  QString atomClass;
  QString fontClass;
  QString pathName;
  QString svgPath;
  QString imageSource;
  QRectF viewBox;
  QFont font;
  QColor color = Qt::black;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal depth = 0.0;
  qreal shift = 0.0;
  qreal ruleThickness = 0.0;
  qreal italic = 0.0;
  qreal italicMarginRight = 0.0;
  qreal xOffset = 0.0;
  qreal yOffset = 0.0;
  bool allowBreak = false;
  bool tightSpacing = false;
  bool phantom = false;
  std::vector<std::unique_ptr<MathLayoutNode>> children;
};

struct MathVListChild {
  std::unique_ptr<MathLayoutNode> elem;
  qreal shift = 0.0;
  qreal marginLeft = 0.0;
  qreal marginRight = 0.0;
};

struct MathVListEntry {
  bool kern = false;
  qreal size = 0.0;
  MathVListChild child;
};

std::unique_ptr<MathLayoutNode> layoutFromRenderNode(std::unique_ptr<MathRenderNode> node);
std::unique_ptr<MathRenderNode> renderNodeFromLayout(const MathLayoutNode& node);
std::unique_ptr<MathLayoutNode> makeLayoutSpan(std::vector<std::unique_ptr<MathLayoutNode>> children);
std::unique_ptr<MathLayoutNode> makeLayoutVListIndividualShift(std::vector<MathVListChild> children);
std::unique_ptr<MathLayoutNode> makeLayoutVListShift(qreal shift, std::vector<MathVListEntry> entries);
std::unique_ptr<MathLayoutNode> makeLayoutVListTop(qreal top, std::vector<MathVListEntry> entries);
std::unique_ptr<MathLayoutNode> makeLayoutVListBottom(qreal bottom, std::vector<MathVListEntry> entries);
std::unique_ptr<MathLayoutNode> makeLayoutVListFirstBaseline(std::vector<MathVListEntry> entries);
std::unique_ptr<MathLayoutNode> makeLayoutVListPositioned(std::vector<MathVListChild> children);
MathVListEntry makeLayoutVListElem(MathVListChild child);
MathVListEntry makeLayoutVListKern(qreal size);

}  // namespace muffin::math
