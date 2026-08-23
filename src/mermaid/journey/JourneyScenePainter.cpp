#include "mermaid/journey/JourneyScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/scene/SvgStroke.h"
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

using PaintState = JourneyPaintState;

PaintState rootSvgFill(const QString& value) {
  return journeyRootSvgFill(value);
}

PaintState elementSvgFill(const QString& value, const PaintState& root,
                          const PaintState& presentation) {
  return journeyElementSvgFill(value, root, presentation);
}

PaintState lineStroke(const QString& value, const QColor& presentation) {
  return journeyLineStroke(value, presentation);
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

// themeCSS keeps CSS spellings; an empty value means the pre-themeCSS base.
QString effectiveCss(const QString& value, const QString& base) {
  return value.isEmpty() ? base : value;
}

qreal effectiveFontSize(const JourneyElementCss& css, qreal base) {
  return css.fontSize >= 0.0 ? css.fontSize : base;
}

qreal effectiveOpacity(const JourneyElementCss& css) {
  return css.opacity >= 0.0 ? css.opacity : 1.0;
}

bool effectiveBold(const JourneyElementCss& css, bool base) {
  if (css.fontWeight.isEmpty()) return base;
  const QString weight = css.fontWeight.trimmed().toLower();
  if (weight == QLatin1String("bold") || weight == QLatin1String("bolder"))
    return true;
  if (weight == QLatin1String("normal") || weight == QLatin1String("lighter"))
    return false;
  bool ok = false;
  const int numeric = weight.toInt(&ok);
  return ok && numeric >= 600;
}

// stroke-width resolves like every other paint value: the base string when
// themeCSS has no opinion, the cascade outcome otherwise. % is relative to
// the SVG viewport's normalized diagonal.
qreal strokeWidthFor(const QString& value, const QString& base,
                     const CssLengthContext& ctx, qreal diagonal) {
  return editor::cssStrokeWidthPx(effectiveCss(value, base), ctx, diagonal);
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
                      bool cssFillActive, const JourneyElementCss& label,
                      const JourneyElementCss& svgText) {
  const PaintState root = rootSvgFill(effectiveCss(
      scene.rootCss.fill, scene.style.textColor));
  const QString placement = scene.config.textPlacement;
  if (placement == QStringLiteral("fo")) {
    if (!label.visible) return;
    const editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
        effectiveCss(label.fontFamily, scene.style.fontFamily),
        effectiveFontSize(label, scene.style.fontSize));
    painter.save();
    painter.setOpacity(effectiveOpacity(label));
    drawCenteredFo(painter, font, rect, text,
                   htmlColor(effectiveCss(label.color, scene.style.textColor)));
    painter.restore();
    return;
  }

  if (placement == QStringLiteral("old")) {
    if (!svgText.visible) return;
    const PaintState paint = elementSvgFill(
        effectiveCss(svgText.fill, scene.style.textColor), root, root);
    if (paint.none) return;
    painter.save();
    painter.setOpacity(effectiveOpacity(svgText));
    drawBaseline(painter,
                 editor::makeUnhintedCssPixelFont(scene.style.fontFamily,
                                                  scene.style.fontSize),
                 oldTextAnchor,
                 collapsedSvgText(text), paint.color, false, true);
    painter.restore();
    return;
  }

  if (!svgText.visible) return;
  const QStringList lines = text.split(
      QRegularExpression(QStringLiteral(R"(<br\s*/?>)"),
                         QRegularExpression::CaseInsensitiveOption),
      Qt::KeepEmptyParts);
  const editor::CssPixelFont font = editor::makeUnhintedCssPixelFont(
      scene.config.taskFontFamily, scene.config.taskFontSize);
  const QFontMetricsF metrics(font.font);
  const PaintState white{false, Qt::white};
  const PaintState paint = elementSvgFill(
      effectiveCss(svgText.fill,
                   sectionLabel && cssFillActive ? sectionFill : QString()),
      root, sectionLabel && cssFillActive ? elementSvgFill(sectionFill, root, white) : white);
  if (paint.none) return;
  painter.save();
  painter.setOpacity(effectiveOpacity(svgText));
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
  painter.restore();
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

JourneyPaintState journeyRootSvgFill(const QString& value) {
  const QString text = value.trimmed();
  const QString keyword = text.toLower();
  if (keyword == QLatin1String("none")) return {true, {}};
  if (color::isParsableColor(text)) return {false, color::toQColor(text)};
  // The SVG root inherits the CSS initial color/fill from its host. All CSS
  // globals, an empty declaration, and garbage therefore resolve to black.
  return {false, Qt::black};
}

JourneyPaintState journeyElementSvgFill(
    const QString& value, const JourneyPaintState& root,
    const JourneyPaintState& presentation) {
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

JourneyPaintState journeyLineStroke(const QString& value,
                                    const QColor& presentation) {
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

  // The #id root rule paints the svg element itself: fill becomes the
  // inherited fill for mouth/arrowhead/un-attributed text, font-size drives
  // every text that does not declare its own.
  const PaintState rootFill = rootSvgFill(effectiveCss(
      scene.rootCss.fill, scene.style.textColor));
  const QString rootFamily = effectiveCss(scene.rootCss.fontFamily,
                                          scene.style.fontFamily);
  const qreal rootSize = scene.rootCss.fontSize >= 0.0
                             ? scene.rootCss.fontSize
                             : scene.style.fontSize;
  const editor::CssPixelFont rootFont =
      editor::makeUnhintedCssPixelFont(rootFamily, rootSize);
  const CssLengthContext lengthCtx =
      editor::pieCssLengthContext(rootFamily, rootSize);
  const qreal diagonal =
      std::sqrt(scene.bounds.width() * scene.bounds.width() +
                scene.bounds.height() * scene.bounds.height()) /
      std::sqrt(2.0);

  // Actor legend is rendered before the chart groups.
  for (const JourneyActor& actor : scene.actors) {
    if (actor.circle.visible) {
      painter.save();
      painter.setOpacity(effectiveOpacity(actor.circle));
      const PaintState presentation{false, color::cssColor(actor.color)};
      setBrush(painter, elementSvgFill(
                            effectiveCss(actor.circle.fill, actor.color),
                            rootFill, presentation));
      const PaintState stroke = lineStroke(
          effectiveCss(actor.circle.stroke, QStringLiteral("#000000")),
          Qt::black);
      if (!stroke.none) {
        painter.setPen(QPen(stroke.color, strokeWidthFor(
            actor.circle.strokeWidth, QStringLiteral("1px"), lengthCtx,
            diagonal)));
      } else {
        painter.setPen(Qt::NoPen);
      }
      painter.drawEllipse(QPointF(20.0, actor.y), 7.0, 7.0);
      painter.restore();
    }
    if (!actor.text.visible || rootFill.none) continue;
    painter.save();
    painter.setOpacity(effectiveOpacity(actor.text));
    const editor::CssPixelFont legendFont = editor::makeUnhintedCssPixelFont(
        effectiveCss(actor.text.fontFamily, rootFamily),
        actor.text.fontSize >= 0.0 ? actor.text.fontSize : rootSize);
    const PaintState textPaint = elementSvgFill(
        effectiveCss(actor.text.fill, scene.style.textColor), rootFill,
        rootFill);
    if (textPaint.none) {
      painter.restore();
      continue;
    }
    for (qsizetype i = 0; i < actor.lines.size(); ++i)
      drawBaseline(painter, legendFont,
                   QPointF(40.0 + 2.0 * scene.config.boxTextMargin,
                           actor.y + 7.0 + i * 20.0),
                   collapsedSvgText(actor.lines.at(i)), textPaint.color,
                   effectiveBold(actor.text, false));
    painter.restore();
  }

  for (const JourneySectionGeometry& section : scene.sections) {
    if (!section.box.visible) continue;
    painter.save();
    painter.setOpacity(effectiveOpacity(section.box));
    const PaintState presentation{
        false, color::cssColor(section.presentationFill,
                        QColor(QStringLiteral("#cccccc")))};
    const PaintState stroke = lineStroke(
        effectiveCss(section.box.stroke, QStringLiteral("#666666")),
        QColor(QStringLiteral("#666666")));
    if (!stroke.none) {
      painter.setPen(QPen(stroke.color, strokeWidthFor(
          section.box.strokeWidth, QStringLiteral("1px"), lengthCtx,
          diagonal)));
    } else {
      painter.setPen(Qt::NoPen);
    }
    setBrush(painter, section.cssFillActive
                          ? elementSvgFill(section.fill, rootFill, presentation)
                          : presentation);
    painter.drawRoundedRect(section.rect, 3.0, 3.0);
    painter.restore();
    drawJourneyLabel(painter, scene, section.rect, section.text,
                     section.oldTextAnchor, section.tspanTextAnchor, true,
                     section.fill, section.cssFillActive, section.label,
                     section.svgText);
  }

  for (const JourneyTaskGeometry& task : scene.tasks) {
    const qreal centerX = task.faceCenter.x();
    if (task.line.visible) {
      painter.save();
      painter.setOpacity(effectiveOpacity(task.line));
      const PaintState taskStroke = lineStroke(
          effectiveCss(task.line.stroke, scene.style.textColor),
          QColor(QStringLiteral("#666666")));
      if (!taskStroke.none) {
        const qreal lineWidth = strokeWidthFor(
            task.line.strokeWidth, QStringLiteral("1px"), lengthCtx,
            diagonal);
        QPen linePen(taskStroke.color, lineWidth);
        linePen.setCapStyle(Qt::FlatCap);
        linePen.setJoinStyle(Qt::MiterJoin);
        linePen.setDashPattern(scene::normalizedSvgDashPattern(
            {4.0, 2.0}, lineWidth));
        painter.setPen(linePen);
      } else {
        painter.setPen(Qt::NoPen);
      }
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(QPointF(centerX, task.rect.y()), QPointF(centerX, 450.0));
      painter.restore();
    }

    if (std::isfinite(task.faceCenter.y()) && task.face.visible) {
      painter.save();
      painter.setOpacity(effectiveOpacity(task.face));
      const PaintState facePresentation{
          false, color::cssColor(scene.style.faceColor,
                          QColor(QStringLiteral("#FFF8DC")))};
      setBrush(painter, elementSvgFill(
                            effectiveCss(task.face.fill, scene.style.faceColor),
                            rootFill, facePresentation));
      const PaintState faceStroke = lineStroke(
          effectiveCss(task.face.stroke, QStringLiteral("#999999")),
          QColor(QStringLiteral("#999999")));
      if (!faceStroke.none) {
        painter.setPen(QPen(faceStroke.color, strokeWidthFor(
            task.face.strokeWidth, QStringLiteral("2px"), lengthCtx,
            diagonal)));
      } else {
        painter.setPen(Qt::NoPen);
      }
      painter.drawEllipse(task.faceCenter, 15.0, 15.0);
      painter.restore();

      painter.save();
      painter.setOpacity(effectiveOpacity(task.face));
      painter.setPen(QPen(QColor(QStringLiteral("#666666")), 2.0));
      painter.setBrush(QColor(QStringLiteral("#666666")));
      painter.drawEllipse(task.faceCenter + QPointF(-5.0, -5.0), 1.5, 1.5);
      painter.drawEllipse(task.faceCenter + QPointF(5.0, -5.0), 1.5, 1.5);
      painter.restore();
      if (task.mouth.visible) {
        painter.save();
        painter.setOpacity(effectiveOpacity(task.mouth));
        if (task.score > 3.0 || task.score < 3.0) {
          painter.translate(task.faceCenter +
                            QPointF(0.0, task.score > 3.0 ? 2.0 : 7.0));
          const PaintState mouthStroke = lineStroke(
              effectiveCss(task.mouth.stroke, QStringLiteral("#666666")),
              QColor(QStringLiteral("#666666")));
          if (!mouthStroke.none) {
            painter.setPen(QPen(mouthStroke.color, strokeWidthFor(
                task.mouth.strokeWidth, QStringLiteral("1px"), lengthCtx,
                diagonal)));
          } else {
            painter.setPen(Qt::NoPen);
          }
          setBrush(painter, rootFill);
          painter.drawPath(mouthPath(task.score > 3.0));
        } else {
          QPen mouthPen(color::cssColor(
                            effectiveCss(task.mouth.stroke,
                                         QStringLiteral("#666666")),
                            QColor(QStringLiteral("#666666"))),
                        strokeWidthFor(task.mouth.strokeWidth,
                                       QStringLiteral("1px"), lengthCtx,
                                       diagonal));
          mouthPen.setCapStyle(Qt::FlatCap);
          painter.setPen(mouthPen);
          painter.drawLine(task.faceCenter + QPointF(-5.0, 7.0),
                           task.faceCenter + QPointF(5.0, 7.0));
        }
        painter.restore();
      }
    }

    if (task.box.visible) {
      painter.save();
      painter.setOpacity(effectiveOpacity(task.box));
      const PaintState stroke = lineStroke(
          effectiveCss(task.box.stroke, QStringLiteral("#666666")),
          QColor(QStringLiteral("#666666")));
      if (!stroke.none) {
        painter.setPen(QPen(stroke.color, strokeWidthFor(
            task.box.strokeWidth, QStringLiteral("1px"), lengthCtx,
            diagonal)));
      } else {
        painter.setPen(Qt::NoPen);
      }
      const PaintState presentation{
          false, color::cssColor(task.presentationFill,
                          QColor(QStringLiteral("#cccccc")))};
      setBrush(painter, task.cssFillActive
                            ? elementSvgFill(task.fill, rootFill, presentation)
                            : presentation);
      painter.drawRoundedRect(task.rect, 3.0, 3.0);
      painter.restore();
    }

    for (qsizetype i = 0; i < task.people.size(); ++i) {
      const QString& person = task.people.at(i);
      const JourneyActor* actor = actorByName(scene, person);
      if (!actor) continue;
      const JourneyElementCss& circleCss =
          task.peopleCircles.value(i, JourneyElementCss{});
      if (!circleCss.visible) continue;
      painter.save();
      painter.setOpacity(effectiveOpacity(circleCss));
      const qreal actorX = task.actorCenters.value(i, task.rect.x() + 14.0);
      const PaintState presentation{false, color::cssColor(actor->color)};
      setBrush(painter, elementSvgFill(
                            effectiveCss(circleCss.fill, actor->color),
                            rootFill, presentation));
      const PaintState stroke = lineStroke(
          effectiveCss(circleCss.stroke, QStringLiteral("#000000")),
          Qt::black);
      if (!stroke.none) {
        painter.setPen(QPen(stroke.color, strokeWidthFor(
            circleCss.strokeWidth, QStringLiteral("1px"), lengthCtx,
            diagonal)));
      } else {
        painter.setPen(Qt::NoPen);
      }
      painter.drawEllipse(QPointF(actorX, task.rect.y()), 7.0, 7.0);
      painter.restore();
    }
    drawJourneyLabel(painter, scene, task.rect, task.text,
                     task.oldTextAnchor, task.tspanTextAnchor, false, task.fill,
                     task.cssFillActive, task.label, task.svgText);
  }

  if (!scene.title.isEmpty() && scene.config.titleFontSize > 0.0 &&
      scene.titleCss.visible) {
    const QString base = scene.config.titleColor.isEmpty()
                             ? scene.style.textColor
                             : scene.config.titleColor;
    const PaintState titlePaint = elementSvgFill(
        effectiveCss(scene.titleCss.fill, base), rootFill, rootFill);
    if (!titlePaint.none) {
      painter.save();
      painter.setOpacity(effectiveOpacity(scene.titleCss));
      const QString family = effectiveCss(scene.titleCss.fontFamily,
                                          scene.config.titleFontFamily);
      const qreal size = scene.titleCss.fontSize >= 0.0
                             ? scene.titleCss.fontSize
                             : scene.config.titleFontSize;
      drawBaseline(painter,
                   editor::makeUnhintedCssPixelFont(family, size),
                   QPointF(scene.leftMarginResolved, 25.0),
                   collapsedSvgText(scene.title),
                   titlePaint.color,
                   effectiveBold(scene.titleCss, true));
      painter.restore();
    }
  }

  // Bottom axis and marker. SVG markerUnits defaults to strokeWidth, so the
  // 6x4 marker path is scaled by the 4px line pen.
  const qreal axisY = scene.config.height * 4.0;
  const qreal axisX2 = scene.canvasWidth - scene.leftMarginResolved - 4.0;
  if (scene.axisCss.visible) {
    painter.save();
    painter.setOpacity(effectiveOpacity(scene.axisCss));
    const PaintState axisStroke = lineStroke(
        effectiveCss(scene.axisCss.stroke, scene.style.textColor), Qt::black);
    if (!axisStroke.none) {
      QPen axisPen(axisStroke.color, strokeWidthFor(
          scene.axisCss.strokeWidth, QStringLiteral("4px"), lengthCtx,
          diagonal));
      axisPen.setCapStyle(Qt::FlatCap);
      painter.setPen(axisPen);
    } else {
      painter.setPen(Qt::NoPen);
    }
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(scene.leftMarginResolved, axisY),
                     QPointF(axisX2, axisY));
    painter.restore();
  }
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
