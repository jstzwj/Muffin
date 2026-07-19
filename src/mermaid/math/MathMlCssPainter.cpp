#include "mermaid/math/MathMlCssPainter.h"

#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/math/MathGlyphRasterizer.h"
#include "math/OpenTypeMathFont.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <functional>

namespace muffin::math {

namespace {

constexpr qreal kChromiumLargeOperatorRowBaselinePhase = -0.5;

}  // namespace

QVector<MathMlPaintPrimitive> collectMathMlPaintPrimitives(
    const MathCssPaintOperation& operation, MathMlPaintLayer layer) {
  QVector<MathMlPaintPrimitive> result;
  const bool body = layer != MathMlPaintLayer::Accent;
  const bool accentLayer = layer != MathMlPaintLayer::Body;
  const auto appendRuns = [&](const QVector<MathCssGlyphRunOperation>& runs,
                              MathMlPaintPrimitiveRole role,
                              const QString& path) {
    if (!body) return;
    for (qsizetype index = 0; index < runs.size(); ++index)
      result.push_back({MathMlPaintPrimitiveKind::GlyphRun, role,
                        path + QStringLiteral("/g%1").arg(index),
                        &runs.at(index)});
  };
  const auto appendVertical = [&](
      const std::optional<MathCssVerticalGlyphOperation>& glyph,
      MathMlPaintPrimitiveRole role, const QString& path) {
    if (body && glyph)
      result.push_back({MathMlPaintPrimitiveKind::VerticalGlyph, role, path,
                        &*glyph});
  };
  const auto appendFences = [&](const std::optional<MathCssFencePair>& fences,
                                const QString& path) {
    if (!fences) return;
    appendVertical(fences->leftGlyph, MathMlPaintPrimitiveRole::Fence,
                   path + QStringLiteral("/left"));
    appendVertical(fences->rightGlyph, MathMlPaintPrimitiveRole::Fence,
                   path + QStringLiteral("/right"));
  };

  std::function<void(const MathCssPaintOperation&, const QString&)> visit;
  visit = [&](const MathCssPaintOperation& current, const QString& path) {
    const auto visitChildren = [&] {
      for (qsizetype index = 0; index < current.children.size(); ++index)
        visit(current.children.at(index),
              path + QStringLiteral("/c%1").arg(index));
    };
    if (const auto* group =
            std::get_if<MathCssGlyphRunGroupOperation>(&current.payload)) {
      appendRuns(group->runs, MathMlPaintPrimitiveRole::Row, path);
      return;
    }
    if (const auto* row = std::get_if<MathCssRowOperation>(&current.payload)) {
      appendRuns(row->glyphRuns, MathMlPaintPrimitiveRole::Row, path);
      visitChildren();
      return;
    }
    if (const auto* middle =
            std::get_if<MathCssMiddlePaintOperation>(&current.payload)) {
      if (body)
        result.push_back({MathMlPaintPrimitiveKind::GlyphRun,
                          MathMlPaintPrimitiveRole::Middle,
                          path + QStringLiteral("/middle"),
                          &middle->glyphRun});
      return;
    }
    if (const auto* fraction =
            std::get_if<MathCssFractionPaint>(&current.payload)) {
      appendRuns(fraction->numeratorGlyphRuns,
                 MathMlPaintPrimitiveRole::FractionNumerator,
                 path + QStringLiteral("/numerator"));
      appendRuns(fraction->denominatorGlyphRuns,
                 MathMlPaintPrimitiveRole::FractionDenominator,
                 path + QStringLiteral("/denominator"));
      visitChildren();
      appendVertical(fraction->leftDelimiterGlyph,
                     MathMlPaintPrimitiveRole::FractionDelimiter,
                     path + QStringLiteral("/left"));
      appendVertical(fraction->rightDelimiterGlyph,
                     MathMlPaintPrimitiveRole::FractionDelimiter,
                     path + QStringLiteral("/right"));
      if (body && fraction->box.hasRule)
        result.push_back({MathMlPaintPrimitiveKind::FractionRule,
                          MathMlPaintPrimitiveRole::Row,
                          path + QStringLiteral("/rule"), &fraction->box});
      return;
    }
    if (const auto* leftRight =
            std::get_if<MathCssLeftRightOperation>(&current.payload)) {
      for (qsizetype index = 0; index < leftRight->bodyRegions.size(); ++index)
        appendRuns(leftRight->bodyRegions.at(index).glyphRuns,
                   MathMlPaintPrimitiveRole::LeftRightBody,
                   path + QStringLiteral("/body%1").arg(index));
      visitChildren();
      for (qsizetype index = 0;
           index < leftRight->middleDelimiters.size(); ++index)
        appendVertical(leftRight->middleDelimiters.at(index).glyph,
                       MathMlPaintPrimitiveRole::LeftRightDelimiter,
                       path + QStringLiteral("/middle%1").arg(index));
      appendVertical(leftRight->leftDelimiterGlyph,
                     MathMlPaintPrimitiveRole::LeftRightDelimiter,
                     path + QStringLiteral("/left"));
      appendVertical(leftRight->rightDelimiterGlyph,
                     MathMlPaintPrimitiveRole::LeftRightDelimiter,
                     path + QStringLiteral("/right"));
      return;
    }
    if (const auto* array =
            std::get_if<MathCssArrayOperation>(&current.payload)) {
      for (qsizetype index = 0; index < array->cells.size(); ++index)
        appendRuns(array->cells.at(index).glyphRuns,
                   MathMlPaintPrimitiveRole::ArrayCell,
                   path + QStringLiteral("/cell%1").arg(index));
      visitChildren();
      appendVertical(array->leftDelimiterGlyph,
                     MathMlPaintPrimitiveRole::ArrayDelimiter,
                     path + QStringLiteral("/left"));
      appendVertical(array->rightDelimiterGlyph,
                     MathMlPaintPrimitiveRole::ArrayDelimiter,
                     path + QStringLiteral("/right"));
      return;
    }
    if (const auto* accent =
            std::get_if<MathCssAccentOperation>(&current.payload)) {
      appendRuns(accent->bodyGlyphRuns, MathMlPaintPrimitiveRole::AccentBody,
                 path + QStringLiteral("/body"));
      appendRuns(accent->annotationGlyphRuns,
                 MathMlPaintPrimitiveRole::AccentAnnotation,
                 path + QStringLiteral("/annotation"));
      visitChildren();
      if (accentLayer)
        result.push_back({MathMlPaintPrimitiveKind::Accent,
                          MathMlPaintPrimitiveRole::Row,
                          path + QStringLiteral("/accent"), accent});
      return;
    }
    if (const auto* script =
            std::get_if<MathCssScriptOperation>(&current.payload)) {
      appendVertical(script->largeOperatorGlyph,
                     MathMlPaintPrimitiveRole::LargeOperator,
                     path + QStringLiteral("/operator"));
      appendRuns(script->baseGlyphRuns,
                 MathMlPaintPrimitiveRole::ScriptBase,
                 path + QStringLiteral("/base"));
      appendRuns(script->superscriptGlyphRuns,
                 MathMlPaintPrimitiveRole::ScriptSuperscript,
                 path + QStringLiteral("/sup"));
      appendRuns(script->subscriptGlyphRuns,
                 MathMlPaintPrimitiveRole::ScriptSubscript,
                 path + QStringLiteral("/sub"));
      visitChildren();
      appendFences(script->fences, path + QStringLiteral("/fence"));
      return;
    }
    if (const auto* radical =
            std::get_if<MathCssRadicalOperation>(&current.payload)) {
      appendRuns(radical->bodyGlyphRuns,
                 MathMlPaintPrimitiveRole::RadicalBody,
                 path + QStringLiteral("/body"));
      visitChildren();
      if (body)
        result.push_back({MathMlPaintPrimitiveKind::Radical,
                          MathMlPaintPrimitiveRole::Row,
                          path + QStringLiteral("/radical"), radical});
      appendFences(radical->fences, path + QStringLiteral("/fence"));
      return;
    }
    visitChildren();
  };
  visit(operation, QStringLiteral("root"));
  const bool containsLargeOperator = std::any_of(
      result.cbegin(), result.cend(), [](const MathMlPaintPrimitive& primitive) {
        return primitive.role == MathMlPaintPrimitiveRole::LargeOperator;
      });
  for (MathMlPaintPrimitive& primitive : result) {
    if (primitive.kind != MathMlPaintPrimitiveKind::GlyphRun) continue;
    if (primitive.role == MathMlPaintPrimitiveRole::ScriptSubscript)
      primitive.glyphRasterMode = MathMlGlyphRasterMode::Outline;
    if (containsLargeOperator &&
        primitive.role == MathMlPaintPrimitiveRole::Row) {
      primitive.glyphRasterMode = MathMlGlyphRasterMode::Outline;
      primitive.deterministicCoverage = true;
      primitive.rasterPhase =
          QPointF(0.0, kChromiumLargeOperatorRowBaselinePhase);
    }
  }
  return result;
}

void paintMathMlVerticalGlyphOperation(
    QPainter& painter, const MathCssVerticalGlyphOperation& glyph,
    const QColor& color) {
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
}

void paintMathMlPrimitives(
    QPainter& painter, const QVector<MathMlPaintPrimitive>& primitives,
    const QColor& color) {
  const auto paintVerticalGlyph = [&](const MathCssVerticalGlyphOperation& glyph) {
    paintMathMlVerticalGlyphOperation(painter, glyph, color);
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

  const auto paintGlyphRuns = [&](const QVector<MathCssGlyphRunOperation>& runs,
                                  bool outlineScale = false,
                                  bool deterministicCoverage = false,
                                  QPointF outlineOffset = {}) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    for (const MathCssGlyphRunOperation& glyph : runs) {
      if (glyph.glyphIndexes.isEmpty() || glyph.clip.isEmpty()) continue;
      if (outlineScale && !glyph.rawFont.isValid()) {
        if (muffin::mermaid::math::MathGlyphRasterizer::paintOutlineRun(
                painter, glyph.glyphIndexes, glyph.positions,
                glyph.baselineOrigin, glyph.fontScale, glyph.clip, color,
                deterministicCoverage, outlineOffset))
          continue;
      }
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
                                 glyph.placementExtent / 2.0;
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

  for (const MathMlPaintPrimitive& primitive : primitives) {
    switch (primitive.kind) {
      case MathMlPaintPrimitiveKind::GlyphRun: {
        paintGlyphRuns(
            {*std::get<const MathCssGlyphRunOperation*>(primitive.payload)},
            primitive.glyphRasterMode == MathMlGlyphRasterMode::Outline,
            primitive.deterministicCoverage,
            primitive.rasterPhase);
        break;
      }
      case MathMlPaintPrimitiveKind::VerticalGlyph:
        paintVerticalGlyph(*std::get<const MathCssVerticalGlyphOperation*>(
            primitive.payload));
        break;
      case MathMlPaintPrimitiveKind::FractionRule:
        paintRule(*std::get<const MathCssFractionBox*>(primitive.payload));
        break;
      case MathMlPaintPrimitiveKind::Radical:
        paintRadical(*std::get<const MathCssRadicalOperation*>(
            primitive.payload));
        break;
      case MathMlPaintPrimitiveKind::Accent:
        paintAccent(*std::get<const MathCssAccentOperation*>(
            primitive.payload));
        break;
    }
  }
}

void paintMathMlOperation(QPainter& painter,
                          const MathCssPaintOperation& operation,
                          const QColor& color,
                          MathMlPaintLayer layer) {
  paintMathMlPrimitives(
      painter, collectMathMlPaintPrimitives(operation, layer), color);
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
