#include "math/MathRenderNode.h"

#include "math/MathSvgGeometry.h"

#include <QFontMetricsF>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace muffin::math {
namespace {

QString renderKindName(MathRenderKind kind) {
  switch (kind) {
    case MathRenderKind::Span: return QStringLiteral("Span");
    case MathRenderKind::Symbol: return QStringLiteral("Symbol");
    case MathRenderKind::Rule: return QStringLiteral("Rule");
    case MathRenderKind::Rect: return QStringLiteral("Rect");
    case MathRenderKind::Sqrt: return QStringLiteral("Sqrt");
    case MathRenderKind::SupSub: return QStringLiteral("SupSub");
    case MathRenderKind::Fraction: return QStringLiteral("Fraction");
    case MathRenderKind::Accent: return QStringLiteral("Accent");
    case MathRenderKind::Phantom: return QStringLiteral("Phantom");
    case MathRenderKind::Stretchy: return QStringLiteral("Stretchy");
    case MathRenderKind::LeftRight: return QStringLiteral("LeftRight");
    case MathRenderKind::Array: return QStringLiteral("Array");
    case MathRenderKind::VList: return QStringLiteral("VList");
    case MathRenderKind::Error: return QStringLiteral("Error");
  }
  return QStringLiteral("Unknown");
}

double rounded(qreal value) {
  return std::round(value * 10000.0) / 10000.0;
}

}  // namespace

qreal MathRenderNode::totalHeight() const {
  return height + depth;
}

QRectF MathRenderNode::boundsAt(QPointF origin) const {
  return QRectF(origin.x(), origin.y() - height, width, height + depth);
}

void MathRenderNode::paint(QPainter& painter, QPointF origin) const {
  if (phantom) {
    return;
  }
  painter.save();
  painter.setPen(color);
  switch (kind) {
    case MathRenderKind::Symbol:
    case MathRenderKind::Error: {
      painter.setFont(font);
      painter.drawText(origin, text);
      break;
    }
    case MathRenderKind::Rule: {
      if (qFuzzyIsNull(width) && !qFuzzyIsNull(height + depth)) {
        painter.setPen(QPen(color,
                            qMax<qreal>(1.0, ruleThickness),
                            text == QStringLiteral("dashed") ? Qt::DashLine : Qt::SolidLine,
                            Qt::SquareCap,
                            Qt::MiterJoin));
        painter.drawLine(QPointF(origin.x(), origin.y() - height), QPointF(origin.x(), origin.y() + depth));
      } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal thickness = !qFuzzyIsNull(height) ? height : ruleThickness;
        painter.drawRect(QRectF(origin.x(), origin.y() - thickness, width, thickness));
      }
      break;
    }
    case MathRenderKind::Rect: {
      const QRectF rect(origin.x(), origin.y() - height, width, height + depth);
      if (!imageSource.isEmpty()) {
        QImageReader reader(imageSource);
        const QImage image = reader.read();
        if (!image.isNull()) {
          painter.drawImage(rect, image);
        } else {
          painter.setPen(QPen(color, qMax<qreal>(1.0, ruleThickness)));
          painter.setBrush(Qt::NoBrush);
          painter.drawRect(rect);
          painter.setFont(font);
          painter.drawText(rect, Qt::AlignCenter, text);
        }
      } else if (qFuzzyIsNull(height) && !qFuzzyIsNull(ruleThickness)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(QRectF(origin.x(), origin.y() - ruleThickness, width, ruleThickness));
      } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(rect);
      }
      break;
    }
    case MathRenderKind::Sqrt: {
      painter.setPen(QPen(color, qMax<qreal>(1.0, ruleThickness)));
      const qreal top = origin.y() - height + ruleThickness;
      const qreal left = origin.x();
      painter.drawLine(QPointF(left, origin.y() - depth - 1.0), QPointF(left + width * 0.24, origin.y()));
      painter.drawLine(QPointF(left + width * 0.24, origin.y()), QPointF(left + width * 0.45, top));
      painter.drawLine(QPointF(left + width * 0.45, top), QPointF(left + width, top));
      break;
    }
    case MathRenderKind::Stretchy: {
      painter.setPen(QPen(color, qMax<qreal>(1.0, ruleThickness), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const qreal left = origin.x();
      const qreal right = origin.x() + width;
      const qreal mid = left + width / 2.0;
      const qreal top = origin.y() - height + ruleThickness;
      const qreal base = origin.y() - ruleThickness;
      if (!pathName.isEmpty() && MathSvgGeometry::hasPath(pathName)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(MathSvgGeometry::painterPath(pathName, QRectF(left, origin.y() - height, width, height)));
      } else if (!svgPath.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        // yOffset < 0 means the SVG paint region extends above the layout height
        // (e.g. sqrt emPad padding). KaTeX uses span.style.height = texHeight + 0.08
        // for rendering but span.height = texHeight for layout.
        const qreal paintPad = qMin<qreal>(0.0, yOffset);
        painter.drawPath(MathSvgGeometry::painterPathFromSvgPath(svgPath, viewBox, QRectF(left, origin.y() - height + paintPad, width, height + depth - paintPad)));
      } else if (text == QStringLiteral("\\cancel") || text == QStringLiteral("\\bcancel") || text == QStringLiteral("\\xcancel") ||
                 text == QStringLiteral("\\sout") || text == QStringLiteral("\\fbox") || text == QStringLiteral("\\colorbox") ||
                 text == QStringLiteral("\\fcolorbox") || text == QStringLiteral("\\phase") || text == QStringLiteral("\\angl")) {
        const QRectF rect(left, origin.y() - height, width, height + depth);
        painter.setPen(QPen(color, qMax<qreal>(1.0, ruleThickness), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        if (text == QStringLiteral("\\cancel") || text == QStringLiteral("\\xcancel")) {
          painter.drawLine(rect.bottomLeft(), rect.topRight());
        }
        if (text == QStringLiteral("\\bcancel") || text == QStringLiteral("\\xcancel")) {
          painter.drawLine(rect.topLeft(), rect.bottomRight());
        }
        if (text == QStringLiteral("\\sout")) {
          painter.drawLine(QPointF(rect.left(), rect.center().y()), QPointF(rect.right(), rect.center().y()));
        }
        if (text == QStringLiteral("\\fbox") || text == QStringLiteral("\\colorbox") || text == QStringLiteral("\\fcolorbox")) {
          painter.drawRect(rect);
        }
        if (text == QStringLiteral("\\phase")) {
          painter.drawLine(rect.bottomLeft(), QPointF(rect.left() + rect.height() * 0.55, rect.top()));
          painter.drawLine(QPointF(rect.left() + rect.height() * 0.55, rect.top()), rect.topRight());
        }
        if (text == QStringLiteral("\\angl")) {
          painter.drawLine(rect.topLeft(), rect.topRight());
          painter.drawLine(rect.topRight(), rect.bottomRight());
        }
      } else if (text == QStringLiteral("\\widehat") || text == QStringLiteral("\\widecheck")) {
        const qreal peak = text == QStringLiteral("\\widecheck") ? base : top;
        const qreal side = text == QStringLiteral("\\widecheck") ? top : base;
        painter.drawLine(QPointF(left, side), QPointF(mid, peak));
        painter.drawLine(QPointF(mid, peak), QPointF(right, side));
      } else if (text == QStringLiteral("\\widetilde")) {
        QPainterPath path;
        path.moveTo(left, base);
        path.cubicTo(left + width * 0.25, top, left + width * 0.25, top, mid, base);
        path.cubicTo(left + width * 0.75, top + height * 0.45, left + width * 0.75, top + height * 0.45, right, top);
        painter.drawPath(path);
      } else if (text == QStringLiteral("\\overbrace") || text == QStringLiteral("\\underbrace") ||
                 text == QStringLiteral("\\overbracket") || text == QStringLiteral("\\underbracket")) {
        const bool over = text == QStringLiteral("\\overbrace") || text == QStringLiteral("\\overbracket");
        const bool bracket = text == QStringLiteral("\\overbracket") || text == QStringLiteral("\\underbracket");
        const qreal y = over ? origin.y() - height + ruleThickness : origin.y() - ruleThickness;
        if (bracket) {
          painter.drawLine(QPointF(left, y), QPointF(right, y));
          const qreal tick = height * 0.65;
          painter.drawLine(QPointF(left, y), QPointF(left, over ? y + tick : y - tick));
          painter.drawLine(QPointF(right, y), QPointF(right, over ? y + tick : y - tick));
        } else {
          QPainterPath path;
          const qreal notch = over ? y + height * 0.85 : y - height * 0.85;
          path.moveTo(left, y);
          path.cubicTo(left + width * 0.18, y, left + width * 0.18, notch, mid, notch);
          path.cubicTo(left + width * 0.82, notch, left + width * 0.82, y, right, y);
          painter.drawPath(path);
        }
      } else {
        const bool leftArrow = text == QStringLiteral("\\overleftarrow") || text == QStringLiteral("\\overleftrightarrow");
        const bool rightArrow = text == QStringLiteral("\\overrightarrow") || text == QStringLiteral("\\overleftrightarrow") ||
                                text == QStringLiteral("\\underrightarrow") || text == QStringLiteral("\\underleftrightarrow");
        const bool underLeftArrow = text == QStringLiteral("\\underleftarrow") || text == QStringLiteral("\\underleftrightarrow");
        const qreal y = origin.y() - height / 2.0;
        painter.drawLine(QPointF(left, y), QPointF(right, y));
        const qreal head = qMin<qreal>(width * 0.18, height);
        if (rightArrow) {
          painter.drawLine(QPointF(right, y), QPointF(right - head, y - head * 0.45));
          painter.drawLine(QPointF(right, y), QPointF(right - head, y + head * 0.45));
        }
        if (leftArrow || underLeftArrow) {
          painter.drawLine(QPointF(left, y), QPointF(left + head, y - head * 0.45));
          painter.drawLine(QPointF(left, y), QPointF(left + head, y + head * 0.45));
        }
      }
      break;
    }
    case MathRenderKind::SupSub: {
      if (!children.empty()) {
        children.at(0)->paint(painter, QPointF(origin.x() + children.at(0)->xOffset, origin.y() + children.at(0)->yOffset));
      }
      for (size_t i = 1; i < children.size(); ++i) {
        children.at(i)->paint(painter, QPointF(origin.x() + children.at(i)->xOffset, origin.y() + children.at(i)->yOffset + children.at(i)->shift));
      }
      break;
    }
    case MathRenderKind::Fraction: {
      if (children.size() == 2) {
        const auto& numerator = children.at(0);
        const auto& denominator = children.at(1);
        numerator->paint(painter, QPointF(origin.x() + (width - numerator->width) / 2.0, origin.y() + numerator->shift));
        denominator->paint(painter, QPointF(origin.x() + (width - denominator->width) / 2.0, origin.y() + denominator->shift));
      } else if (children.size() >= 3) {
        const auto& numerator = children.at(0);
        const auto& line = children.at(1);
        const auto& denominator = children.at(2);
        numerator->paint(painter, QPointF(origin.x() + (width - numerator->width) / 2.0, origin.y() + numerator->shift));
        line->paint(painter, origin);
        denominator->paint(painter, QPointF(origin.x() + (width - denominator->width) / 2.0, origin.y() + denominator->shift));
      }
      break;
    }
    case MathRenderKind::Accent:
    case MathRenderKind::Phantom:
    case MathRenderKind::LeftRight:
    case MathRenderKind::Array:
    case MathRenderKind::VList: {
      for (const auto& child : children) {
        child->paint(painter, QPointF(origin.x() + child->xOffset, origin.y() + child->yOffset + child->shift));
      }
      break;
    }
    case MathRenderKind::Span: {
      qreal x = origin.x();
      for (const auto& child : children) {
        child->paint(painter, QPointF(x + child->xOffset, origin.y() + child->yOffset + child->shift));
        x += child->width;
      }
      break;
    }
  }
  painter.restore();
}

QJsonObject MathRenderNode::toJson() const {
  QJsonObject object;
  object.insert(QStringLiteral("kind"), renderKindName(kind));
  if (!text.isEmpty()) object.insert(QStringLiteral("text"), text);
  if (!atomClass.isEmpty()) object.insert(QStringLiteral("atomClass"), atomClass);
  if (!fontClass.isEmpty()) object.insert(QStringLiteral("fontClass"), fontClass);
  if (!pathName.isEmpty()) object.insert(QStringLiteral("pathName"), pathName);
  if (!svgPath.isEmpty()) object.insert(QStringLiteral("hasSvgPath"), true);
  if (!imageSource.isEmpty()) object.insert(QStringLiteral("imageSource"), imageSource);
  object.insert(QStringLiteral("width"), rounded(width));
  object.insert(QStringLiteral("height"), rounded(height));
  object.insert(QStringLiteral("depth"), rounded(depth));
  object.insert(QStringLiteral("shift"), rounded(shift));
  object.insert(QStringLiteral("xOffset"), rounded(xOffset));
  object.insert(QStringLiteral("yOffset"), rounded(yOffset));
  object.insert(QStringLiteral("ruleThickness"), rounded(ruleThickness));
  object.insert(QStringLiteral("italic"), rounded(italic));
  object.insert(QStringLiteral("italicMarginRight"), rounded(italicMarginRight));
  if (allowBreak) object.insert(QStringLiteral("allowBreak"), true);
  if (tightSpacing) object.insert(QStringLiteral("tightSpacing"), true);
  if (phantom) object.insert(QStringLiteral("phantom"), true);
  if (columns > 0) object.insert(QStringLiteral("columns"), columns);
  if (rows > 0) object.insert(QStringLiteral("rows"), rows);
  if (!children.empty()) {
    QJsonArray childArray;
    for (const auto& child : children) {
      childArray.push_back(child->toJson());
    }
    object.insert(QStringLiteral("children"), childArray);
  }
  return object;
}

QString MathRenderNode::toJsonString() const {
  return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

bool MathLayoutResult::valid() const {
  return root != nullptr;
}

void MathLayoutResult::paint(QPainter& painter, QPointF origin) const {
  if (!root) {
    return;
  }
  if (overflow && size.isValid()) {
    painter.save();
    painter.setClipRect(QRectF(origin, size), Qt::IntersectClip);
    root->paint(painter, QPointF(origin.x(), origin.y() + baseline));
    painter.restore();
  } else {
    root->paint(painter, QPointF(origin.x(), origin.y() + baseline));
  }
}

std::unique_ptr<MathRenderNode> cloneNode(const MathRenderNode& node) {
  auto copy = std::make_unique<MathRenderNode>();
  copy->kind = node.kind;
  copy->text = node.text;
  copy->atomClass = node.atomClass;
  copy->fontClass = node.fontClass;
  copy->pathName = node.pathName;
  copy->svgPath = node.svgPath;
  copy->imageSource = node.imageSource;
  copy->viewBox = node.viewBox;
  copy->font = node.font;
  copy->color = node.color;
  copy->width = node.width;
  copy->height = node.height;
  copy->depth = node.depth;
  copy->shift = node.shift;
  copy->ruleThickness = node.ruleThickness;
  copy->italic = node.italic;
  copy->italicMarginRight = node.italicMarginRight;
  copy->xOffset = node.xOffset;
  copy->yOffset = node.yOffset;
  copy->allowBreak = node.allowBreak;
  copy->tightSpacing = node.tightSpacing;
  copy->phantom = node.phantom;
  copy->columns = node.columns;
  copy->rows = node.rows;
  copy->children.reserve(node.children.size());
  for (const auto& child : node.children) {
    copy->children.push_back(cloneNode(*child));
  }
  return copy;
}

}  // namespace muffin::math
