#include "mermaid/journey/JourneyScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <optional>

namespace muffin::mermaid::journey {
namespace {

struct PaintState {
  bool none = false;
  QColor color = Qt::black;
};

PaintState rootSvgFill(const QString& value) {
  const QString text = value.trimmed();
  const QString keyword = text.toLower();
  if (keyword == QLatin1String("none")) return {true, {}};
  if (color::isParsableColor(text)) return {false, color::toQColor(text)};
  // The SVG root inherits the CSS initial color/fill from its host. All CSS
  // globals, an empty declaration, and garbage therefore resolve to black.
  return {false, Qt::black};
}

PaintState elementSvgFill(const QString& value, const PaintState& root,
                          const PaintState& presentation) {
  const QString text = value.trimmed();
  const QString keyword = text.toLower();
  if (keyword == QLatin1String("none")) return {true, {}};
  if (keyword == QLatin1String("currentcolor") ||
      keyword == QLatin1String("initial"))
    return {false, Qt::black};
  if (keyword == QLatin1String("inherit") ||
      keyword == QLatin1String("unset") ||
      keyword == QLatin1String("revert"))
    return root;
  if (keyword == QLatin1String("revert-layer") || text.isEmpty())
    return presentation;
  if (color::isParsableColor(text)) return {false, color::toQColor(text)};
  return presentation;
}

PaintState lineStroke(const QString& value, const QColor& presentation) {
  const QString text = value.trimmed();
  const QString keyword = text.toLower();
  if (keyword == QLatin1String("none") ||
      keyword == QLatin1String("initial") ||
      keyword == QLatin1String("inherit") ||
      keyword == QLatin1String("unset") ||
      keyword == QLatin1String("revert"))
    return {true, {}};
  if (keyword == QLatin1String("currentcolor")) return {false, Qt::black};
  if (color::isParsableColor(text)) return {false, color::toQColor(text)};
  return {false, presentation};
}

QColor cssColor(const QString& value, const QColor& fallback = Qt::black) {
  return color::isParsableColor(value) ? color::toQColor(value) : fallback;
}

QColor htmlColor(const QString& value) {
  // Journey foreignObject labels use CSS `color`, not SVG `fill`. Invalid
  // color values (including `none`) are dropped by CSS and inherit the HTML
  // root's black color; currentColor/inherit/initial do the same here.
  return color::isParsableColor(value) ? color::toQColor(value) : Qt::black;
}

QString collapsedSvgText(QString text) {
  text.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
  return text.trimmed();
}

void setBrush(QPainter& painter, const PaintState& paint) {
  painter.setBrush(paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color));
}

void drawBaseline(QPainter& painter, const editor::CssPixelFont& font,
                  const QPointF& anchor, const QString& text,
                  const QColor& color, bool bold = false,
                  bool centered = false) {
  if (!(font.scale > 0.0) || text.isEmpty()) return;
  QFont qfont = font.font;
  if (bold) qfont.setWeight(QFont::Bold);
  const QFontMetricsF metrics(qfont);
  qreal x = 0.0;
  if (centered) x = -metrics.horizontalAdvance(text) / 2.0;
  painter.save();
  painter.translate(anchor);
  painter.scale(font.scale, font.scale);
  painter.setFont(qfont);
  painter.setPen(color);
  painter.drawText(QPointF(x, 0.0), text);
  painter.restore();
}

void drawCenteredFo(QPainter& painter, const editor::CssPixelFont& font,
                    const QRectF& rect, const QString& raw,
                    const QColor& color) {
  if (raw.isEmpty() || !(font.scale > 0.0)) return;
  const QString text = collapsedSvgText(raw);
  const qreal scale = font.scale;
  const QRectF logicalRect(rect.x() / scale, rect.y() / scale,
                           rect.width() / scale, rect.height() / scale);
  QTextLayout layout(text, font.font);
  QTextOption option;
  option.setAlignment(Qt::AlignHCenter);
  option.setWrapMode(QTextOption::WordWrap);
  layout.setTextOption(option);
  layout.beginLayout();
  QVector<QTextLine> lines;
  qreal totalHeight = 0.0;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(logicalRect.width());
    lines.append(line);
    totalHeight += line.height();
  }
  layout.endLayout();
  // CSS table-cell centering only offsets content that fits. Overflow starts
  // at the foreignObject's top edge and is clipped there.
  qreal y = logicalRect.y() +
            std::max<qreal>(0.0, (logicalRect.height() - totalHeight) / 2.0);
  for (QTextLine& line : lines) {
    line.setPosition(QPointF(logicalRect.x(), y));
    y += line.height();
  }
  painter.save();
  painter.setClipRect(rect);
  painter.scale(scale, scale);
  painter.setPen(color);
  layout.draw(&painter, QPointF());
  painter.restore();
}

void drawJourneyLabel(QPainter& painter, const JourneyScene& scene,
                      const QRectF& rect, const QString& text,
                      const QPointF& oldTextAnchor,
                      const QPointF& tspanTextAnchor,
                      bool sectionLabel, const QString& sectionFill,
                      bool cssFillActive) {
  const PaintState root = rootSvgFill(scene.style.textColor);
  const QString placement = scene.config.textPlacement;
  if (placement == QStringLiteral("fo")) {
    drawCenteredFo(painter,
                   editor::makeUnhintedCssPixelFont(scene.style.fontFamily,
                                                    scene.style.fontSize),
                   rect, text,
                   htmlColor(scene.style.textColor));
    return;
  }

  if (placement == QStringLiteral("old")) {
    const PaintState paint = sectionLabel && cssFillActive
                                 ? elementSvgFill(sectionFill, root, root)
                                 : root;
    if (paint.none) return;
    drawBaseline(painter,
                 editor::makeUnhintedCssPixelFont(scene.style.fontFamily,
                                                  scene.style.fontSize),
                 oldTextAnchor,
                 collapsedSvgText(text), paint.color, false, true);
    return;
  }

  const QStringList lines = text.split(
      QRegularExpression(QStringLiteral(R"(<br\s*/?>)"),
                         QRegularExpression::CaseInsensitiveOption),
      Qt::KeepEmptyParts);
  const editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
      scene.config.taskFontFamily, scene.config.taskFontSize);
  const QFontMetricsF metrics(font.font);
  const PaintState white{false, Qt::white};
  const PaintState paint = sectionLabel && cssFillActive
                               ? elementSvgFill(sectionFill, root, white)
                               : white;
  if (paint.none) return;
  for (qsizetype i = 0; i < lines.size(); ++i) {
    const qreal dy = std::isfinite(scene.config.taskFontLineStep)
                         ? i * scene.config.taskFontLineStep -
                               scene.config.taskFontLineStep *
                                   (lines.size() - 1) / 2.0
                         : 0.0;
    const qreal centralY = tspanTextAnchor.y() + dy;
    const qreal baseline = centralY +
                           (metrics.ascent() - metrics.descent()) * font.scale / 2.0;
    drawBaseline(painter, font, QPointF(tspanTextAnchor.x(), baseline),
                 collapsedSvgText(lines.at(i)), paint.color, false, true);
  }
}

QPainterPath mouthPath(bool smile) {
  // d3-arc swaps the supplied radii because innerRadius(7.5) is greater than
  // outerRadius(6.818), so the emitted path starts at r=7.5.
  constexpr qreal outer = 15.0 / 2.0;
  constexpr qreal inner = 15.0 / 2.2;
  QPainterPath path;
  if (smile) {
    path.moveTo(outer, 0.0);
    path.arcTo(QRectF(-outer, -outer, outer * 2.0, outer * 2.0), 0.0, -180.0);
    path.lineTo(-inner, 0.0);
    path.arcTo(QRectF(-inner, -inner, inner * 2.0, inner * 2.0), 180.0, 180.0);
  } else {
    path.moveTo(-outer, 0.0);
    path.arcTo(QRectF(-outer, -outer, outer * 2.0, outer * 2.0), 180.0, -180.0);
    path.lineTo(inner, 0.0);
    path.arcTo(QRectF(-inner, -inner, inner * 2.0, inner * 2.0), 0.0, 180.0);
  }
  path.closeSubpath();
  return path;
}

const JourneyActor* actorByName(const JourneyScene& scene, const QString& name) {
  if (name == QStringLiteral("__proto__") && scene.hasPrototypeActor)
    return &scene.prototypeActor;
  for (const JourneyActor& actor : scene.actors)
    if (actor.name == name) return &actor;
  return nullptr;
}

}  // namespace

void paintJourneyScene(const JourneyScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options) {
  Q_UNUSED(options);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  // Mermaid gives the SVG root 25px more viewport height than its viewBox.
  // Geometry outside the viewBox is clipped; the trailing viewport area stays
  // transparent rather than revealing low-score faces below y=445.
  painter.setClipRect(scene.upstreamViewBox);

  const PaintState rootFill = rootSvgFill(scene.style.textColor);
  const editor::CssPixelFont rootFont =
      editor::makeUnhintedCssPixelFont(scene.style.fontFamily,
                                       scene.style.fontSize);

  // Actor legend is rendered before the chart groups.
  for (const JourneyActor& actor : scene.actors) {
    painter.setPen(QPen(Qt::black, 1.0));
    painter.setBrush(cssColor(actor.color));
    painter.drawEllipse(QPointF(20.0, actor.y), 7.0, 7.0);
    if (!rootFill.none) {
      for (qsizetype i = 0; i < actor.lines.size(); ++i)
        drawBaseline(painter, rootFont,
                     QPointF(40.0 + 2.0 * scene.config.boxTextMargin,
                             actor.y + 7.0 + i * 20.0),
                     collapsedSvgText(actor.lines.at(i)), rootFill.color);
    }
  }

  for (const JourneySectionGeometry& section : scene.sections) {
    painter.setPen(QPen(QColor(QStringLiteral("#666666")), 1.0));
    const PaintState presentation{
        false, cssColor(section.presentationFill,
                        QColor(QStringLiteral("#cccccc")))};
    setBrush(painter, section.cssFillActive
                          ? elementSvgFill(section.fill, rootFill, presentation)
                          : presentation);
    painter.drawRoundedRect(section.rect, 3.0, 3.0);
    drawJourneyLabel(painter, scene, section.rect, section.text,
                     section.oldTextAnchor, section.tspanTextAnchor, true,
                     section.fill, section.cssFillActive);
  }

  for (const JourneyTaskGeometry& task : scene.tasks) {
    const qreal centerX = task.faceCenter.x();
    const PaintState taskStroke =
        lineStroke(scene.style.textColor, QColor(QStringLiteral("#666666")));
    if (!taskStroke.none) {
      QPen linePen(taskStroke.color, 1.0);
      linePen.setCapStyle(Qt::FlatCap);
      linePen.setDashPattern({4.0, 2.0});
      painter.setPen(linePen);
    } else {
      painter.setPen(Qt::NoPen);
    }
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(centerX, task.rect.y()), QPointF(centerX, 450.0));

    if (std::isfinite(task.faceCenter.y())) {
      painter.setPen(QPen(QColor(QStringLiteral("#999999")), 2.0));
      painter.setBrush(cssColor(scene.style.faceColor,
                                QColor(QStringLiteral("#FFF8DC"))));
      painter.drawEllipse(task.faceCenter, 15.0, 15.0);

      painter.setPen(QPen(QColor(QStringLiteral("#666666")), 2.0));
      painter.setBrush(QColor(QStringLiteral("#666666")));
      painter.drawEllipse(task.faceCenter + QPointF(-5.0, -5.0), 1.5, 1.5);
      painter.drawEllipse(task.faceCenter + QPointF(5.0, -5.0), 1.5, 1.5);
      if (task.score > 3.0 || task.score < 3.0) {
        painter.save();
        painter.translate(task.faceCenter +
                          QPointF(0.0, task.score > 3.0 ? 2.0 : 7.0));
        painter.setPen(QPen(QColor(QStringLiteral("#666666")), 1.0));
        setBrush(painter, rootFill);
        painter.drawPath(mouthPath(task.score > 3.0));
        painter.restore();
      } else {
        QPen mouthPen(QColor(QStringLiteral("#666666")), 1.0);
        mouthPen.setCapStyle(Qt::FlatCap);
        painter.setPen(mouthPen);
        painter.drawLine(task.faceCenter + QPointF(-5.0, 7.0),
                         task.faceCenter + QPointF(5.0, 7.0));
      }
    }

    painter.setPen(QPen(QColor(QStringLiteral("#666666")), 1.0));
    const PaintState presentation{
        false, cssColor(task.presentationFill,
                        QColor(QStringLiteral("#cccccc")))};
    setBrush(painter, task.cssFillActive
                          ? elementSvgFill(task.fill, rootFill, presentation)
                          : presentation);
    painter.drawRoundedRect(task.rect, 3.0, 3.0);

    for (qsizetype i = 0; i < task.people.size(); ++i) {
      const QString& person = task.people.at(i);
      const JourneyActor* actor = actorByName(scene, person);
      if (!actor) continue;
      const qreal actorX = task.actorCenters.value(i, task.rect.x() + 14.0);
      painter.setPen(QPen(Qt::black, 1.0));
      painter.setBrush(cssColor(actor->color));
      painter.drawEllipse(QPointF(actorX, task.rect.y()), 7.0, 7.0);
    }
    drawJourneyLabel(painter, scene, task.rect, task.text,
                     task.oldTextAnchor, task.tspanTextAnchor, false, task.fill,
                     task.cssFillActive);
  }

  if (!scene.title.isEmpty() && scene.config.titleFontSize > 0.0) {
    const PaintState titlePaint = scene.config.titleColor.isEmpty()
                                      ? rootFill
                                      : elementSvgFill(scene.config.titleColor,
                                                       rootFill, rootFill);
    if (!titlePaint.none) drawBaseline(painter,
                 editor::makeUnhintedCssPixelFont(scene.config.titleFontFamily,
                                                  scene.config.titleFontSize),
                 QPointF(scene.leftMarginResolved, 25.0),
                 collapsedSvgText(scene.title),
                 titlePaint.color, true);
  }

  // Bottom axis and marker. SVG markerUnits defaults to strokeWidth, so the
  // 6x4 marker path is scaled by the 4px line pen.
  const qreal axisY = scene.config.height * 4.0;
  const qreal axisX2 = scene.canvasWidth - scene.leftMarginResolved - 4.0;
  const PaintState axisStroke = lineStroke(scene.style.textColor, Qt::black);
  if (!axisStroke.none) {
    QPen axisPen(axisStroke.color, 4.0);
    axisPen.setCapStyle(Qt::FlatCap);
    painter.setPen(axisPen);
  } else {
    painter.setPen(Qt::NoPen);
  }
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(QPointF(scene.leftMarginResolved, axisY),
                   QPointF(axisX2, axisY));
  QPainterPath arrow;
  arrow.moveTo(axisX2 - 20.0, axisY - 8.0);
  arrow.lineTo(axisX2 + 4.0, axisY);
  arrow.lineTo(axisX2 - 20.0, axisY + 8.0);
  arrow.closeSubpath();
  painter.setPen(Qt::NoPen);
  setBrush(painter, rootFill);
  painter.drawPath(arrow);

  painter.restore();
}

}  // namespace muffin::mermaid::journey
