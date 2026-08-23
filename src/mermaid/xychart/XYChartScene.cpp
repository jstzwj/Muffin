#include "mermaid/xychart/XYChartScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/text/LabelText.h"
#include "mermaid/xychart/XYChartScenePainter.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace muffin::mermaid::xychart {
namespace {

constexpr qreal kBarWidthToTickWidthRatio = 0.7;
constexpr qreal kMaxOuterPaddingPercent = 0.2;

struct Dimension { qreal width = 0.0; qreal height = 0.0; };

Dimension textDimension(const QStringList& texts, qreal size,
                        const QString& family, qreal rootSize = 16.0) {
  const qreal usedSize = size < 0.0 || !std::isfinite(size) ? rootSize : size;
  if (!(usedSize > 0.0)) return {};
  const editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(family, usedSize);
  if (!(font.scale > 0.0)) return {};
  const QFontMetricsF metrics(font.font);
  Dimension result;
  for (const QString& raw : texts) {
    const QString text = text::collapsedSvgText(raw);
    result.width = std::max(result.width,
                            metrics.horizontalAdvance(text) * font.scale);
    result.height = std::max(result.height, metrics.height() * font.scale);
  }
  return result;
}

QString number(qreal value) {
  return editor::jsNumberToString(double(value));
}

QString pathNumber(qreal value) {
  if (!std::isfinite(value)) return number(value);
  qreal rounded = std::round(value * 1000.0) / 1000.0;
  if (rounded == 0.0) rounded = 0.0;
  return editor::jsNumberToString(double(rounded));
}

struct TickSpec { qint64 i1 = 0; qint64 i2 = -1; qreal inc = 0.0; };

TickSpec tickSpec(qreal start, qreal stop, qreal count) {
  const qreal step = (stop - start) / std::max<qreal>(0.0, count);
  const qreal power = std::floor(std::log10(step));
  const qreal error = step / std::pow(10.0, power);
  const qreal factor = error >= std::sqrt(50.0) ? 10.0
                     : error >= std::sqrt(10.0) ? 5.0
                     : error >= std::sqrt(2.0) ? 2.0 : 1.0;
  qint64 i1 = 0;
  qint64 i2 = -1;
  qreal inc = 0.0;
  if (power < 0.0) {
    inc = std::pow(10.0, -power) / factor;
    i1 = qint64(std::llround(start * inc));
    if (qreal(i1) / inc < start) ++i1;
    i2 = qint64(std::llround(stop * inc));
    if (qreal(i2) / inc > stop) --i2;
    inc = -inc;
  } else {
    inc = std::pow(10.0, power) * factor;
    i1 = qint64(std::llround(start / inc));
    if (qreal(i1) * inc < start) ++i1;
    i2 = qint64(std::llround(stop / inc));
    if (qreal(i2) * inc > stop) --i2;
  }
  if (i2 < i1 && 0.5 <= count && count < 2.0)
    return tickSpec(start, stop, count * 2.0);
  return {i1, i2, inc};
}

QVector<qreal> d3Ticks(qreal start, qreal stop, qreal count = 10.0) {
  if (!(count > 0.0) || !std::isfinite(start) || !std::isfinite(stop)) return {};
  if (start == stop) return {start};
  const bool reverse = stop < start;
  if (reverse) std::swap(start, stop);
  const TickSpec spec = tickSpec(start, stop, count);
  const qint64 n = spec.i2 >= spec.i1 ? spec.i2 - spec.i1 + 1 : 0;
  QVector<qreal> result;
  result.reserve(int(std::min<qint64>(n, 100000)));
  for (qint64 i = 0; i < n && i < 100000; ++i) {
    const qint64 index = reverse ? spec.i2 - i : spec.i1 + i;
    result.append(spec.inc < 0.0 ? qreal(index) / -spec.inc
                                : qreal(index) * spec.inc);
  }
  return result;
}

enum class AxisPosition { Left, Right, Bottom, Top };

struct AxisTheme {
  QString titleColor;
  QString labelColor;
  QString tickColor;
  QString lineColor;
};

class AxisState {
public:
  AxisState(const XYChartAxisData& data, const XYChartAxisConfig& config,
            AxisTheme theme, QString family, qreal rootFontSize, QString prefix)
      : data_(data), config_(config), theme_(std::move(theme)),
        family_(std::move(family)), rootFontSize_(rootFontSize),
        prefix_(std::move(prefix)) {
    if (config_.labelRotation >= -90.0 && config_.labelRotation <= 90.0)
      rotation_ = config_.labelRotation * M_PI / 180.0;
  }

  void setPosition(AxisPosition value) { position_ = value; setRange(range_); }
  void setRange(QPair<qreal,qreal> value) { range_ = value; }
  void setOrigin(QPointF value) { bounds_.moveTopLeft(value); }
  qreal outerPadding() const { return outerPadding_; }
  QRectF bounds() const { return bounds_; }

  QVector<QString> tickStrings() const {
    if (data_.type == XYChartAxisType::Band)
      return QVector<QString>(data_.categories.cbegin(), data_.categories.cend());
    QVector<QString> result;
    for (qreal tick : d3Ticks(data_.min, data_.max)) result.append(number(tick));
    if (position_ == AxisPosition::Left) std::reverse(result.begin(), result.end());
    return result;
  }

  QVector<qreal> tickNumbers() const {
    QVector<qreal> result = data_.type == XYChartAxisType::Linear
                                ? d3Ticks(data_.min, data_.max)
                                : QVector<qreal>();
    if (position_ == AxisPosition::Left) std::reverse(result.begin(), result.end());
    return result;
  }

  qreal scale(const QString& category) const {
    if (data_.type == XYChartAxisType::Linear)
      return scale(category.toDouble());
    const auto first = std::find(data_.categories.cbegin(), data_.categories.cend(), category);
    if (first == data_.categories.cend()) return effectiveRange().first;
    const qsizetype index = std::distance(data_.categories.cbegin(), first);
    const int n = data_.categories.size();
    if (n <= 1) return (effectiveRange().first + effectiveRange().second) / 2.0;
    return effectiveRange().first +
           (effectiveRange().second - effectiveRange().first) * qreal(index) / qreal(n - 1);
  }

  qreal scale(qreal value) const {
    QPair<qreal,qreal> r = effectiveRange();
    qreal d0 = data_.min;
    qreal d1 = data_.max;
    if (position_ == AxisPosition::Left) std::swap(d0, d1);
    if (d0 == d1) return (r.first + r.second) / 2.0;
    return r.first + (value - d0) / (d1 - d0) * (r.second - r.first);
  }

  qreal tickDistance() const {
    const int n = data_.type == XYChartAxisType::Band
                      ? data_.categories.size() : d3Ticks(data_.min, data_.max).size();
    if (n == 0) return std::numeric_limits<qreal>::infinity();
    const auto r = effectiveRange();
    return std::abs(r.first - r.second) / qreal(n);
  }

  void recalculateOuterPaddingForBars() {
    if (kBarWidthToTickWidthRatio * tickDistance() > outerPadding_ * 2.0)
      outerPadding_ = std::floor(kBarWidthToTickWidthRatio * tickDistance() / 2.0);
  }

  QSizeF calculateSpace(QSizeF available) {
    showTitle_ = showLabel_ = showTick_ = showLine_ = false;
    if (position_ == AxisPosition::Left || position_ == AxisPosition::Right)
      calculateVertical(available);
    else
      calculateHorizontal(available);
    return bounds_.size();
  }

  void appendGeometry(XYChartScene& scene) const {
    switch (position_) {
      case AxisPosition::Left: appendLeft(scene); break;
      case AxisPosition::Bottom: appendBottom(scene); break;
      case AxisPosition::Top: appendTop(scene); break;
      case AxisPosition::Right: break;
    }
  }

private:
  QPair<qreal,qreal> effectiveRange() const {
    return {range_.first + outerPadding_, range_.second - outerPadding_};
  }

  Dimension labels() const {
    QStringList values;
    for (const QString& value : tickStrings()) values.append(value);
    return textDimension(values, config_.labelFontSize, family_, rootFontSize_);
  }

  void calculateHorizontal(QSizeF available) {
    qreal left = available.height();
    if (config_.showAxisLine && left > config_.axisLineWidth) {
      left -= config_.axisLineWidth; showLine_ = true;
    }
    if (config_.showLabel) {
      const Dimension d = labels();
      outerPadding_ = std::min(d.width / 2.0,
                               kMaxOuterPaddingPercent * available.width());
      qreal needed = d.height;
      if (position_ == AxisPosition::Bottom && rotation_ != 0.0)
        needed = std::max(needed, std::abs(std::sin(rotation_) * d.width) +
                                  std::abs(std::cos(rotation_) * d.height));
      needed += config_.labelPadding * 2.0;
      labelHeight_ = d.height;
      if (needed <= left) { left -= needed; showLabel_ = true; }
    }
    if (config_.showTick && left >= config_.tickLength) {
      left -= config_.tickLength; showTick_ = true;
    }
    if (config_.showTitle && !data_.title.isEmpty()) {
      const Dimension d = textDimension({data_.title}, config_.titleFontSize,
                                        family_, rootFontSize_);
      const qreal needed = d.height + config_.titlePadding * 2.0;
      titleHeight_ = d.height;
      if (needed <= left) { left -= needed; showTitle_ = true; }
    }
    bounds_.setSize(QSizeF(available.width(), available.height() - left));
  }

  void calculateVertical(QSizeF available) {
    qreal left = available.width();
    if (config_.showAxisLine && left > config_.axisLineWidth) {
      left -= config_.axisLineWidth; showLine_ = true;
    }
    if (config_.showLabel) {
      const Dimension d = labels();
      outerPadding_ = std::min(d.height / 2.0,
                               kMaxOuterPaddingPercent * available.height());
      const qreal needed = d.width + config_.labelPadding * 2.0;
      if (needed <= left) { left -= needed; showLabel_ = true; }
    }
    if (config_.showTick && left >= config_.tickLength) {
      left -= config_.tickLength; showTick_ = true;
    }
    if (config_.showTitle && !data_.title.isEmpty()) {
      const Dimension d = textDimension({data_.title}, config_.titleFontSize,
                                        family_, rootFontSize_);
      const qreal needed = d.height + config_.titlePadding * 2.0;
      titleHeight_ = d.height;
      if (needed <= left) { left -= needed; showTitle_ = true; }
    }
    bounds_.setSize(QSizeF(available.width() - left, available.height()));
  }

  void addPath(XYChartScene& scene, QString suffix, QString path,
               QString stroke, qreal width) const {
    scene.paths.append({prefix_ + QLatin1Char('/') + suffix, std::move(path), {},
                        QStringLiteral("none"), std::move(stroke), width});
    scene.paths.back().paintOrder = scene.nextPaintOrder++;
  }

  void addText(XYChartScene& scene, QString suffix, QString text, QPointF p,
               QString fill, qreal size, qreal rotation,
               XYChartTextAnchor anchor, XYChartBaseline baseline) const {
    scene.texts.append({prefix_ + QLatin1Char('/') + suffix, std::move(text), p,
                        std::move(fill), size, rotation, anchor, baseline});
    scene.texts.back().paintOrder = scene.nextPaintOrder++;
  }

  void appendLeft(XYChartScene& scene) const {
    if (showLine_) {
      const qreal x = bounds_.right() - config_.axisLineWidth / 2.0;
      addPath(scene, QStringLiteral("axisl-line"),
              QStringLiteral("M %1,%2 L %1,%3 ").arg(number(x), number(range_.first), number(range_.second)),
              theme_.lineColor, config_.axisLineWidth);
    }
    const QVector<QString> labels = tickStrings();
    const QVector<qreal> values = tickNumbers();
    for (int i = 0; showLabel_ && i < labels.size(); ++i) {
      const qreal y = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
      const qreal x = bounds_.right() - (showLabel_ ? config_.labelPadding : 0.0) -
                      (showTick_ ? config_.tickLength : 0.0) -
                      (showLine_ ? config_.axisLineWidth : 0.0);
      addText(scene, QStringLiteral("label"), labels[i], {x,y}, theme_.labelColor,
              config_.labelFontSize, 0.0, XYChartTextAnchor::End, XYChartBaseline::Middle);
    }
    if (showTick_) {
      const qreal x = bounds_.right() - (showLine_ ? config_.axisLineWidth : 0.0);
      for (int i = 0; i < labels.size(); ++i) {
        const qreal y = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
        addPath(scene, QStringLiteral("ticks"),
                QStringLiteral("M %1,%2 L %3,%2").arg(number(x), number(y), number(x-config_.tickLength)),
                theme_.tickColor, config_.tickWidth);
      }
    }
    if (showTitle_)
      addText(scene, QStringLiteral("title"), data_.title,
              {bounds_.left()+config_.titlePadding, (range_.first+range_.second)/2.0},
              theme_.titleColor, config_.titleFontSize, 270.0,
              XYChartTextAnchor::Middle, XYChartBaseline::BeforeEdge);
  }

  void appendBottom(XYChartScene& scene) const {
    if (showLine_) {
      const qreal y = bounds_.top() + config_.axisLineWidth / 2.0;
      addPath(scene, QStringLiteral("axis-line"),
              QStringLiteral("M %1,%2 L %3,%2").arg(number(range_.first),number(y),number(range_.second)),
              theme_.lineColor, config_.axisLineWidth);
    }
    const QVector<QString> labels = tickStrings();
    const QVector<qreal> values = tickNumbers();
    for (int i = 0; showLabel_ && i < labels.size(); ++i) {
      const qreal x0 = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
      const Dimension d = labels.isEmpty() ? Dimension{} : this->labels();
      const qreal x = x0 + std::sin(rotation_) * d.height / 2.0;
      const qreal y = bounds_.top() + config_.labelPadding +
                      (showTick_ ? config_.tickLength : 0.0) +
                      (showLine_ ? config_.axisLineWidth : 0.0) +
                      std::abs(std::sin(rotation_) * d.width / 2.0);
      addText(scene, QStringLiteral("label"), labels[i], {x,y}, theme_.labelColor,
              config_.labelFontSize, rotation_*180.0/M_PI,
              XYChartTextAnchor::Middle, XYChartBaseline::BeforeEdge);
    }
    if (showTick_) {
      const qreal y = bounds_.top() + (showLine_ ? config_.axisLineWidth : 0.0);
      for (int i = 0; i < labels.size(); ++i) {
        const qreal x = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
        addPath(scene, QStringLiteral("ticks"),
                QStringLiteral("M %1,%2 L %1,%3").arg(number(x),number(y),number(y+config_.tickLength)),
                theme_.tickColor, config_.tickWidth);
      }
    }
    if (showTitle_)
      addText(scene, QStringLiteral("title"), data_.title,
              {(range_.first+range_.second)/2.0, bounds_.bottom()-config_.titlePadding-titleHeight_},
              theme_.titleColor, config_.titleFontSize, 0.0,
              XYChartTextAnchor::Middle, XYChartBaseline::BeforeEdge);
  }

  void appendTop(XYChartScene& scene) const {
    if (showLine_) {
      const qreal y = bounds_.bottom() - config_.axisLineWidth / 2.0;
      addPath(scene, QStringLiteral("axis-line"),
              QStringLiteral("M %1,%2 L %3,%2").arg(number(range_.first),number(y),number(range_.second)),
              theme_.lineColor, config_.axisLineWidth);
    }
    const QVector<QString> labels = tickStrings();
    const QVector<qreal> values = tickNumbers();
    for (int i = 0; showLabel_ && i < labels.size(); ++i) {
      const qreal x = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
      const qreal y = bounds_.top() + (showTitle_ ? titleHeight_ + config_.titlePadding*2.0 : 0.0) + config_.labelPadding;
      addText(scene, QStringLiteral("label"), labels[i], {x,y}, theme_.labelColor,
              config_.labelFontSize, 0.0, XYChartTextAnchor::Middle, XYChartBaseline::BeforeEdge);
    }
    if (showTick_) {
      for (int i = 0; i < labels.size(); ++i) {
        const qreal x = data_.type == XYChartAxisType::Band ? scale(labels[i]) : scale(values[i]);
        const qreal y1 = bounds_.bottom()-(showLine_?config_.axisLineWidth:0.0);
        addPath(scene, QStringLiteral("ticks"),
                QStringLiteral("M %1,%2 L %1,%3").arg(number(x),number(y1),number(y1-config_.tickLength)),
                theme_.tickColor, config_.tickWidth);
      }
    }
    if (showTitle_)
      addText(scene, QStringLiteral("title"), data_.title,
              {(range_.first+range_.second)/2.0, bounds_.top()+config_.titlePadding},
              theme_.titleColor, config_.titleFontSize, 0.0,
              XYChartTextAnchor::Middle, XYChartBaseline::BeforeEdge);
  }

  XYChartAxisData data_;
  XYChartAxisConfig config_;
  AxisTheme theme_;
  QString family_;
  qreal rootFontSize_ = 16.0;
  QString prefix_;
  AxisPosition position_ = AxisPosition::Left;
  QPair<qreal,qreal> range_{0.0,10.0};
  QRectF bounds_;
  qreal outerPadding_ = 0.0;
  qreal titleHeight_ = 0.0;
  qreal labelHeight_ = 0.0;
  qreal rotation_ = 0.0;
  bool showTitle_ = false, showLabel_ = false, showTick_ = false, showLine_ = false;
};

QString plotColor(const XYChartSceneStyle& style, int index) {
  if (style.plotColorPalette.isEmpty()) return QString();
  return style.plotColorPalette.at(index == 0 ? 0 : index % style.plotColorPalette.size()).trimmed();
}

void appendPlots(XYChartScene& scene, const XYChartData& data,
                 const AxisState& xAxis, const AxisState& yAxis) {
  QVector<double> firstValues;
  if (!data.plots.isEmpty())
    for (const XYChartPoint& point : data.plots.front().points) firstValues.append(point.value);
  for (int p = 0; p < data.plots.size(); ++p) {
    const XYChartPlotData& plot = data.plots[p];
    const QString color = plotColor(scene.style, plot.paletteIndex);
    QVector<QPointF> points;
    for (const XYChartPoint& point : plot.points) {
      const qreal value = point.valueDefined
                              ? qreal(point.value)
                              : std::numeric_limits<qreal>::quiet_NaN();
      const QPointF xy(xAxis.scale(point.category), yAxis.scale(value));
      points.append(scene.config.orientation == XYChartOrientation::Horizontal
                        ? QPointF(xy.y(), xy.x()) : xy);
    }
    const QString group = QStringLiteral("plot/%1-plot-%2")
                              .arg(plot.type == XYChartPlotType::Line ? QStringLiteral("line") : QStringLiteral("bar"))
                              .arg(p);
    if (plot.type == XYChartPlotType::Line) {
      QString d;
      for (int i = 0; i < points.size(); ++i)
        d += (i == 0 ? QLatin1Char('M') : QLatin1Char('L')) +
             pathNumber(points[i].x()) + QLatin1Char(',') + pathNumber(points[i].y());
      if (points.size() == 1) d += QLatin1Char('Z');
      if (!d.isEmpty()) {
        scene.paths.append({group,d,points,QStringLiteral("none"),color,2.0});
        scene.paths.back().paintOrder = scene.nextPaintOrder++;
      }
      if (plot.hasPointLabels) {
        for (int i = 0; i < points.size() && i < plot.pointLabels.size(); ++i) {
          if (plot.pointLabels[i].isEmpty()) continue;
          const bool horizontal = scene.config.orientation == XYChartOrientation::Horizontal;
          scene.texts.append({group+QStringLiteral("/labels"), plot.pointLabels[i],
                              points[i]+(horizontal?QPointF(10,0):QPointF(0,-10)),
                              color,12.0,0.0,
                              horizontal?XYChartTextAnchor::Start:XYChartTextAnchor::Middle,
                              XYChartBaseline::Middle});
          scene.texts.back().paintOrder = scene.nextPaintOrder++;
        }
      }
      continue;
    }

    const qreal barWidth = std::min(xAxis.outerPadding()*2.0, xAxis.tickDistance())*0.95;
    QVector<XYChartRectGeometry> bars;
    for (const QPointF& point : points) {
      QRectF rect;
      if (scene.config.orientation == XYChartOrientation::Horizontal)
        rect = QRectF(scene.plotBounds.left(), point.y()-barWidth/2.0,
                      point.x()-scene.plotBounds.left(), barWidth);
      else
        rect = QRectF(point.x()-barWidth/2.0, point.y(), barWidth,
                      scene.plotBounds.bottom()-point.y());
      bars.append({group,rect,color,color,0.0});
      scene.rects.append(bars.back());
      scene.rects.back().paintOrder = scene.nextPaintOrder++;
    }
    if (!scene.config.showDataLabel) continue;
    struct LabelItem { QRectF rect; QString label; };
    QVector<LabelItem> labels;
    for (int i=0;i<bars.size();++i) {
      if (i >= data.plots.front().points.size() ||
          !data.plots.front().points.at(i).valueDefined)
        throw std::runtime_error("Cannot read properties of undefined (reading 'toString')");
      if (!(bars[i].rect.width()>0 && bars[i].rect.height()>0)) continue;
      labels.append({bars[i].rect,number(firstValues[i])});
    }
    if (labels.isEmpty()) continue;
    qreal uniform = std::numeric_limits<qreal>::infinity();
    if (scene.config.orientation == XYChartOrientation::Horizontal) {
      for (const LabelItem& item : labels) {
        qreal fs = item.rect.height()*0.7;
        while (fs*item.label.size()*0.7 > item.rect.width()-10.0 && fs>0.0) fs-=1.0;
        uniform = std::min(uniform,fs);
      }
    } else {
      for (const LabelItem& item : labels) {
        qreal fs = item.rect.width()/(item.label.size()*0.7);
        while (fs>0.0) {
          const qreal tw=fs*item.label.size()*0.7;
          const bool hf=item.rect.center().x()-tw/2.0>=item.rect.left() && item.rect.center().x()+tw/2.0<=item.rect.right();
          const bool vf=item.rect.top()+10.0+fs<=item.rect.bottom();
          if(hf&&vf) break;
          fs-=1.0;
        }
        uniform=std::min(uniform,fs);
      }
    }
    uniform=std::floor(uniform);
    for(const LabelItem& item:labels){
      QPointF pos; XYChartTextAnchor anchor; XYChartBaseline baseline;
      if(scene.config.orientation==XYChartOrientation::Horizontal){
        pos={item.rect.right()+(scene.config.showDataLabelOutsideBar?10.0:-10.0),item.rect.center().y()};
        anchor=scene.config.showDataLabelOutsideBar?XYChartTextAnchor::Start:XYChartTextAnchor::End;
        baseline=XYChartBaseline::Middle;
      }else{
        pos={item.rect.center().x(),item.rect.top()+(scene.config.showDataLabelOutsideBar?-10.0:10.0)};
        anchor=XYChartTextAnchor::Middle;
        baseline=scene.config.showDataLabelOutsideBar?XYChartBaseline::Auto:XYChartBaseline::Hanging;
      }
      scene.texts.append({group,item.label,pos,
                          scene.style.dataLabelColor,uniform,0.0,anchor,baseline});
      scene.texts.back().paintOrder = scene.nextPaintOrder++;
    }
  }
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"),p.x()},{QStringLiteral("y"),p.y()}};
}

}  // namespace

XYChartScene buildXYChartScene(const XYChartData& data, XYChartConfig config,
                               XYChartSceneStyle style) {
  XYChartScene scene;
  scene.bounds=QRectF(0,0,config.width,config.height);
  scene.config=config;
  scene.style=std::move(style);
  scene.title=data.title;
  scene.accTitle=data.accTitle;
  scene.accDescr=data.accDescr;

  AxisState xAxis(data.xAxis,config.xAxis,
                  {scene.style.xAxisTitleColor,scene.style.xAxisLabelColor,
                   scene.style.xAxisTickColor,scene.style.xAxisLineColor},
                  scene.style.fontFamily,scene.style.rootFontSize,
                  QStringLiteral("x-axis"));
  AxisState yAxis(data.yAxis,config.yAxis,
                  {scene.style.yAxisTitleColor,scene.style.yAxisLabelColor,
                   scene.style.yAxisTickColor,scene.style.yAxisLineColor},
                  scene.style.fontFamily,scene.style.rootFontSize,
                  QStringLiteral("y-axis"));
  qreal availableWidth=config.width, availableHeight=config.height;
  qreal chartWidth=std::floor(availableWidth*config.plotReservedSpacePercent/100.0);
  qreal chartHeight=std::floor(availableHeight*config.plotReservedSpacePercent/100.0);
  availableWidth-=chartWidth; availableHeight-=chartHeight;

  qreal titleHeight=0.0;
  if(config.showTitle&&!data.title.isEmpty()){
    const Dimension d=textDimension({data.title},config.titleFontSize,
                                    scene.style.fontFamily,
                                    scene.style.rootFontSize);
    const qreal required=d.height+2.0*config.titlePadding;
    if(d.width<=std::max(d.width,config.width)&&d.height<=required){
      titleHeight=required;
      scene.texts.append({QStringLiteral("chart-title"),data.title,
                          {std::max(config.width,d.width)/2.0,titleHeight/2.0},
                          scene.style.titleColor,
                          config.titleFontSize,0.0,XYChartTextAnchor::Middle,
                          XYChartBaseline::Middle});
      scene.texts.back().paintOrder = scene.nextPaintOrder++;
    }
  }
  availableHeight-=titleHeight;
  qreal plotX=0.0,plotY=0.0;
  if(config.orientation==XYChartOrientation::Horizontal){
    xAxis.setPosition(AxisPosition::Left);
    const QSizeF xs=xAxis.calculateSpace({availableWidth,availableHeight});
    availableWidth-=xs.width(); plotX=xs.width();
    yAxis.setPosition(AxisPosition::Top);
    const QSizeF ys=yAxis.calculateSpace({availableWidth,availableHeight});
    availableHeight-=ys.height(); plotY=titleHeight+ys.height();
    if(availableWidth>0) chartWidth+=availableWidth;
    if(availableHeight>0) chartHeight+=availableHeight;
    scene.plotBounds=QRectF(plotX,plotY,chartWidth,chartHeight);
    yAxis.setRange({plotX,plotX+chartWidth}); yAxis.setOrigin({plotX,titleHeight});
    xAxis.setRange({plotY,plotY+chartHeight}); xAxis.setOrigin({0,plotY});
  }else{
    plotY=titleHeight;
    xAxis.setPosition(AxisPosition::Bottom);
    const QSizeF xs=xAxis.calculateSpace({availableWidth,availableHeight});
    availableHeight-=xs.height();
    yAxis.setPosition(AxisPosition::Left);
    const QSizeF ys=yAxis.calculateSpace({availableWidth,availableHeight});
    plotX=ys.width(); availableWidth-=ys.width();
    if(availableWidth>0) chartWidth+=availableWidth;
    if(availableHeight>0) chartHeight+=availableHeight;
    scene.plotBounds=QRectF(plotX,plotY,chartWidth,chartHeight);
    xAxis.setRange({plotX,plotX+chartWidth}); xAxis.setOrigin({plotX,plotY+chartHeight});
    yAxis.setRange({plotY,plotY+chartHeight}); yAxis.setOrigin({0,plotY});
  }
  if(std::any_of(data.plots.cbegin(),data.plots.cend(),[](const XYChartPlotData& p){return p.type==XYChartPlotType::Bar;}))
    xAxis.recalculateOuterPaddingForBars();
  appendPlots(scene,data,xAxis,yAxis);
  xAxis.appendGeometry(scene);
  yAxis.appendGeometry(scene);
  return scene;
}

void XYChartScene::paint(QPainter& painter,const MermaidPaintOptions& options) const {
  paintXYChartScene(*this,painter,options);
}

QJsonObject XYChartScene::toJsonObject() const {
  QJsonArray textJson,pathJson,rectJson;
  for(const auto& t:texts) textJson.append(QJsonObject{{QStringLiteral("group"),t.group},{QStringLiteral("text"),t.text},{QStringLiteral("x"),t.position.x()},{QStringLiteral("y"),t.position.y()},{QStringLiteral("fill"),t.fill},{QStringLiteral("fontSize"),t.fontSize},{QStringLiteral("rotation"),t.rotation}});
  for(const auto& p:paths) pathJson.append(QJsonObject{{QStringLiteral("group"),p.group},{QStringLiteral("d"),p.path},{QStringLiteral("fill"),p.fill},{QStringLiteral("stroke"),p.stroke},{QStringLiteral("strokeWidth"),p.strokeWidth}});
  for(const auto& r:rects) rectJson.append(QJsonObject{{QStringLiteral("group"),r.group},{QStringLiteral("x"),r.rect.x()},{QStringLiteral("y"),r.rect.y()},{QStringLiteral("width"),r.rect.width()},{QStringLiteral("height"),r.rect.height()},{QStringLiteral("fill"),r.fill}});
  return {{QStringLiteral("bounds"),QJsonObject{{QStringLiteral("x"),bounds.x()},{QStringLiteral("y"),bounds.y()},{QStringLiteral("width"),bounds.width()},{QStringLiteral("height"),bounds.height()}}},{QStringLiteral("plotBounds"),QJsonObject{{QStringLiteral("x"),plotBounds.x()},{QStringLiteral("y"),plotBounds.y()},{QStringLiteral("width"),plotBounds.width()},{QStringLiteral("height"),plotBounds.height()}}},{QStringLiteral("texts"),textJson},{QStringLiteral("paths"),pathJson},{QStringLiteral("rects"),rectJson}};
}

}  // namespace muffin::mermaid::xychart
