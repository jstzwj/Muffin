#include "mermaid/math/MathMlCssPainter.h"

#include "mermaid/math/MathMlCssLayout.h"
#include "math/OpenTypeMathFont.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <functional>

namespace muffin::math {

void paintMathMlOperation(QPainter& painter,
                          const MathCssPaintOperation& operation,
                          const QColor& color,
                          MathMlPaintLayer layer) {
  const auto paintVerticalGlyph = [&](const MathCssVerticalGlyphOperation& glyph) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    if (glyph.kind == MathCssVerticalGlyphKind::FixedVariant &&
        glyph.fixedGlyphIndex != 0 && !glyph.inkBounds.isEmpty() &&
        glyph.scalePolicy == MathCssVerticalScalePolicy::FitTargetExtent) {
      const QPainterPath path = font.glyphPath(glyph.fixedGlyphIndex);
      const QRectF bounds = path.boundingRect();
      if (!bounds.isEmpty()) {
        QTransform placement;
        placement.translate(glyph.target.center().x(),
                            glyph.target.center().y());
        placement.scale(glyph.inkBounds.width() / bounds.width(),
                        glyph.target.height() / bounds.height());
        placement.translate(-bounds.center().x(), -bounds.center().y());
        painter.save();
        painter.setClipRect(glyph.target, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(placement.map(path));
        painter.restore();
        return;
      }
    }
    QVector<quint32> indexes;
    QVector<QPointF> positions;
    if (glyph.kind == MathCssVerticalGlyphKind::FixedVariant) {
      if (glyph.fixedGlyphIndex == 0) return;
      indexes = {glyph.fixedGlyphIndex};
      positions = {QPointF()};
    } else {
      indexes.reserve(glyph.parts.size());
      positions.reserve(glyph.parts.size());
      for (const MathCssVerticalGlyphPart& part : glyph.parts) {
        indexes.push_back(part.glyphIndex);
        positions.push_back(part.position);
      }
    }
    if (indexes.isEmpty()) return;
    QGlyphRun run;
    run.setRawFont(font.rasterFont());
    run.setGlyphIndexes(indexes);
    run.setPositions(positions);
    painter.save();
    if (glyph.scalePolicy == MathCssVerticalScalePolicy::FitTargetExtent)
      painter.setClipRect(glyph.target, Qt::IntersectClip);
    painter.setPen(color);
    painter.drawGlyphRun(glyph.baselineOrigin, run);
    painter.restore();
  };

  const auto paintSolidRect = [&](QRectF rule) {
    if (rule.isEmpty()) return;
    const QTransform transform = painter.transform();
    QRectF deviceRule = transform.mapRect(rule);
    deviceRule.setLeft(std::ceil(deviceRule.left()));
    deviceRule.setWidth(std::max<qreal>(1.0, std::floor(deviceRule.width())));
    deviceRule.setTop(std::ceil(deviceRule.top()));
    deviceRule.setHeight(std::max<qreal>(
        1.0, std::floor(deviceRule.height())));
    painter.save();
    painter.resetTransform();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRect(deviceRule);
    painter.restore();
  };
  const auto paintRule = [&](const MathCssFractionBox& fraction) {
    if (fraction.hasRule) paintSolidRect(fraction.rule);
  };

  const auto paintRadical = [&](const MathCssRadicalOperation& radical) {
    if (radical.glyphRun.glyphIndexes.isEmpty() ||
        radical.glyphRun.clip.isEmpty())
      return;
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const QPainterPath path = font.glyphPath(
        radical.glyphRun.glyphIndexes.front());
    const QRectF bounds = path.boundingRect();
    if (!bounds.isEmpty()) {
      const qreal scale = radical.glyphRun.fontScale;
      QTransform placement;
      placement.translate(radical.glyph.left() - bounds.left() * scale,
                          radical.glyph.top() - bounds.top() * scale);
      placement.scale(scale, scale);
      painter.save();
      painter.setClipRect(radical.glyphRun.clip, Qt::IntersectClip);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawPath(placement.map(path));
      painter.restore();
    }
    paintSolidRect(radical.rule);
  };

  const auto paintGlyphRuns = [&](const QVector<MathCssGlyphRunOperation>& runs) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    for (const MathCssGlyphRunOperation& glyph : runs) {
      if (glyph.glyphIndexes.isEmpty() || glyph.clip.isEmpty()) continue;
      QGlyphRun run;
      run.setRawFont(glyph.rawFont.isValid()
                         ? glyph.rawFont
                         : font.rasterFont(glyph.fontScale));
      run.setGlyphIndexes(glyph.glyphIndexes);
      run.setPositions(glyph.positions);
      painter.save();
      painter.setClipRect(glyph.clip, Qt::IntersectClip);
      painter.setPen(color);
      painter.drawGlyphRun(glyph.baselineOrigin, run);
      painter.restore();
    }
  };

  const auto paintFences = [&](const std::optional<MathCssFencePair>& fences) {
    if (!fences) return;
    if (fences->leftGlyph) paintVerticalGlyph(*fences->leftGlyph);
    if (fences->rightGlyph) paintVerticalGlyph(*fences->rightGlyph);
  };

  const auto paintAccent = [&](const MathCssAccentOperation& accent) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const MathCssHorizontalGlyphOperation& glyph = accent.glyph;

    painter.save();
    painter.setClipRect(accent.container, Qt::IntersectClip);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (glyph.kind == MathCssHorizontalGlyphKind::FixedVariant) {
      const QPainterPath path = font.glyphPath(glyph.fixedGlyphIndex);
      const QRectF bounds = path.boundingRect();
      if (!bounds.isEmpty()) {
        const qreal scale =
            glyph.scalePolicy ==
                    MathCssHorizontalScalePolicy::PreserveVariantScale
                ? glyph.fontScale
                : glyph.target.width() / glyph.realizedExtent;
        QTransform placement;
        placement.translate(glyph.target.center().x() + glyph.paintOffset.x(),
                            glyph.target.center().y() + glyph.paintOffset.y());
        placement.scale(scale, glyph.fontScale);
        placement.translate(-bounds.center().x(), -bounds.center().y());
        painter.drawPath(placement.map(path));
      }
    } else if (glyph.kind == MathCssHorizontalGlyphKind::ShapedText) {
      QGlyphRun run;
      run.setRawFont(font.rasterFont(glyph.fontScale));
      run.setGlyphIndexes(glyph.textGlyphIndexes);
      run.setPositions(glyph.textGlyphPositions);
      painter.setPen(color);
      painter.drawGlyphRun(glyph.target.center() - glyph.inkBounds.center() +
                               glyph.paintOffset,
                           run);
    } else {
      const qreal assemblyLeft = glyph.target.center().x() -
                                 glyph.selectionTarget / 2.0;
      const qreal assemblyBaseline = glyph.target.center().y() -
                                     glyph.inkBounds.center().y() +
                                     glyph.paintOffset.y();
      QVector<quint32> glyphIndexes;
      QVector<QPointF> glyphPositions;
      glyphIndexes.reserve(glyph.parts.size());
      glyphPositions.reserve(glyph.parts.size());
      for (const MathCssHorizontalGlyphPart& part : glyph.parts) {
        glyphIndexes.push_back(part.glyphIndex);
        glyphPositions.push_back(QPointF(part.offset, 0.0));
      }
      QGlyphRun run;
      run.setRawFont(font.rasterFont(glyph.fontScale));
      run.setGlyphIndexes(glyphIndexes);
      run.setPositions(glyphPositions);
      painter.setPen(color);
      painter.drawGlyphRun(
          QPointF(assemblyLeft + glyph.paintOffset.x(), assemblyBaseline), run);
    }
    painter.restore();
  };

  std::function<void(const MathCssPaintOperation&)> paintOperation;
  paintOperation = [&](const MathCssPaintOperation& current) {
    const bool paintBody = layer != MathMlPaintLayer::Accent;
    if (const auto* glyphRun =
            std::get_if<MathCssGlyphRunGroupOperation>(&current.payload)) {
      if (paintBody) paintGlyphRuns(glyphRun->runs);
      return;
    }
    if (const auto* row =
            std::get_if<MathCssRowOperation>(&current.payload)) {
      if (paintBody) paintGlyphRuns(row->glyphRuns);
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      return;
    }
    if (const auto* middle =
            std::get_if<MathCssMiddlePaintOperation>(&current.payload)) {
      if (paintBody) paintGlyphRuns({middle->glyphRun});
      return;
    }
    if (const auto* fraction =
            std::get_if<MathCssFractionPaint>(&current.payload)) {
      const auto paintRow = [&] (
          QRectF row,
          const QVector<MathCssGlyphRunOperation>& glyphRuns) {
      if (!row.isEmpty() && !glyphRuns.isEmpty())
        paintGlyphRuns(glyphRuns);
      };
      if (paintBody) {
        paintRow(fraction->box.numerator,
                 fraction->numeratorGlyphRuns);
        paintRow(fraction->box.denominator,
                 fraction->denominatorGlyphRuns);
      }
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      if (paintBody) {
        if (fraction->leftDelimiterGlyph)
          paintVerticalGlyph(*fraction->leftDelimiterGlyph);
        if (fraction->rightDelimiterGlyph)
          paintVerticalGlyph(*fraction->rightDelimiterGlyph);
        paintRule(fraction->box);
      }
      return;
    }

    if (const auto* leftRight =
            std::get_if<MathCssLeftRightOperation>(&current.payload)) {
      if (paintBody)
        for (const MathCssLeftRightBodyRegion& region :
             leftRight->bodyRegions) {
          if (!region.glyphRuns.isEmpty())
            paintGlyphRuns(region.glyphRuns);
        }
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      if (paintBody) {
        for (const MathCssMiddleDelimiterOperation& middle :
             leftRight->middleDelimiters)
          if (middle.glyph) paintVerticalGlyph(*middle.glyph);
        if (leftRight->leftDelimiterGlyph)
          paintVerticalGlyph(*leftRight->leftDelimiterGlyph);
        if (leftRight->rightDelimiterGlyph)
          paintVerticalGlyph(*leftRight->rightDelimiterGlyph);
      }
      return;
    }

    if (const auto* array =
            std::get_if<MathCssArrayOperation>(&current.payload)) {
      if (paintBody)
        for (const MathCssArrayCell& cell : array->cells) {
          if (!cell.glyphRuns.isEmpty())
            paintGlyphRuns(cell.glyphRuns);
        }
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      if (paintBody) {
        if (array->leftDelimiterGlyph)
          paintVerticalGlyph(*array->leftDelimiterGlyph);
        if (array->rightDelimiterGlyph)
          paintVerticalGlyph(*array->rightDelimiterGlyph);
      }
      return;
    }

    if (const auto* accent =
            std::get_if<MathCssAccentOperation>(&current.payload)) {
      if (paintBody) {
        if (!accent->bodyGlyphRuns.isEmpty())
          paintGlyphRuns(accent->bodyGlyphRuns);
        if (!accent->annotationGlyphRuns.isEmpty())
          paintGlyphRuns(accent->annotationGlyphRuns);
      }
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      if (layer != MathMlPaintLayer::Body) paintAccent(*accent);
      return;
    }

    if (const auto* script =
            std::get_if<MathCssScriptOperation>(&current.payload)) {
      if (paintBody) {
        if (script->largeOperatorGlyph)
          paintVerticalGlyph(*script->largeOperatorGlyph);
        if (!script->baseGlyphRuns.isEmpty())
          paintGlyphRuns(script->baseGlyphRuns);
        if (!script->superscriptGlyphRuns.isEmpty())
          paintGlyphRuns(script->superscriptGlyphRuns);
        if (!script->subscriptGlyphRuns.isEmpty())
          paintGlyphRuns(script->subscriptGlyphRuns);
      }
    } else if (const auto* radical =
                   std::get_if<MathCssRadicalOperation>(&current.payload)) {
      if (paintBody && !radical->bodyGlyphRuns.isEmpty())
        paintGlyphRuns(radical->bodyGlyphRuns);
    }
    for (const MathCssPaintOperation& child : current.children)
      paintOperation(child);
    if (const auto* radical =
            std::get_if<MathCssRadicalOperation>(&current.payload)) {
      if (paintBody) {
        paintRadical(*radical);
        paintFences(radical->fences);
      }
    } else if (const auto* script =
                   std::get_if<MathCssScriptOperation>(&current.payload)) {
      if (paintBody) paintFences(script->fences);
    }
  };
  paintOperation(operation);
}

MathMlPaintOperationBuildResult paintMathMlOperations(
    QPainter& painter, const MathLayoutResult& layout, const QColor& color,
    qreal renderFontPixelSize, MathMlPaintLayer layer) {
  auto build = buildMathMlPaintOperations(
      layout, renderFontPixelSize, 16.0);
  if (!build.operation || build.operation->container().isEmpty()) return build;
  paintMathMlOperation(painter, *build.operation, color, layer);
  return build;
}

}  // namespace muffin::math
