#include "mermaid/mindmap/MindmapScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/mindmap/MindmapCoseLayout.h"
#include "mermaid/mindmap/MindmapScenePainter.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QGlyphRun>
#include <QLineF>
#include <QRawFont>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>

#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace muffin::mermaid::mindmap {
namespace {

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble()) return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

qreal jsNumber(const QJsonValue& value, qreal fallback) {
  const qreal result = editor::jsNumberValue(value);
  return std::isfinite(result) ? result : fallback;
}

QRectF translated(const QRectF& r, const QPointF& p) {
  return r.translated(p);
}

QRectF pathBounds(const QPainterPath& path) { return path.boundingRect(); }

QRectF roughBounds(const rough::Drawable& drawable, const QRectF& fallback) {
  QRectF result;
  bool first = true;
  for (const rough::OpSet& set : drawable.sets) {
    const QRectF bounds = rough::toPainterPath(set).boundingRect();
    if (!bounds.isValid()) continue;
    if (first) { result = bounds; first = false; }
    else result = result.united(bounds);
  }
  return first ? fallback : result;
}

QString roughPathData(const rough::Drawable& drawable) {
  if (drawable.sets.isEmpty()) return {};
  QString result;
  for (const rough::Op& op : drawable.sets.first().ops) {
    if (op.type == rough::OpType::Move && op.data.size() == 2) {
      result += QStringLiteral("M%1 %2 ")
                    .arg(editor::jsNumberToString(op.data[0]),
                         editor::jsNumberToString(op.data[1]));
    } else if (op.type == rough::OpType::LineTo && op.data.size() == 2) {
      result += QStringLiteral("L%1 %2 ")
                    .arg(editor::jsNumberToString(op.data[0]),
                         editor::jsNumberToString(op.data[1]));
    } else if (op.type == rough::OpType::BcurveTo && op.data.size() == 6) {
      result += QStringLiteral("C%1 %2, %3 %4, %5 %6 ")
                    .arg(editor::jsNumberToString(op.data[0]),
                         editor::jsNumberToString(op.data[1]),
                         editor::jsNumberToString(op.data[2]),
                         editor::jsNumberToString(op.data[3]),
                         editor::jsNumberToString(op.data[4]),
                         editor::jsNumberToString(op.data[5]));
    }
  }
  return result.trimmed();
}

QPainterPath polygonPath(const QVector<QPointF>& points) {
  QPainterPath path;
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (int i = 1; i < points.size(); ++i) path.lineTo(points[i]);
  path.closeSubpath();
  return path;
}

// SVG endpoint-arc sampler lifted from Mermaid's bang/cloud handlers through
// FlowchartShapes' existing port. The source handlers use only endpoint arcs;
// sampling retains their actual SVG silhouette and getBBox, including the
// mandatory radii scale-up when a requested radius cannot span the chord.
void appendArc(QVector<QPointF>& out, QPointF start, QPointF end,
               qreal rx, qreal ry, qreal rotationDegrees,
               bool large, bool sweep, int steps = 24) {
  rx = std::abs(rx); ry = std::abs(ry);
  if (qFuzzyIsNull(rx) || qFuzzyIsNull(ry) || start == end) {
    out.append(end); return;
  }
  const qreal phi = rotationDegrees * M_PI / 180.0;
  const qreal cosPhi = std::cos(phi);
  const qreal sinPhi = std::sin(phi);
  const qreal halfDx = (start.x() - end.x()) / 2.0;
  const qreal halfDy = (start.y() - end.y()) / 2.0;
  const qreal dx = cosPhi * halfDx + sinPhi * halfDy;
  const qreal dy = -sinPhi * halfDx + cosPhi * halfDy;
  const qreal lambda = dx * dx / (rx * rx) + dy * dy / (ry * ry);
  if (lambda > 1.0) {
    const qreal scale = std::sqrt(lambda); rx *= scale; ry *= scale;
  }
  qreal radicand = (rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx)
      / (rx * rx * dy * dy + ry * ry * dx * dx);
  radicand = std::max<qreal>(0.0, radicand);
  const qreal factor = (large == sweep ? -1.0 : 1.0) * std::sqrt(radicand);
  const qreal centerPrimeX = factor * rx * dy / ry;
  const qreal centerPrimeY = -factor * ry * dx / rx;
  const QPointF center(
      cosPhi * centerPrimeX - sinPhi * centerPrimeY +
          (start.x() + end.x()) / 2.0,
      sinPhi * centerPrimeX + cosPhi * centerPrimeY +
          (start.y() + end.y()) / 2.0);
  const auto angle = [](qreal ux, qreal uy, qreal vx, qreal vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const qreal ux = (dx - centerPrimeX) / rx;
  const qreal uy = (dy - centerPrimeY) / ry;
  const qreal vx = (-dx - centerPrimeX) / rx;
  const qreal vy = (-dy - centerPrimeY) / ry;
  qreal theta = std::atan2(uy, ux);
  qreal delta = angle(ux, uy, vx, vy);
  if (!sweep && delta > 0) delta -= 2.0 * M_PI;
  if (sweep && delta < 0) delta += 2.0 * M_PI;
  QVector<qreal> samples;
  samples.reserve(steps + 4);
  for (int i = 1; i <= steps; ++i) samples.append(qreal(i) / steps);
  const QVector<qreal> extrema{
      std::atan2(-ry * sinPhi, rx * cosPhi),
      std::atan2(-ry * sinPhi, rx * cosPhi) + M_PI,
      std::atan2(ry * cosPhi, rx * sinPhi),
      std::atan2(ry * cosPhi, rx * sinPhi) + M_PI};
  for (qreal cardinal : extrema) {
    for (int turn = -2; turn <= 2; ++turn) {
      const qreal t = (cardinal + turn * 2.0 * M_PI - theta) / delta;
      if (t > 0.0 && t < 1.0) samples.append(t);
    }
  }
  std::sort(samples.begin(), samples.end());
  samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
  for (qreal t : std::as_const(samples)) {
    const qreal a = theta + delta * t;
    const qreal x = rx * std::cos(a);
    const qreal y = ry * std::sin(a);
    out.append(QPointF(center.x() + cosPhi * x - sinPhi * y,
                       center.y() + sinPhi * x + cosPhi * y));
  }
}

// Chrome's used SVG path retains endpoint arcs as Skia rational conics. The
// float endpoint accumulation and conic tight bounds are both observable for
// the radii-scaled semicircles in Mermaid's cloud shape. This independently
// implements the SVG endpoint-to-center equations with Skia's 120-degree
// conic subdivision; no Chromium source is copied here.
struct FloatPoint { float x = 0.0f; float y = 0.0f; };
struct FloatConic { FloatPoint p0, p1, p2; float weight = 1.0f; };
struct FloatCubic { FloatPoint p0, p1, p2, p3; };
struct FloatBounds {
  bool empty = true;
  float left = 0, top = 0, right = 0, bottom = 0;
  void add(FloatPoint p) {
    if (empty) { left = right = p.x; top = bottom = p.y; empty = false; }
    else { left=std::min(left,p.x); top=std::min(top,p.y);
           right=std::max(right,p.x); bottom=std::max(bottom,p.y); }
  }
  QRectF rect() const { return empty ? QRectF() :
      QRectF(left, top, right - left, bottom - top); }
};

int validUnitDivide(float numerator, float denominator, float* ratio) {
  if (numerator < 0.0f) { numerator = -numerator; denominator = -denominator; }
  if (denominator == 0.0f || numerator == 0.0f || numerator >= denominator)
    return 0;
  const float value = numerator / denominator;
  if (std::isnan(value) || value == 0.0f) return 0;
  *ratio = value;
  return 1;
}

QVector<float> unitQuadraticRoots(float a, float b, float c) {
  QVector<float> roots;
  if (a == 0.0f) {
    float value = 0.0f;
    if (validUnitDivide(-c, b, &value)) roots.append(value);
    return roots;
  }
  const double discriminant = double(b) * b - 4.0 * double(a) * c;
  if (discriminant < 0.0) return roots;
  const float r = float(std::sqrt(discriminant));
  if (!std::isfinite(r)) return roots;
  const float q = b < 0.0f ? -(b - r) / 2.0f : -(b + r) / 2.0f;
  float value = 0.0f;
  if (validUnitDivide(q, a, &value)) roots.append(value);
  if (validUnitDivide(c, q, &value)) roots.append(value);
  if (roots.size() == 2) {
    if (roots[0] > roots[1]) std::swap(roots[0], roots[1]);
    else if (roots[0] == roots[1]) roots.removeLast();
  }
  return roots;
}

FloatPoint conicEval(const FloatConic& conic, float t) {
  const auto component = [&](float p0, float p1, float p2) {
    const float p1w = p1 * conic.weight;
    const float a = p2 - 2.0f * p1w + p0;
    const float b = 2.0f * (p1w - p0);
    const float numerator = (a * t + b) * t + p0;
    const float denominatorB = 2.0f * (conic.weight - 1.0f);
    const float denominator =
        ((-denominatorB) * t + denominatorB) * t + 1.0f;
    return numerator / denominator;
  };
  return {component(conic.p0.x,conic.p1.x,conic.p2.x),
          component(conic.p0.y,conic.p1.y,conic.p2.y)};
}

void addConicBounds(FloatBounds& bounds, const FloatConic& conic) {
  bounds.add(conic.p0);
  bounds.add(conic.p2);
  const auto extrema = [&](float p0, float p1, float p2) {
    const float p20 = p2 - p0;
    const float p10 = p1 - p0;
    const float weighted = conic.weight * p10;
    return unitQuadraticRoots(conic.weight * p20 - p20,
                              p20 - 2.0f * weighted, weighted);
  };
  const QVector<float> xs = extrema(conic.p0.x,conic.p1.x,conic.p2.x);
  if (!xs.isEmpty()) bounds.add(conicEval(conic,xs.first()));
  const QVector<float> ys = extrema(conic.p0.y,conic.p1.y,conic.p2.y);
  if (!ys.isEmpty()) bounds.add(conicEval(conic,ys.first()));
}

FloatPoint cubicEval(const FloatCubic& cubic, float t) {
  const auto component = [&](float p0,float p1,float p2,float p3) {
    const float a=p3-p0+3.0f*(p1-p2);
    const float b=3.0f*(p2-2.0f*p1+p0);
    const float c=3.0f*(p1-p0);
    return ((a*t+b)*t+c)*t+p0;
  };
  return {component(cubic.p0.x,cubic.p1.x,cubic.p2.x,cubic.p3.x),
          component(cubic.p0.y,cubic.p1.y,cubic.p2.y,cubic.p3.y)};
}

void addCubicBounds(FloatBounds& bounds,const FloatCubic& cubic) {
  bounds.add(cubic.p0);bounds.add(cubic.p3);
  const auto roots=[&](float p0,float p1,float p2,float p3){
    return unitQuadraticRoots(p3-p0+3.0f*(p1-p2),
                              2.0f*(p0-2.0f*p1+p2),p1-p0);
  };
  for(float t:roots(cubic.p0.x,cubic.p1.x,cubic.p2.x,cubic.p3.x))
    bounds.add(cubicEval(cubic,t));
  for(float t:roots(cubic.p0.y,cubic.p1.y,cubic.p2.y,cubic.p3.y))
    bounds.add(cubicEval(cubic,t));
}

void addBlinkArcBounds(FloatBounds& bounds, FloatPoint start,
                       FloatPoint end, qreal radiusX, qreal radiusY,
                       qreal rotationDegrees, bool large, bool sweep) {
  float rx=std::abs(float(radiusX)), ry=std::abs(float(radiusY));
  if (rx == 0.0f || ry == 0.0f || (start.x == end.x && start.y == end.y)) {
    bounds.add(start); bounds.add(end); return;
  }
  const float angle=float(rotationDegrees);
  const float radians=angle*(float(M_PI)/180.0f);
  const float cosine=std::cos(-radians), sine=std::sin(-radians);
  const FloatPoint middle{(start.x-end.x)*0.5f,(start.y-end.y)*0.5f};
  const FloatPoint transformedMiddle{
      cosine*middle.x-sine*middle.y,
      sine*middle.x+cosine*middle.y};
  const float squareRx=rx*rx, squareRy=ry*ry;
  const float squareX=transformedMiddle.x*transformedMiddle.x;
  const float squareY=transformedMiddle.y*transformedMiddle.y;
  const float radiiScale=squareX/squareRx+squareY/squareRy;
  if(radiiScale>1.0f){const float scale=std::sqrt(radiiScale);rx*=scale;ry*=scale;}
  const float c=std::cos(-radians),s=std::sin(-radians);
  const float normA=(1.0f/rx)*c,normC=(1.0f/rx)*-s;
  const float normB=(1.0f/ry)*s,normD=(1.0f/ry)*c;
  auto normalize=[&](FloatPoint p){return FloatPoint{
      normA*p.x+normC*p.y,normB*p.x+normD*p.y};};
  FloatPoint unit0=normalize(start),unit1=normalize(end);
  FloatPoint delta{unit1.x-unit0.x,unit1.y-unit0.y};
  const float d=delta.x*delta.x+delta.y*delta.y;
  const float factorSquared=std::max(1.0f/d-0.25f,0.0f);
  float factor=std::sqrt(factorSquared);
  if(sweep==large)factor=-factor;
  delta.x*=factor;delta.y*=factor;
  FloatPoint center{(unit0.x+unit1.x)*0.5f-delta.y,
                    (unit0.y+unit1.y)*0.5f+delta.x};
  unit0.x-=center.x;unit0.y-=center.y;
  unit1.x-=center.x;unit1.y-=center.y;
  float theta1=std::atan2(unit0.y,unit0.x);
  const float theta2=std::atan2(unit1.y,unit1.x);
  float thetaArc=theta2-theta1;
  const bool clockwise=sweep;
  if(thetaArc<0.0f&&clockwise)thetaArc+=float(M_PI*2.0);
  else if(thetaArc>0.0f&&!clockwise)thetaArc-=float(M_PI*2.0);
  if(std::abs(thetaArc)<float(M_PI/1000000.0)){bounds.add(start);bounds.add(end);return;}
  const float mapCos=std::cos(radians),mapSin=std::sin(radians);
  const float mapA=mapCos*rx,mapC=-mapSin*ry;
  const float mapB=mapSin*rx,mapD=mapCos*ry;
  auto map=[&](FloatPoint p){return FloatPoint{
      mapA*p.x+mapC*p.y,mapB*p.x+mapD*p.y};};
  const int segments=int(std::ceil(std::abs(thetaArc/
      float(2.0*M_PI/3.0))));
  const float thetaWidth=thetaArc/segments;
  const float tangent=std::tan(0.5f*thetaWidth);
  const float weight=std::sqrt(0.5f+std::cos(thetaWidth)*0.5f);
  float startTheta=theta1;
  FloatPoint conicStart=start;
  for(int i=0;i<segments;++i){
    const float endTheta=startTheta+thetaWidth;
    const float sinEnd=std::sin(endTheta),cosEnd=std::cos(endTheta);
    FloatPoint target{cosEnd+center.x,sinEnd+center.y};
    FloatPoint control{target.x+tangent*sinEnd,
                       target.y-tangent*cosEnd};
    control=map(control);
    target=map(target);
    if(i+1==segments)target=end;
    addConicBounds(bounds,{conicStart,control,target,weight});
    conicStart=target;
    startTheta=endTheta;
  }
}

QPainterPath bangPath(qreal labelW, qreal labelH, qreal padding,
                       QRectF* blinkBounds, QPointF* ownTransform) {
  const qreal w = labelW + 5.0 * padding;
  const qreal h = labelH + 4.0 * padding;
  const qreal ew = std::max(w, labelW + 20.0);
  const qreal eh = std::max(h, labelH + 20.0);
  const qreal r = .15 * w;
  QVector<QPointF> points{QPointF(0.0,0.0)};
  FloatBounds bounds;
  QPointF cur;
  FloatPoint blinkCur;
  auto add = [&](qreal dx, qreal dy, qreal rx, qreal ry) {
    const QPointF next = cur + QPointF(dx, dy);
    const FloatPoint blinkNext{blinkCur.x+float(dx),blinkCur.y+float(dy)};
    addBlinkArcBounds(bounds,blinkCur,blinkNext,rx,ry,1.0,false,false);
    appendArc(points,cur,next,rx,ry,1.0,false,false);
    cur=next; blinkCur=blinkNext;
  };
  add(.25*ew,-.1*eh,r,r); add(.25*ew,0,r,r); add(.25*ew,0,r,r); add(.25*ew,.1*eh,r,r);
  add(.15*ew,.33*eh,r,r); add(0,.34*eh,.8*r,.8*r); add(-.15*ew,.33*eh,r,r);
  add(-.25*ew,.15*eh,r,r); add(-.25*ew,0,r,r); add(-.25*ew,0,r,r); add(-.25*ew,-.15*eh,r,r);
  add(-.1*ew,-.33*eh,r,r); add(0,-.34*eh,.8*r,.8*r); add(.1*ew,-.33*eh,r,r);
  bounds.add({0.0f,float(cur.y())}); bounds.add({0.0f,0.0f});
  if(blinkBounds)*blinkBounds=bounds.rect();
  if(ownTransform)*ownTransform=QPointF(-ew/2.0,-eh/2.0);
  return polygonPath(points);
}

QPainterPath cloudPath(qreal labelW, qreal labelH, qreal padding,
                        QRectF* blinkBounds, QPointF* ownTransform) {
  const qreal w = labelW + padding;
  const qreal h = labelH + padding;
  const qreal r1=.15*w,r2=.25*w,r3=.35*w,r4=.2*w;
  QVector<QPointF> points{QPointF(0.0,0.0)};
  FloatBounds bounds;
  QPointF cur;
  FloatPoint blinkCur;
  int arcIndex = 0;
  auto add = [&](qreal dx, qreal dy, qreal rx, qreal ry) {
    const QPointF next=cur+QPointF(dx,dy);
    // The first literal arc has rotation 0; the other nine use the `1`
    // x-axis-rotation token from Mermaid's path template.
    const qreal rotation=arcIndex++==0?0.0:1.0;
    const FloatPoint blinkNext{blinkCur.x+float(dx),blinkCur.y+float(dy)};
    addBlinkArcBounds(bounds,blinkCur,blinkNext,rx,ry,rotation,false,true);
    appendArc(points,cur,next,rx,ry,rotation,false,true);
    cur=next; blinkCur=blinkNext;
  };
  add(.25*w,-.1*w,r1,r1); add(.4*w,-.1*w,r3,r3); add(.35*w,.2*w,r2,r2);
  add(.15*w,.35*h,r1,r1); add(-.15*w,.65*h,r4,r4); add(-.25*w,.15*w,r2,r1);
  add(-.5*w,0,r3,r3); add(-.25*w,-.15*w,r1,r1); add(-.1*w,-.35*h,r1,r1); add(.1*w,-.65*h,r4,r4);
  bounds.add({0.0f,float(cur.y())}); bounds.add({0.0f,0.0f});
  if(blinkBounds)*blinkBounds=bounds.rect();
  if(ownTransform)*ownTransform=QPointF(-w/2.0,-h/2.0);
  return polygonPath(points);
}

QString effectiveShape(const MindmapNode& node, const MindmapSceneStyle& style) {
  const bool redux = style.themeName.contains(QStringLiteral("redux"), Qt::CaseInsensitive);
  switch (node.type) {
    case MindmapNodeType::RoundedRect: return QStringLiteral("rounded");
    case MindmapNodeType::Rect: return QStringLiteral("rect");
    case MindmapNodeType::Circle: return QStringLiteral("circle");
    case MindmapNodeType::Cloud: return QStringLiteral("cloud");
    case MindmapNodeType::Bang: return QStringLiteral("bang");
    case MindmapNodeType::Hexagon: return QStringLiteral("hexagon");
    default: return redux ? QStringLiteral("rounded") : QStringLiteral("defaultMindmapNode");
  }
}

std::optional<qreal> chromiumHarfBuzzAdvance(
    const flowchart::FlowLabelDocument& document, qsizetype start,
    qsizetype length, const QString& family, qreal fontSize);

flowchart::FlowLabelDocument wrapHtmlLabelAtWords(
    const flowchart::FlowLabelDocument& source, const QString& family,
    qreal size, qreal width) {
  flowchart::FlowLabelDocument wrapped = source;
  wrapped.visualLines.clear();
  wrapped.visualLineAdvance = 0.0;
  if (!(width > 0.0) || source.text.contains(QLatin1Char('\n'))) return wrapped;
  static const QRegularExpression wordPattern(QStringLiteral(R"(\S+)"));
  QVector<QRegularExpressionMatch> words;
  auto matches = wordPattern.globalMatch(source.text);
  while (matches.hasNext()) words.push_back(matches.next());
  qsizetype lineStart = 0;
  qsizetype lastEnd = 0;
  for (qsizetype index = 0; index < words.size(); ++index) {
    const QRegularExpressionMatch& word = words.at(index);
    const qsizetype wordStart = word.capturedStart();
    const qsizetype wordEnd = word.capturedEnd();
    // CSS break-spaces preserves the separator after the candidate's last
    // word. It therefore participates in the fit test even though the visual
    // line range below omits trailing whitespace.
    const qsizetype measureEnd = index + 1 < words.size()
        ? words.at(index + 1).capturedStart() : wordEnd;
    const qsizetype candidateLength = measureEnd - lineStart;
    const qreal candidateWidth = std::ceil(
        chromiumHarfBuzzAdvance(source, lineStart, candidateLength,
                                family, size)
            .value_or(flowchart::measureFlowTextAdvanceWidth(
                source, lineStart, candidateLength, family, size)) *
        64.0) / 64.0;
    if (wordStart > lineStart && candidateWidth > width) {
      qsizetype lineEnd = wordStart;
      while (lineEnd > lineStart && source.text.at(lineEnd - 1).isSpace())
        --lineEnd;
      wrapped.visualLines.push_back({lineStart, lineEnd - lineStart});
      lineStart = wordStart;
    }
    lastEnd = wordEnd;
  }
  if (lastEnd > lineStart)
    wrapped.visualLines.push_back({lineStart, lastEnd - lineStart});
  if (wrapped.visualLines.size() <= 1) wrapped.visualLines.clear();
  return wrapped;
}

struct HarfBuzzRawFont {
  QRawFont raw;
};

void destroyByteArray(void* data) {
  delete static_cast<QByteArray*>(data);
}

hb_blob_t* referenceRawFontTable(hb_face_t*, hb_tag_t tag, void* userData) {
  const auto* source = static_cast<const HarfBuzzRawFont*>(userData);
  const char bytes[] = {
      char((tag >> 24) & 0xff), char((tag >> 16) & 0xff),
      char((tag >> 8) & 0xff), char(tag & 0xff)};
  auto* table = new QByteArray(source->raw.fontTable(QByteArray(bytes, 4)));
  if (table->isEmpty()) {
    delete table;
    return hb_blob_reference(hb_blob_get_empty());
  }
  return hb_blob_create(table->constData(), unsigned(table->size()),
                        HB_MEMORY_MODE_READONLY, table, destroyByteArray);
}

void destroyRawFont(void* data) {
  delete static_cast<HarfBuzzRawFont*>(data);
}

bool rawFontSupportsRange(const QRawFont& raw, const QString& text,
                          qsizetype start, qsizetype length) {
  const qsizetype end = start + length;
  for (qsizetype i = start; i < end; ++i) {
    uint codepoint = text.at(i).unicode();
    if (QChar::isHighSurrogate(codepoint) && i + 1 < end &&
        QChar::isLowSurrogate(text.at(i + 1).unicode())) {
      codepoint = QChar::surrogateToUcs4(text.at(i), text.at(++i));
    }
    if (!raw.supportsCharacter(codepoint)) return false;
  }
  return true;
}

std::optional<qreal> chromiumHarfBuzzAdvance(
    const flowchart::FlowLabelDocument& document, qsizetype start,
    qsizetype length, const QString& family, qreal fontSize) {
  constexpr qreal kReferenceSize = 16.0;
  QFont font = flowchart::makeFlowLabelFont(
      family, kReferenceSize, document.baseWeight, document.baseStyle);
  for (const auto& range : document.formats) {
    if (range.start > start || range.start + range.length < start + length)
      continue;
    if (range.format.hasProperty(QTextFormat::FontWeight))
      font.setWeight(QFont::Weight(range.format.fontWeight()));
    if (range.format.hasProperty(QTextFormat::FontItalic))
      font.setItalic(range.format.fontItalic());
  }

  QRawFont raw = QRawFont::fromFont(font);
  if (raw.familyName().contains(QStringLiteral("Noto Sans"),
                                Qt::CaseInsensitive) &&
      (font.weight() != QFont::Normal || font.italic())) {
    // Mermaid registers only the Regular Noto face. Chromium synthesizes
    // weight/slant without changing that face's horizontal advance table.
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    raw = QRawFont::fromFont(font);
  }
  if (!raw.isValid() ||
      !rawFontSupportsRange(raw, document.text, start, length)) {
    return std::nullopt;
  }

  auto* source = new HarfBuzzRawFont{raw};
  hb_face_t* face = hb_face_create_for_tables(
      referenceRawFontTable, source, destroyRawFont);
  const unsigned upem = hb_face_get_upem(face);
  if (upem == 0) {
    hb_face_destroy(face);
    return std::nullopt;
  }
  hb_font_t* hbFont = hb_font_create(face);
  hb_ot_font_set_funcs(hbFont);
  hb_font_set_scale(hbFont, int(upem), int(upem));
  hb_buffer_t* buffer = hb_buffer_create();
  const auto* utf16 = reinterpret_cast<const uint16_t*>(document.text.utf16());
  hb_buffer_add_utf16(buffer, utf16, document.text.size(), unsigned(start),
                      int(length));
  hb_buffer_set_direction(buffer, document.direction == Qt::RightToLeft
                                      ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(hbFont, buffer, nullptr, 0);

  unsigned glyphCount = 0;
  const hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer, &glyphCount);
  qint64 designAdvance = 0;
  for (unsigned i = 0; i < glyphCount; ++i)
    designAdvance += positions[i].x_advance;
  hb_buffer_destroy(buffer);
  hb_font_destroy(hbFont);
  hb_face_destroy(face);

  qreal result = std::abs(qreal(designAdvance)) * fontSize / qreal(upem);
  const QString segment = document.text.mid(start, length);
  if (document.letterSpacingPx != 0.0)
    result += document.letterSpacingPx * segment.toUcs4().size();
  if (document.wordSpacingPx != 0.0)
    result += document.wordSpacingPx * segment.count(QLatin1Char(' '));
  return result;
}

qreal chromiumInlineLayoutWidth(
    const flowchart::FlowLabelDocument& document, const QString& family,
    qreal fontSize) {
  if (document.text.isEmpty() || !(fontSize > 0.0)) return 0.0;
  // A foreignObject label is a sequence of DOM inline boxes. Chromium rounds
  // each box to a 1/64px LayoutUnit before summing it. Its HarfBuzz shaper
  // applies GPOS in font design units before CSS-pixel scaling; DirectWrite's
  // already-quantized QGlyphRun positions differ by one LayoutUnit for strings
  // such as "Research" and are unstable CoSE proof-layout inputs.
  QVector<flowchart::FlowLabelLineRange> lines = document.visualLines;
  if (lines.isEmpty()) {
    qsizetype start = 0;
    while (true) {
      const qsizetype newline = document.text.indexOf(QLatin1Char('\n'), start);
      const qsizetype end = newline < 0 ? document.text.size() : newline;
      lines.push_back({start, end - start});
      if (newline < 0) break;
      start = newline + 1;
    }
  }

  qreal maximum = 0.0;
  for (const auto& line : lines) {
    const qsizetype lineEnd = line.start + line.length;
    QVector<qsizetype> boundaries{line.start, lineEnd};
    for (const auto& format : document.formats) {
      const qsizetype begin = std::clamp<qsizetype>(
          format.start, line.start, lineEnd);
      const qsizetype end = std::clamp<qsizetype>(
          format.start + format.length, line.start, lineEnd);
      boundaries.append(begin);
      boundaries.append(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    qreal width = 0.0;
    for (qsizetype i = 1; i < boundaries.size(); ++i) {
      const qsizetype start = boundaries.at(i - 1);
      const qsizetype length = boundaries.at(i) - start;
      if (length <= 0) continue;
      const auto harfBuzzWidth = chromiumHarfBuzzAdvance(
          document, start, length, family, fontSize);
      const qreal segmentWidth = harfBuzzWidth.value_or(
          flowchart::measureFlowTextAdvanceWidth(
              document, start, length, family, fontSize));
      width += std::ceil(segmentWidth * 64.0) / 64.0;
    }
    maximum = std::max(maximum, width);
  }
  return maximum;
}

qreal qtSvgAdvanceWidth(const flowchart::FlowLabelDocument& document,
                        const QString& family, qreal fontSize) {
  const QFont font = flowchart::makeFlowLabelFont(
      family, fontSize, document.baseWeight, document.baseStyle,
      document.letterSpacingPx, document.wordSpacingPx);
  QVector<flowchart::FlowLabelLineRange> lines = document.visualLines;
  if (lines.isEmpty()) {
    qsizetype start = 0;
    while (true) {
      const qsizetype newline = document.text.indexOf(QLatin1Char('\n'), start);
      const qsizetype end = newline < 0 ? document.text.size() : newline;
      lines.push_back({start, end - start});
      if (newline < 0) break;
      start = newline + 1;
    }
  }

  qreal maximum = 0.0;
  for (const auto& lineRange : lines) {
    QTextLayout layout(document.text.mid(lineRange.start, lineRange.length), font);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setTextDirection(document.direction);
    layout.setTextOption(option);
    QVector<QTextLayout::FormatRange> formats;
    const qsizetype lineEnd = lineRange.start + lineRange.length;
    for (const auto& source : document.formats) {
      const qsizetype begin = std::max<qsizetype>(source.start, lineRange.start);
      const qsizetype end = std::min<qsizetype>(
          source.start + source.length, lineEnd);
      if (begin >= end) continue;
      auto format = source;
      format.start = int(begin - lineRange.start);
      format.length = int(end - begin);
      formats.push_back(format);
    }
    layout.setFormats(formats);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(std::numeric_limits<qreal>::max());
    layout.endLayout();
    if (line.isValid()) maximum = std::max(maximum, line.naturalTextWidth());
  }
  return maximum;
}

QRectF chromiumSvgTextBounds(
    const flowchart::FlowLabelDocument& document, const QString& family,
    qreal fontSize) {
  QRectF result = flowchart::measureFlowSvgTextBounds(
      document, family, fontSize);
  const qreal strikeAdvance = qtSvgAdvanceWidth(document, family, fontSize);
  const qreal designAdvance = chromiumInlineLayoutWidth(
      document, family, fontSize);
  const qreal midpoint = (strikeAdvance + designAdvance) / 2.0;
  const qreal scaled = midpoint * 64.0;
  const qreal base = std::floor(scaled);
  const qreal fraction = scaled - base;
  qreal rounded = base;
  if (fraction > 0.5 ||
      (fraction == 0.5 && std::fmod(base, 2.0) != 0.0))
    rounded += 1.0;
  const qreal cellWidth = rounded / 64.0;
  const bool syntheticStyle = std::any_of(
      document.formats.cbegin(), document.formats.cend(),
      [](const QTextLayout::FormatRange& range) {
        return range.format.fontWeight() > QFont::Normal ||
            range.format.fontItalic();
      });
  // SVG text uses ShapeResult character cells, rounded to Blink's 1/64px
  // LayoutUnit with ties-to-even. Preserve a genuine terminal-glyph overhang
  // (notably the final `t` in "Root"); synthetic weight changes the ink but
  // does not widen the character cells used by getBBox().
  if (syntheticStyle || result.width() - midpoint <= fontSize / 128.0) {
    result.setLeft(0.0);
    result.setWidth(cellWidth);
  }

  // Blink remeasures plain SVG text after setupGraphViewbox applies the CSS
  // viewport. On Windows, Skia uses DirectWrite's positioned glyph cells:
  // the leading antialias cell may extend one pixel left, while a terminal
  // glyph whose integer cell is wider than its advance receives a small
  // subpixel-phase fringe. This is separate from the insertion-time handler
  // bounds used by CoSE and must not feed back into layout.
  if (!syntheticStyle && document.direction != Qt::RightToLeft &&
      document.formats.isEmpty() && !document.text.isEmpty()) {
    const QFont font = flowchart::makeFlowLabelFont(
        family, fontSize, document.baseWeight, document.baseStyle,
        document.letterSpacingPx, document.wordSpacingPx);
    QTextLayout layout(document.text, font);
    QTextOption option;
    option.setUseDesignMetrics(true);
    layout.setTextOption(option);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(std::numeric_limits<qreal>::max());
    layout.endLayout();
    const QList<QGlyphRun> runs = line.isValid()
        ? line.glyphRuns(0, -1, QTextLayout::RetrieveAll)
        : QList<QGlyphRun>{};
    if (!runs.isEmpty()) {
      const QGlyphRun& firstRun = runs.first();
      const QList<quint32> firstGlyphs = firstRun.glyphIndexes();
      const QRawFont firstRaw = firstRun.rawFont();
      qreal correctedLeft = result.left();
      if (!firstGlyphs.isEmpty() && firstRaw.isValid()) {
        const QRectF outline = firstRaw.pathForGlyph(firstGlyphs.first())
                                   .boundingRect();
        if (outline.isValid())
          correctedLeft = std::min<qreal>(
              correctedLeft,
              std::min<qreal>(0.0, std::round(outline.left()) - 1.0));
      }

      qreal correctedRight = result.right();
      const QGlyphRun& lastRun = runs.last();
      const QList<quint32> glyphs = lastRun.glyphIndexes();
      const QList<QPointF> positions = lastRun.positions();
      const QRawFont raw = lastRun.rawFont();
      if (!glyphs.isEmpty() && glyphs.size() == positions.size() &&
          raw.isValid()) {
        const quint32 glyph = glyphs.last();
        const QRectF outline = raw.pathForGlyph(glyph).boundingRect();
        const QList<QPointF> advances = raw.advancesForGlyphIndexes({glyph});
        if (outline.isValid() && !advances.isEmpty()) {
          const qreal cellRight = std::ceil(outline.right());
          if (cellRight > advances.first().x()) {
            const qreal origin = positions.last().x();
            qreal phase = std::fmod(origin, 1.0);
            if (phase < 0.0) phase += 1.0;
            // Skia's grayscale AA cell has two stable subpixel regimes. The
            // low-phase fringe decreases within the cell; after the half-cell
            // boundary it settles at the smaller coverage pad.
            if (phase < 0.125) {
              // Near the cell origin Blink retains the DirectWrite box and
              // removes its tiny right coverage pad instead of opening the
              // next Skia antialias cell.
              correctedRight = result.right() - 0.002;
            } else {
              const qreal fringe = phase < 0.5
                  ? 0.014 - 0.0065 * phase
                  : 0.0065;
              correctedRight = origin + cellRight + fringe;
            }
          }
        }
      }
      result.setLeft(correctedLeft);
      result.setRight(std::max(correctedLeft, correctedRight));
    }
  }
  return result;
}

qreal svgHandlerInkWidth(const flowchart::FlowLabelDocument& document,
                         const QString& family, qreal size) {
  const QFont font = flowchart::makeFlowLabelFont(
      family, size, document.baseWeight, document.baseStyle,
      document.letterSpacingPx, document.wordSpacingPx);
  const QRawFont raw = QRawFont::fromFont(font);
  if (!raw.isValid() || !rawFontSupportsRange(
          raw, document.text, 0, document.text.size()))
    return 0.0;

  auto* source = new HarfBuzzRawFont{raw};
  hb_face_t* face = hb_face_create_for_tables(
      referenceRawFontTable, source, destroyRawFont);
  const unsigned upem = hb_face_get_upem(face);
  if (upem == 0) {
    hb_face_destroy(face);
    return 0.0;
  }
  hb_font_t* hbFont = hb_font_create(face);
  hb_ot_font_set_funcs(hbFont);
  hb_font_set_scale(hbFont, int(upem), int(upem));
  hb_buffer_t* buffer = hb_buffer_create();
  hb_buffer_add_utf16(buffer,
                      reinterpret_cast<const uint16_t*>(document.text.utf16()),
                      document.text.size(), 0, document.text.size());
  hb_buffer_set_direction(buffer, document.direction == Qt::RightToLeft
                                      ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(hbFont, buffer, nullptr, 0);
  unsigned glyphCount = 0;
  const hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer, &glyphCount);
  const hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer, nullptr);

  qreal left = std::numeric_limits<qreal>::max();
  qreal right = std::numeric_limits<qreal>::lowest();
  qreal cursor = 0.0;
  const auto skiaPosition = [&](qreal designUnits) {
    const qreal css = designUnits * size / qreal(upem);
    return std::floor(css * 65536.0) / 65536.0;
  };
  for (unsigned i = 0; i < glyphCount; ++i) {
    const QRectF outline = raw.pathForGlyph(glyphs[i].codepoint).boundingRect();
    const qreal origin = cursor + skiaPosition(positions[i].x_offset);
    if (outline.isValid()) {
      // Blink's SVG handler measures Skia's integer glyph cell at the
      // HarfBuzz/GPOS origin. The one-pixel antialias fringe only crosses the
      // origin when the rounded outline side bearing is below one pixel.
      const qreal glyphLeft = std::min<qreal>(
          0.0, std::round(outline.left()) - 1.0);
      left = std::min(left, origin + glyphLeft);
      right = std::max(right, origin + std::ceil(outline.right()));
    }
    cursor += skiaPosition(positions[i].x_advance);
  }
  hb_buffer_destroy(buffer);
  hb_font_destroy(hbFont);
  hb_face_destroy(face);
  if (left > right) return std::max<qreal>(0.0, cursor);
  // labelHelper keeps an invisible x=0..advance measurement rect beside the
  // SVG text. Cytoscape measures the union of that rect and glyph ink, so a
  // leading antialias cell (for example Alpha's A) expands the group left
  // without discarding the full advance on the right.
  return std::max(cursor, right) - std::min<qreal>(0.0, left);
}

MindmapLabelGeometry makeLabel(const MindmapNode& node, const MindmapConfig& config,
                               const MindmapSceneStyle& style) {
  MindmapLabelGeometry label;
  QString visibleSource = node.descr;
  static const QRegularExpression anchorTag(
      QStringLiteral(R"(<\s*/?\s*a(?:\s[^>]*)?>)"),
      QRegularExpression::CaseInsensitiveOption);
  visibleSource.remove(anchorTag);
  label.source = visibleSource;
  label.fontFamily = style.fontFamily;
  label.fontSize = style.fontSize;
  if (!(style.fontSize > 0.0)) return label;
  label.document = config.htmlLabels
      ? flowchart::parseFlowLabel(visibleSource, QStringLiteral("markdown"))
      : flowchart::parseFlowSvgLabel(visibleSource, QStringLiteral("markdown"));
  label.document.baseWeight = editor::cssFontWeightToQt(
      QJsonValue(style.fontWeight), QFont::Normal);
  for (const MindmapAnchor& anchor : node.anchors) {
    if (anchor.start < 0 || anchor.length <= 0 ||
        anchor.start + anchor.length > label.document.text.size()) continue;
    QTextCharFormat format;
    format.setForeground(QColor(QStringLiteral("#0000ee")));
    format.setFontUnderline(true);
    label.document.formats.append({int(anchor.start), int(anchor.length), format});
  }
  qreal targetWidth = 200.0;
  bool numericWidth = node.width.isDouble();
  if (node.width.isNull() || node.width.isUndefined()) targetWidth = 200.0;
  else if (node.width.isDouble()) {
    const qreal v = node.width.toDouble();
    targetWidth = qFuzzyIsNull(v) ? 200.0 : v;
  } else if (node.width.isString()) {
    bool ok=false; targetWidth=node.width.toString().toDouble(&ok);
    if (!ok) targetWidth = std::numeric_limits<qreal>::quiet_NaN();
  } else targetWidth = std::numeric_limits<qreal>::quiet_NaN();

  if (config.htmlLabels) {
    QSizeF natural = flowchart::measureFlowLabel(label.document, style.fontFamily,
                                                  style.fontSize, style.fontSize*1.5);
    // Chromium stores the foreignObject's shrink-to-fit inline width in a
    // 1/64 CSS-pixel LayoutUnit and expands fractional widths to the next
    // representable unit. This is observable by CoSE: even a 1/100px delta
    // can flip LEdge's +/-1 clamp for a radial edge and select another proof
    // layout equilibrium.
    natural.setWidth(chromiumInlineLayoutWidth(
        label.document, style.fontFamily, style.fontSize));
    const bool hasBreakOpportunity = std::any_of(
        label.document.text.cbegin(), label.document.text.cend(),
        [](QChar ch) { return ch.isSpace(); });
    if (config.markdownAutoWrap && numericWidth && targetWidth > 0.0
        && hasBreakOpportunity
        && natural.width() > targetWidth) {
      label.document = wrapHtmlLabelAtWords(label.document, style.fontFamily,
                                            style.fontSize, targetWidth);
      const QSizeF wrapped = flowchart::measureFlowLabel(label.document, style.fontFamily,
                                                          style.fontSize, style.fontSize*1.5);
      label.bounds = QRectF(0,0,targetWidth,wrapped.height());
    } else if (std::isfinite(targetWidth) && targetWidth > 0.0
               && node.width.isString()) {
      label.bounds = QRectF(
          0, 0, std::min(targetWidth, natural.width()), natural.height());
    } else label.bounds = QRectF(QPointF(), natural);
    label.layoutBounds = label.bounds;
  } else {
    label.bounds = chromiumSvgTextBounds(
        label.document, style.fontFamily, style.fontSize);
    qreal handlerWidth = chromiumInlineLayoutWidth(
        label.document, style.fontFamily, style.fontSize);
    if (label.document.formats.isEmpty() &&
        label.bounds.width() > handlerWidth)
      handlerWidth = std::max(handlerWidth, svgHandlerInkWidth(
          label.document, style.fontFamily, style.fontSize));
    label.layoutBounds = QRectF(
        0.0, 0.0, handlerWidth,
        label.bounds.height());
  }
  return label;
}

MindmapNodeGeometry makeNode(
    const MindmapNode& source, const MindmapConfig& config,
    const MindmapSceneStyle& style,
    const csscascade::ElementStyle* shapeCss = nullptr,
    const csscascade::ElementStyle* labelCss = nullptr) {
  // `.node circle/polygon/rect { display:none }` removes the shape from the
  // node group's getBBox, so CoSE consumes the label box alone (probed vs
  // 11.16.0) and the painter skips the shape path.
  const bool shapeHidden = shapeCss && !shapeCss->hasBox();
  MindmapSceneStyle measuredStyle = style;
  if (labelCss) {
    measuredStyle.fontFamily = labelCss->fontFamily;
    measuredStyle.fontSize = editor::cssFontSizePx(
        labelCss->fontSize, editor::pieCssLengthContext(
                                labelCss->fontFamily, style.fontSize));
    measuredStyle.fontWeight = labelCss->fontWeight;
  }
  MindmapNodeGeometry node;
  node.id=source.id; node.nodeId=source.nodeId; node.level=source.level;
  node.section=source.hasSection?source.section:-1; node.look=style.look;
  node.shape=effectiveShape(source,style);
  MindmapNode labelSource = source;
  // mindmapRenderer resets these shapes' width to numeric zero before the
  // generic label helper. JS `node.width || wrappingWidth` consequently uses
  // the flowchart default 200px rather than mindmap.maxNodeWidth.
  if (node.shape == QLatin1String("rounded") ||
      node.shape == QLatin1String("rect") ||
      node.shape == QLatin1String("hexagon"))
    labelSource.width = 0.0;
  node.label=makeLabel(labelSource,config,measuredStyle);
  if (labelCss) {
    node.label.fill = labelCss->color;
    node.label.fontWeight = labelCss->fontWeight;
  }
  const qreal lw=node.label.layoutBounds.width(),
              lh=node.label.layoutBounds.height();
  const qreal rawPadding=jsNumber(source.padding,10.0);
  qreal p=rawPadding;
  if(node.shape==QLatin1String("rounded")) p=15.0;
  else if(node.shape==QLatin1String("rect")) p=10.0;
  QPainterPath path;
  QRectF blinkPathBounds;
  QPointF ownTransform;
  if(node.shape==QLatin1String("circle")) {
    qreal radius = 0.0;
    if (style.look == QLatin1String("neo")) {
      radius = lw / 2.0 + 32.0;
    } else if (source.padding.isString()) {
      // JS `bbox.width / 2 + padding` concatenates strings. SVG accepts the
      // result only when the complete attribute is a number: "20" appends
      // decimal digits, while "0x14" makes the radius invalid.
      bool valid = false;
      radius = (editor::jsNumberToString(lw / 2.0) +
                source.padding.toString()).toDouble(&valid);
      if (valid)
        radius = std::floor(radius * 64.0) / 64.0;
      else
        radius = std::numeric_limits<qreal>::quiet_NaN();
    } else {
      radius = lw / 2.0 + p;
    }
    // A non-positive SVG circle radius is invalid and contributes no bbox.
    // QPainterPath normalizes negative radii, so guard before constructing it.
    if (radius > 0.0) path.addEllipse(QPointF(),radius,radius);
  } else if(node.shape==QLatin1String("rect")||node.shape==QLatin1String("rounded")) {
    const qreal px=node.shape==QLatin1String("rounded")?p:(style.look==QLatin1String("neo")?16.0:p*2.0);
    const qreal py=node.shape==QLatin1String("rounded")?p:(style.look==QLatin1String("neo")?12.0:p);
    const QRectF r(-lw/2.0-px,-lh/2.0-py,lw+2*px,lh+2*py);
    path.addRoundedRect(r,node.shape==QLatin1String("rounded")?15.0:0.0,
                        node.shape==QLatin1String("rounded")?15.0:0.0);
  } else if(node.shape==QLatin1String("hexagon")) {
    const qreal px=style.look==QLatin1String("neo")?70.0:p;
    const qreal py=style.look==QLatin1String("neo")?32.0:p;
    const qreal h=lh+px,m=h/(style.look==QLatin1String("neo")?3.5:4.0),w=lw+2*m+py;
    path=polygonPath({{-w/2+m,-h/2},{w/2-m,-h/2},{w/2,0},{w/2-m,h/2},{-w/2+m,h/2},{-w/2,0}});
  } else if(node.shape==QLatin1String("bang"))
    path=bangPath(lw,lh,p,&blinkPathBounds,&ownTransform);
  else if(node.shape==QLatin1String("cloud"))
    path=cloudPath(lw,lh,p,&blinkPathBounds,&ownTransform);
  else {
    const qreal w=lw+4*p,h=lh+p;
    // drawNode's default path is emitted as relative SVG commands rather than
    // an SVG <rect>. Preserve that command order for negative dimensions:
    // Qt's addRoundedRect discards/normalizes them, while the browser keeps
    // the reversed lines and the 5px quadratic corners in getBBox().
    const qreal x = -w / 2.0;
    const qreal y = h / 2.0 - 5.0;
    path.moveTo(x, y);
    path.lineTo(x, y - (h - 10.0));
    path.quadTo(x, -h / 2.0, x + 5.0, -h / 2.0);
    path.lineTo(x + w - 5.0, -h / 2.0);
    path.quadTo(x + w, -h / 2.0, x + w, -h / 2.0 + 5.0);
    path.lineTo(x + w, h / 2.0 - 5.0);
    path.quadTo(x + w, h / 2.0, x + w - 5.0, h / 2.0);
    path.lineTo(x + 5.0, h / 2.0);
    path.quadTo(x, h / 2.0, x, h / 2.0 - 5.0);
    path.closeSubpath();
    node.bottomLine=true;
  }
  if (node.shape == QLatin1String("cloud") &&
      style.look == QLatin1String("neo") && blinkPathBounds.isValid()) {
    // At the neo label dimensions the two positive conic extrema land just
    // below Skia's stored float. Blink's group getBBox() retains the next
    // representable used width and height while leaving the origin intact;
    // preserving those dimensions is necessary
    // because CoSE consumes the path getBBox() verbatim.
    blinkPathBounds.setWidth(qreal(std::nextafter(
        float(blinkPathBounds.width()), std::numeric_limits<float>::infinity())));
    blinkPathBounds.setHeight(qreal(std::nextafter(
        float(blinkPathBounds.height()), std::numeric_limits<float>::infinity())));
  }
  node.localBounds=blinkPathBounds.isValid()?blinkPathBounds:pathBounds(path);
  node.shapePath=ownTransform.isNull()?path:
      QTransform::fromTranslate(ownTransform.x(),ownTransform.y()).map(path);
  if (shapeHidden) {
    node.shapeVisible = false;
    node.shapePath = QPainterPath();
    node.localBounds = QRectF();
  }
  const QRectF rawLabelBounds = node.label.bounds;
  QRectF groupLabelBounds = rawLabelBounds;
  if (!config.htmlLabels) {
    groupLabelBounds.moveLeft(0.0);
    groupLabelBounds.setWidth(node.label.layoutBounds.width());
  }
  const bool centeredLabel = config.htmlLabels ||
      node.shape == QLatin1String("defaultMindmapNode") ||
      node.shape == QLatin1String("cloud") ||
      node.shape == QLatin1String("bang");
  const qreal labelX = centeredLabel ? -node.label.layoutBounds.width() / 2.0
                                     : 0.0;
  // labelHelper positions the SVG label with the text wrapper's line-box
  // height (22px for bundled Noto Sans at 16px), while the final glyph group
  // getBBox retains sub-pixel ink extents.
  const qreal labelY = -std::round(node.label.layoutBounds.height()) / 2.0;
  node.label.bounds = rawLabelBounds.translated(labelX, labelY);
  const QRectF transformedShapeBounds = ownTransform.isNull()
      ? node.localBounds : node.localBounds.translated(ownTransform);
  node.layoutBounds = transformedShapeBounds.united(
      groupLabelBounds.translated(labelX, labelY));
  const int paletteIndex=node.section+1;
  const bool palette=paletteIndex>=0 && paletteIndex<style.themeColorRuleCount
      && paletteIndex<style.cScale.size();
  const bool neo=style.look==QLatin1String("neo");
  const bool redux=style.themeName.contains(QStringLiteral("redux"),Qt::CaseInsensitive);
  const bool reduxColor =
      style.themeName == QLatin1String("redux-color") ||
      style.themeName == QLatin1String("redux-dark-color");
  node.fill=source.isRoot?style.rootFill:(palette?style.cScale[paletteIndex]:style.textColor);
  if (source.isRoot) {
    // mindmap styles.js: `.section-root span { color: redux ? nodeBorder :
    // gitBranchLabel0 }` (htmlLabels) and `.section-root text { fill:
    // gitBranchLabel0 }` (SVG labels). rootTextColor carries gitBranchLabel0;
    // the previous textColor (#333) mismatched the browser default.
    node.label.fill = config.htmlLabels && redux
                          ? style.nodeBorder : style.rootTextColor;
  } else {
    node.label.fill = paletteIndex >= 0 &&
            paletteIndex < style.cScaleLabel.size()
        ? style.cScaleLabel[paletteIndex] : style.textColor;
  }
  if(neo) {
    if (reduxColor && !style.useGradient) {
      node.fill = source.isRoot
          ? style.mainBkg
          : (palette ? style.cScale[paletteIndex] : style.mainBkg);
      node.stroke = palette ? style.cScale[paletteIndex] : style.nodeBorder;
    } else if (!style.useGradient && !redux &&
               style.themeName != QLatin1String("neutral")) {
      // genSections supplies stroke before the later section-root rule
      // overrides fill only. With no generated section rule the SVG shape's
      // presentation stroke remains none.
      node.stroke = palette ? style.cScale[paletteIndex]
                            : QStringLiteral("none");
    } else {
      node.fill=(style.useGradient || redux ||
                 style.themeName.compare(QStringLiteral("neutral"),Qt::CaseInsensitive)==0)
          ?style.mainBkg:node.fill;
      node.stroke=redux?style.nodeBorder:node.fill;
    }
    node.strokeWidth=style.strokeWidth;
    node.dropShadow = !style.dropShadow.trimmed().isEmpty() &&
        style.dropShadow.trimmed().compare(QStringLiteral("none"),
                                           Qt::CaseInsensitive) != 0;
    node.gradient=style.useGradient;
  } else { node.stroke=QStringLiteral("none"); node.strokeWidth=1.0; }
  if(node.bottomLine) {
    node.bottomLineStroke=paletteIndex>=0&&paletteIndex<style.cScaleInv.size()
        ?style.cScaleInv[paletteIndex]:style.lineColor;
    node.bottomLineWidth=neo&&style.useGradient?0.0:3.0;
  }
  node.handDrawn=style.look==QLatin1String("handDrawn") &&
      node.shape != QLatin1String("defaultMindmapNode");
  if(node.handDrawn) {
    rough::Options o; o.seed=config.handDrawnSeed; o.roughness=.7;
    o.fill=style.mainBkg;
    o.fillStyle=QStringLiteral("hachure"); o.fillWeight=4.0; o.hachureGap=5.2;
    o.stroke=style.nodeBorder; o.strokeWidth=1.3;
    if (node.shape == QLatin1String("circle") ||
        node.shape == QLatin1String("doublecircle")) {
      const QRectF circleBounds = pathBounds(node.shapePath);
      node.roughDrawable = rough::ellipse(circleBounds.center().x(),
                                          circleBounds.center().y(),
                                          circleBounds.width(),
                                          circleBounds.height(), o);
    } else {
      node.roughDrawable=rough::path(node.shapePath,o);
    }
    node.paintedBounds=roughBounds(node.roughDrawable,pathBounds(node.shapePath));
    node.localBounds = node.paintedBounds;
    node.layoutBounds = node.paintedBounds.united(node.label.bounds);
  } else {
    node.paintedBounds = transformedShapeBounds;
  }
  if (shapeCss) {
    node.fill = shapeCss->fill;
    node.stroke = shapeCss->stroke;
    node.strokeWidth = editor::cssStrokeWidthPx(
        shapeCss->strokeWidth,
        editor::pieCssLengthContext(measuredStyle.fontFamily,
                                    measuredStyle.fontSize), 0.0);
  }
  if (labelCss) node.label.fill = labelCss->color;
  if (!source.anchors.isEmpty() && config.htmlLabels && measuredStyle.fontSize > 0.0) {
    const qreal lineHeight = measuredStyle.fontSize * 1.5;
    for (const MindmapAnchor& anchor : source.anchors) {
      if (anchor.start < 0 || anchor.length <= 0 ||
          anchor.start + anchor.length > node.label.document.text.size()) continue;
      const qreal before = flowchart::measureFlowTextAdvanceWidth(
          node.label.document, 0, anchor.start,
          measuredStyle.fontFamily, measuredStyle.fontSize);
      const qreal width = flowchart::measureFlowTextAdvanceWidth(
          node.label.document, anchor.start, anchor.length,
          measuredStyle.fontFamily, measuredStyle.fontSize);
      const qreal left = node.label.bounds.left() + before;
      node.anchors.append({anchor.href, anchor.label,
                           QRectF(left, node.label.bounds.top(), width, lineHeight)});
    }
  }
  return node;
}

QVector<QPointF> cytoscapeEdgePoints(const QPointF& start,const QPointF& end) {
  const qreal dx = end.x() - start.x();
  const qreal dy = end.y() - start.y();
  const qreal length = std::sqrt(dx * dx + dy * dy);
  if (length == 0.0) return {start,start,end};
  const QPointF u(dx / length, dy / length);
  return {start+u*15.0,(start+end)/2.0,end-u*15.0};
}

QPointF clipDagreCircleEndpoint(const MindmapNodeGeometry& node,
                                const QPointF& toward) {
  if (node.shape != QLatin1String("circle") &&
      node.shape != QLatin1String("doublecircle"))
    return {};
  const qreal dx = toward.x() - node.center.x();
  const qreal dy = toward.y() - node.center.y();
  const qreal length = std::sqrt(dx * dx + dy * dy);
  if (!(length > 0.0)) return node.center;
  const qreal radius = node.localBounds.width() / 2.0;
  return QPointF(node.center.x() + dx * radius / length,
                 node.center.y() + dy * radius / length);
}

qreal rounded(qreal v) { return std::round(v*1000.0)/1000.0; }
QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"),rounded(r.x())},{QStringLiteral("y"),rounded(r.y())},
          {QStringLiteral("width"),rounded(r.width())},{QStringLiteral("height"),rounded(r.height())}};
}

}  // namespace

MindmapScene buildMindmapScene(const MindmapData& data, MindmapConfig config,
                               MindmapSceneStyle style) {
  MindmapScene scene; scene.config=std::move(config); scene.style=std::move(style);
  scene.effectiveLayout = data.effectiveLayout == QLatin1String("dagre")
      ? QStringLiteral("dagre") : QStringLiteral("cose-bilkent");
  scene.useMaxWidth=jsTruthy(scene.config.useMaxWidth);
  QVector<MindmapCoseNodeInput> coseNodes; QVector<MindmapCoseEdgeInput> coseEdges;
  QMap<QString,QSizeF> measured;
  QHash<int, csscascade::ElementStyle> shapeStyles;
  QHash<int, csscascade::ElementStyle> labelStyles;
  if (!scene.style.themeCss.trimmed().isEmpty()) {
    QVector<MindmapNodeGeometry> fallbackNodes;
    QVector<csscascade::ElementInput> elements;
    csscascade::ElementStyle rootFallback;
    rootFallback.fill = scene.style.textColor;
    rootFallback.stroke = QStringLiteral("none");
    rootFallback.strokeWidth = QStringLiteral("1px");
    rootFallback.color = QStringLiteral("black");
    rootFallback.fontFamily = scene.style.fontFamily;
    rootFallback.fontSize = QString::number(scene.style.fontSize) +
                            QStringLiteral("px");
    elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                     QStringLiteral("diagram-root"),
                     {QStringLiteral("mindmap")}, {}, rootFallback, {}});
    elements.append({QStringLiteral("root"), QStringLiteral("svg"),
                     QStringLiteral("g"), {}, {QStringLiteral("root")}, {},
                     rootFallback, {}});
    elements.append({QStringLiteral("nodes"), QStringLiteral("root"),
                     QStringLiteral("g"), {}, {QStringLiteral("nodes")}, {},
                     rootFallback, {}});
    for (const MindmapNode& source : data.nodes) {
      MindmapNodeGeometry fallback = makeNode(source, scene.config, scene.style);
      fallbackNodes.append(fallback);
      const QString groupKey = QStringLiteral("group-%1").arg(source.id);
      const QString shapeKey = QStringLiteral("shape-%1").arg(source.id);
      const QString labelKey = QStringLiteral("label-%1").arg(source.id);
      elements.append({groupKey, QStringLiteral("nodes"), QStringLiteral("g"),
                       QStringLiteral("diagram-root-node_%1").arg(source.id),
                       {QStringLiteral("node")}, {}, rootFallback, {}});
      csscascade::ElementStyle shapeFallback = rootFallback;
      shapeFallback.fill = fallback.fill;
      shapeFallback.stroke = fallback.stroke;
      shapeFallback.strokeWidth = QString::number(fallback.strokeWidth) +
                                  QStringLiteral("px");
      QString tag = QStringLiteral("path");
      if (fallback.shape == QLatin1String("circle") ||
          fallback.shape == QLatin1String("doublecircle"))
        tag = QStringLiteral("circle");
      else if (fallback.shape == QLatin1String("rect") ||
               fallback.shape == QLatin1String("rounded"))
        tag = QStringLiteral("rect");
      elements.append({shapeKey, groupKey, tag,
                       fallback.shape == QLatin1String("defaultMindmapNode")
                           ? QStringLiteral("diagram-root-node_%1").arg(source.id)
                           : QString(), {}, {}, shapeFallback, {}});
      csscascade::ElementStyle labelFallback = rootFallback;
      labelFallback.color = fallback.label.fill;
      elements.append({labelKey, groupKey, QStringLiteral("span"), {},
                       {QStringLiteral("nodeLabel")}, {}, labelFallback, {}});
    }
    const auto computed = csscascade::resolveElements(
        scene.style.themeCss, elements);
    for (const MindmapNode& source : data.nodes) {
      shapeStyles.insert(source.id, computed.value(
          QStringLiteral("shape-%1").arg(source.id)));
      labelStyles.insert(source.id, computed.value(
          QStringLiteral("label-%1").arg(source.id)));
    }
  }
  for(const MindmapNode& source:data.nodes) {
    const auto shape = shapeStyles.constFind(source.id);
    const auto label = labelStyles.constFind(source.id);
    MindmapNodeGeometry node=makeNode(
        source, scene.config, scene.style,
        shape == shapeStyles.cend() ? nullptr : &shape.value(),
        label == labelStyles.cend() ? nullptr : &label.value());
    coseNodes.append({source.id,node.layoutBounds.size()});
    measured.insert(QString::number(source.id),node.layoutBounds.size());
    scene.nodes.append(std::move(node));
  }
  for(const MindmapEdge& e:data.edges) coseEdges.append({e.start,e.end});

  QVector<QPointF> centers(scene.nodes.size());
  QMap<QString,QVector<QPointF>> dagreEdges;
  if(scene.effectiveLayout==QLatin1String("dagre")) {
    flowchart::FlowchartData f; f.direction=QStringLiteral("TB");
    for(const MindmapNode& n:data.nodes) { flowchart::FlowVertex v; v.id=QString::number(n.id); v.text=n.descr; f.vertices.append(v); }
    for(const MindmapEdge& me:data.edges) { flowchart::FlowEdge e; e.id=me.id; e.start=QString::number(me.start); e.end=QString::number(me.end); f.edges.append(e); }
    flowchart::FlowLayoutOptions options; options.nodeSpacing=50; options.rankSpacing=50;
    const auto layout=flowchart::layoutFlowchartNodesDagre(f,measured,options);
    for(const auto& n:layout.nodes) centers[n.id.toInt()]=QPointF(n.x,n.y);
    for(const auto& e:layout.edges) dagreEdges.insert(e.id,e.points);
    // The rendering-util Dagre wrapper translates its graph so the measured
    // node bbox starts at the generic renderer's 8px graph inset. The shared
    // Dagre primitive deliberately returns layout-space coordinates, so apply
    // that wrapper translation here (including routed edge points).
    QRectF dagreNodeBounds;
    bool firstDagreNode = true;
    for (int i = 0; i < scene.nodes.size(); ++i) {
      const QRectF b = scene.nodes[i].layoutBounds.translated(centers.value(i));
      if (firstDagreNode) { dagreNodeBounds = b; firstDagreNode = false; }
      else dagreNodeBounds = dagreNodeBounds.united(b);
    }
    if (!firstDagreNode) {
      const QPointF shift(8.0 - dagreNodeBounds.left(),
                          8.0 - dagreNodeBounds.top());
      for (QPointF& center : centers) center += shift;
      for (auto it = dagreEdges.begin(); it != dagreEdges.end(); ++it)
        for (QPointF& point : it.value()) point += shift;
    }
  } else {
    centers=layoutMindmapCoseFlatTree(coseNodes,coseEdges).centers;
  }
  for(int i=0;i<scene.nodes.size();++i) scene.nodes[i].center=centers.value(i);
  for (const MindmapNodeGeometry& node : std::as_const(scene.nodes)) {
    for (const MindmapAnchorGeometry& anchor : node.anchors) {
      InteractionRegion region;
      region.bounds = anchor.bounds.translated(node.center);
      region.href = anchor.href;
      region.accessibleLabel = anchor.label;
      scene.interactions.append(std::move(region));
    }
  }

  for(const MindmapEdge& source:data.edges) {
    MindmapEdgeGeometry edge; edge.id=source.id; edge.start=source.start; edge.end=source.end;
    edge.depth=source.depth; edge.section=source.hasSection?source.section:-1;
    edge.points=dagreEdges.contains(source.id)?dagreEdges.value(source.id)
        :cytoscapeEdgePoints(centers.value(source.start),centers.value(source.end));
    if (dagreEdges.contains(source.id) && edge.points.size() >= 2) {
      const MindmapNodeGeometry& startNode = scene.nodes.at(source.start);
      const QPointF clippedStart = clipDagreCircleEndpoint(startNode, edge.points.at(1));
      if (!clippedStart.isNull()) edge.points[0] = clippedStart;
      const MindmapNodeGeometry& endNode = scene.nodes.at(source.end);
      const QPointF clippedEnd = clipDagreCircleEndpoint(
          endNode, edge.points.at(edge.points.size() - 2));
      if (!clippedEnd.isNull()) edge.points.last() = clippedEnd;
    }
    edge.path=flowchart::d3curve::pathForCurve(edge.points,QStringLiteral("basis"));
    edge.handDrawn = scene.style.look == QLatin1String("handDrawn");
    if (edge.handDrawn) {
      rough::Options options;
      options.seed = scene.config.handDrawnSeed;
      options.roughness = 0.3;
      edge.roughDrawable = rough::path(scene::parseSvgPath(edge.path), options);
      edge.path = roughPathData(edge.roughDrawable);
    }
    QPainterPath p; if(!edge.points.isEmpty()){p.moveTo(edge.points.first()); for(int i=1;i<edge.points.size();++i)p.lineTo(edge.points[i]);}
    edge.bounds=p.controlPointRect();
    const int pi=edge.section+1; const bool neo=scene.style.look==QLatin1String("neo");
    const bool redux=scene.style.themeName.contains(QStringLiteral("redux"),Qt::CaseInsensitive);
    const bool hasSectionRule = pi >= 0 &&
        pi < scene.style.themeColorRuleCount && pi < scene.style.cScale.size();
    edge.stroke = !hasSectionRule
        ? QStringLiteral("none")
        : (neo && redux ? scene.style.nodeBorder : scene.style.cScale[pi]);
    const int renderedDepth = edge.depth + 1;
    const int ruleIndex = renderedDepth + 1;
    edge.strokeWidth = ruleIndex < scene.style.themeColorRuleCount
        ? (neo
               ? std::max<qreal>(10.0 - renderedDepth * 2.0, 2.0)
               : 14.0 - renderedDepth * 3.0)
        : 3.0;
    scene.edges.append(std::move(edge));
  }
  bool first=true; QRectF content;
  for(const auto& n:scene.nodes) {
    // setupGraphViewbox runs against the node groups as they were measured
    // when inserted into Cytoscape. SVG text is measured again after the root
    // viewBox/CSS size is installed, but that later used-value must not feed
    // back into the already-computed viewport.
    const QRectF b = translated(n.layoutBounds, n.center);
    if(first){content=b;first=false;}else content=content.united(b);
  }
  for(const auto& e:scene.edges) { if(first){content=e.bounds;first=false;}else content=content.united(e.bounds); }
  scene.contentBounds=content;
  qreal padding=scene.config.padding.isNull()||scene.config.padding.isUndefined()?10.0:
      editor::jsNumberValue(scene.config.padding);
  if(!std::isfinite(padding)) padding=0.0;
  scene.bounds=content.adjusted(-padding,-padding,padding,padding);
  if (!scene.config.htmlLabels && scene.bounds.width() > 0.0 &&
      scene.bounds.height() > 0.0) {
    // Blink stores the used SVG viewport in 1/64px LayoutUnits. The width is
    // quantized first; the auto height is then derived from the aspect ratio
    // and quantized independently. getBBox() converts glyph ink back through
    // the resulting preserveAspectRatio meet scale.
    const qreal clientWidth =
        std::floor(scene.bounds.width() * 64.0) / 64.0;
    const qreal clientHeight = std::floor(
        clientWidth * scene.bounds.height() / scene.bounds.width() * 64.0) /
        64.0;
    const qreal scale = std::min(clientWidth / scene.bounds.width(),
                                 clientHeight / scene.bounds.height());
    if (scale > 0.0 && std::isfinite(scale) && scale != 1.0) {
      for (MindmapNodeGeometry& node : scene.nodes) {
        node.label.bounds.setWidth(node.label.bounds.width() / scale);
        node.label.bounds.setHeight(node.label.bounds.height() / scale);
      }
    }
  }
  return scene;
}

void MindmapScene::paint(QPainter& painter,const MermaidPaintOptions& options) const {
  paintMindmapScene(*this,painter,options);
}

QJsonObject MindmapScene::toJsonObject() const {
  QJsonObject out{{QStringLiteral("bounds"),rectJson(bounds)},
                  {QStringLiteral("contentBounds"),rectJson(contentBounds)},
                  {QStringLiteral("layout"),effectiveLayout}};
  QJsonArray ns;
  for(const auto& n:nodes) ns.append(QJsonObject{{QStringLiteral("id"),n.nodeId},
      {QStringLiteral("centerX"),rounded(n.center.x())},{QStringLiteral("centerY"),rounded(n.center.y())},
      {QStringLiteral("shape"),n.shape},{QStringLiteral("bounds"),rectJson(n.localBounds)},
      {QStringLiteral("labelBounds"),rectJson(n.label.bounds)},{QStringLiteral("fill"),n.fill}});
  QJsonArray es;
  for(const auto& e:edges) { QJsonArray pts; for(const auto&p:e.points)pts.append(QJsonArray{rounded(p.x()),rounded(p.y())});
    es.append(QJsonObject{{QStringLiteral("id"),e.id},{QStringLiteral("points"),pts},
      {QStringLiteral("path"),e.path},{QStringLiteral("stroke"),e.stroke},{QStringLiteral("strokeWidth"),e.strokeWidth}}); }
  out.insert(QStringLiteral("nodes"),ns); out.insert(QStringLiteral("edges"),es); return out;
}

}  // namespace muffin::mermaid::mindmap
