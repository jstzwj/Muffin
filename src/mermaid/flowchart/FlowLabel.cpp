#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/MermaidFontRegistry.h"

#include "mermaid/math/MathMlCssLayout.h"
#include "math/OpenTypeMathFont.h"
#include "math/MathRenderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QMap>
#include <QPainter>
#include <QRectF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace muffin::mermaid::flowchart {
namespace {

struct Marker {
  QString token;
  QTextCharFormat format;
};

struct MathMlInlineMetrics {
  qreal visualWidth = 0.0;
  qreal advance = 0.0;
  qreal height = 22.0;
  FlowLabelMathStructure structure = FlowLabelMathStructure::None;
  qreal baseline = 0.0;
  qreal inkTop = 0.0;
  qreal inkBottom = 0.0;
};

const muffin::math::MathRenderNode* findMathNode(
    const muffin::math::MathRenderNode* node,
    muffin::math::MathRenderKind kind) {
  if (!node) return nullptr;
  if (node->kind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* found = findMathNode(child.get(), kind)) return found;
  return nullptr;
}

const muffin::math::MathRenderNode* singleMathSymbol(
    const muffin::math::MathRenderNode* node) {
  if (!node) return nullptr;
  if (node->kind == muffin::math::MathRenderKind::Symbol) return node;
  const muffin::math::MathRenderNode* result = nullptr;
  for (const auto& child : node->children) {
    const auto* symbol = singleMathSymbol(child.get());
    if (!symbol) {
      if (findMathNode(child.get(), muffin::math::MathRenderKind::Symbol))
        return nullptr;
      continue;
    }
    if (result) return nullptr;
    result = symbol;
  }
  return result;
}

QString mathMlDelimiterCharacter(QString delimiter) {
  if (delimiter == QLatin1String("\\lbrace") || delimiter == QLatin1String("\\{"))
    return QStringLiteral("{");
  if (delimiter == QLatin1String("\\rbrace") || delimiter == QLatin1String("\\}"))
    return QStringLiteral("}");
  return delimiter;
}

bool paintMathMlOperations(QPainter& painter,
                           const muffin::math::MathLayoutResult& layout,
                           const muffin::math::MathCssBox& box,
                           QColor color,
                           qreal renderFontPixelSize) {
  using namespace muffin::math;
  const auto operation = layoutMathMlPaintOperations(
      layout, renderFontPixelSize, 16.0);
  if (!operation || operation->container().isEmpty()) return false;

  const qreal scaleX = box.width / layout.naturalSize.width();
  const qreal scaleY = box.height / layout.naturalSize.height();
  const auto paintContent = [&](const QPainterPath& clip) {
    if (clip.isEmpty()) return;
    painter.save();
    painter.setClipPath(clip, Qt::IntersectClip);
    painter.scale(scaleX, scaleY);
    layout.paint(painter, QPointF());
    painter.restore();
  };

  const auto paintDelimiter = [&](QString delimiter, QRectF target) {
    if (delimiter.isEmpty() || delimiter == QLatin1String(".") ||
        target.isEmpty())
      return;
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const QString character = mathMlDelimiterCharacter(std::move(delimiter));
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const auto largestFixed = font.verticalVariant(
        character, std::numeric_limits<qreal>::max());
    const auto assembly = largestFixed &&
            target.height() > largestFixed->extent + 0.001
        ? font.verticalAssemblyParts(character, target.height())
        : std::optional<MathGlyphAssembly>{};
    if (assembly && !assembly->parts.isEmpty()) {
      const qreal assemblyTop = target.center().y() - assembly->extent / 2.0;
      for (const MathGlyphAssemblyPart& part : assembly->parts) {
        const QPainterPath path = font.glyphPath(part.glyphIndex);
        const QRectF bounds = path.boundingRect();
        if (bounds.isEmpty()) continue;
        QTransform placement;
        placement.translate(target.center().x() - bounds.center().x(),
                            assemblyTop + part.offset - bounds.top());
        painter.drawPath(placement.map(path));
      }
    } else if (const auto variant = font.verticalVariant(
                   character, target.height())) {
      const QPainterPath path = font.glyphPath(variant->glyphIndex);
      const QRectF bounds = path.boundingRect();
      if (!bounds.isEmpty()) {
        QTransform placement;
        placement.translate(target.center().x(), target.center().y());
        placement.translate(-bounds.center().x(), -bounds.center().y());
        painter.drawPath(placement.map(path));
      }
    }
    painter.restore();
  };

  const auto paintSubtree = [&](const MathRenderNode* node, QRectF target) {
    if (!node || target.isEmpty() || node->width <= 0.0 ||
        node->height + node->depth <= 0.0)
      return;
    if (const MathRenderNode* symbol = singleMathSymbol(node);
        symbol && symbol->text.size() == 1) {
      const OpenTypeMathFont& font = OpenTypeMathFont::instance();
      const QChar character = symbol->text.front();
      const bool italic = character.isLetter() &&
          (symbol->fontClass == QLatin1String("mathnormal") ||
           symbol->fontClass == QLatin1String("mathit"));
      const auto glyph = italic ? font.mathItalicGlyph(character)
                                : font.glyph(symbol->text);
      if (glyph && glyph->advance > 0.0 && !glyph->inkBounds.isEmpty()) {
        const QPainterPath path = font.glyphPath(glyph->glyphIndex);
        const qreal scaleX = target.width() / glyph->advance;
        const qreal scaleY = target.height() / glyph->inkBounds.height();
        QTransform placement;
        placement.translate(target.left(),
                            target.top() - glyph->inkBounds.top() * scaleY);
        placement.scale(scaleX, scaleY);
        painter.save();
        painter.setClipRect(target, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(placement.map(path));
        painter.restore();
        return;
      }
    }
    painter.save();
    painter.setClipRect(target, Qt::IntersectClip);
    painter.translate(target.left(), target.top());
    painter.scale(target.width() / node->width,
                  target.height() / (node->height + node->depth));
    node->paint(painter, QPointF(0.0, node->height));
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
    if (radical.glyphIndex == 0 || radical.glyph.isEmpty()) return;
    const QPainterPath path = OpenTypeMathFont::instance().glyphPath(
        radical.glyphIndex);
    const QRectF bounds = path.boundingRect();
    if (bounds.isEmpty()) return;
    QTransform placement;
    placement.translate(radical.glyph.left() - bounds.left(),
                        radical.glyph.top() - bounds.top());
    painter.save();
    painter.setClipRect(
        radical.glyph.intersected(radical.container), Qt::IntersectClip);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(placement.map(path));
    painter.restore();
    paintSolidRect(radical.rule);
  };

  const auto paintAccent = [&](const MathCssAccentOperation& accent) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const qreal variantTarget = accent.fixedVariantTargetWidth > 0.0
        ? accent.fixedVariantTargetWidth : accent.box.accent.width();
    const qreal designExtent = variantTarget / accent.box.fontScale;
    const auto fixedVariant = font.horizontalVariant(
        accent.box.character, variantTarget);
    const bool useFixedVariant = fixedVariant &&
        fixedVariant->extent >= variantTarget;
    const auto assembly = useFixedVariant
        ? std::optional<MathGlyphAssembly>{}
        : font.horizontalAssemblyParts(accent.box.character, designExtent);
    if (!useFixedVariant && (!assembly || assembly->parts.isEmpty())) return;

    painter.save();
    painter.setClipRect(accent.box.accent, Qt::IntersectClip);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (useFixedVariant) {
      const QPainterPath path = font.glyphPath(fixedVariant->glyphIndex);
      const QRectF bounds = path.boundingRect();
      if (!bounds.isEmpty()) {
        const qreal scale = accent.fixedVariantUsesNaturalScale
            ? accent.box.fontScale
            : accent.box.accent.width() / fixedVariant->extent;
        QTransform placement;
        placement.translate(accent.box.accent.center().x(),
                            accent.box.accent.center().y());
        placement.scale(scale, accent.box.fontScale);
        placement.translate(-bounds.center().x(), -bounds.center().y());
        painter.drawPath(placement.map(path));
      }
    } else {
      const qreal assemblyLeft = accent.box.accent.center().x() -
                                 variantTarget / 2.0;
      for (const MathGlyphAssemblyPart& part : assembly->parts) {
        const QPainterPath path = font.glyphPath(part.glyphIndex);
        const QRectF bounds = path.boundingRect();
        if (bounds.isEmpty()) continue;
        QTransform placement;
        placement.translate(assemblyLeft +
                                part.offset * accent.box.fontScale,
                            accent.box.accent.center().y());
        placement.scale(accent.box.fontScale, accent.box.fontScale);
        placement.translate(-bounds.left(), -bounds.center().y());
        painter.drawPath(placement.map(path));
      }
    }
    painter.restore();
  };

  const auto intersects = [](const MathCssPaintOperation& child, QRectF area) {
    return child.container().intersects(area);
  };
  const auto exclusionPath = [&](QRectF area,
                                 const QVector<MathCssPaintOperation>& children) {
    QPainterPath clip;
    clip.setFillRule(Qt::OddEvenFill);
    const QRectF expanded = area.adjusted(-1.0, -2.0, 1.0, 2.0);
    clip.addRect(expanded);
    for (const MathCssPaintOperation& child : children) {
      const QRectF container = child.container();
      if (!container.intersects(area)) continue;
      constexpr qreal kOwnershipTolerance = 0.25;
      const bool ownsArea = std::abs(container.left() - area.left()) <=
                                kOwnershipTolerance &&
                            std::abs(container.right() - area.right()) <=
                                kOwnershipTolerance &&
                            std::abs(container.top() - area.top()) <=
                                kOwnershipTolerance &&
                            std::abs(container.bottom() - area.bottom()) <=
                                kOwnershipTolerance;
      clip.addRect(ownsArea ? expanded
                            : container.adjusted(0.0, -2.0, 0.0, 2.0));
    }
    return clip;
  };
  const auto paintOwnedSubtree = [&](const MathRenderNode* node, QRectF target,
                                     const QVector<MathCssPaintOperation>& children) {
    const bool hasChild = std::any_of(
        children.cbegin(), children.cend(),
        [&](const MathCssPaintOperation& child) {
          return intersects(child, target);
        });
    if (!hasChild) {
      paintSubtree(node, target);
      return;
    }
    if (!node || target.isEmpty() || node->width <= 0.0 ||
        node->height + node->depth <= 0.0)
      return;
    painter.save();
    painter.setClipPath(exclusionPath(target, children), Qt::IntersectClip);
    painter.translate(target.left(), target.top());
    painter.scale(target.width() / node->width,
                  target.height() / (node->height + node->depth));
    node->paint(painter, QPointF(0.0, node->height));
    painter.restore();
  };

  const auto paintSourceSubtree = [&] (
      const MathRenderNode* node, QPointF sourceOrigin, QRectF target,
      const QVector<MathCssPaintOperation>& children) {
    if (!node || target.isEmpty() || node->width <= 0.0 ||
        node->height + node->depth <= 0.0)
      return;
    const QRectF source(sourceOrigin.x(), sourceOrigin.y() - node->height,
                        node->width, node->height + node->depth);
    painter.save();
    painter.setClipPath(exclusionPath(target, children), Qt::IntersectClip);
    painter.translate(target.left(), target.top());
    painter.scale(target.width() / source.width(),
                  target.height() / source.height());
    painter.translate(-source.left(), -source.top());
    node->paint(painter, sourceOrigin);
    painter.restore();
  };

  const auto paintSourceSubtreeAtLayoutScale = [&] (
      const MathRenderNode* node, QPointF sourceOrigin, QRectF target,
      const QVector<MathCssPaintOperation>& children) {
    if (!node || target.isEmpty()) return;
    painter.save();
    painter.setClipPath(exclusionPath(target, children), Qt::IntersectClip);
    painter.scale(scaleX, scaleY);
    node->paint(painter, sourceOrigin);
    painter.restore();
  };

  std::function<void(const MathCssPaintOperation&)> paintOperation;
  paintOperation = [&](const MathCssPaintOperation& current) {
    if (const auto* fraction =
            std::get_if<MathCssFractionPaint>(&current.payload)) {
      const auto paintRow = [&](const MathRenderNode* node, QRectF row) {
      if (row.isEmpty()) return;
      const bool containsChild = std::any_of(
          current.children.cbegin(), current.children.cend(),
          [&](const MathCssPaintOperation& child) {
            return intersects(child, row);
          });
      if (fraction->nested && !containsChild) {
        paintSubtree(node, row);
        return;
      }
      paintContent(exclusionPath(row, current.children));
      };
      paintRow(fraction->numeratorNode, fraction->box.numerator);
      paintRow(fraction->denominatorNode, fraction->box.denominator);
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      paintDelimiter(fraction->box.leftDelimiterCharacter,
                     fraction->box.leftDelimiter);
      paintDelimiter(fraction->box.rightDelimiterCharacter,
                     fraction->box.rightDelimiter);
      paintRule(fraction->box);
      return;
    }

    if (const auto* array =
            std::get_if<MathCssArrayOperation>(&current.payload)) {
      for (const MathCssArrayCell& cell : array->cells)
        paintOwnedSubtree(
            cell.contentNode, cell.content, current.children);
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      paintDelimiter(array->leftDelimiterCharacter, array->leftDelimiter);
      paintDelimiter(array->rightDelimiterCharacter, array->rightDelimiter);
      return;
    }

    if (const auto* accent =
            std::get_if<MathCssAccentOperation>(&current.payload)) {
      if (accent->hasBodySourceOrigin) {
        if (accent->bodyUsesLayoutScale)
          paintSourceSubtreeAtLayoutScale(
              accent->bodyNode, accent->bodySourceOrigin,
              accent->box.body, current.children);
        else
          paintSourceSubtree(accent->bodyNode, accent->bodySourceOrigin,
                             accent->box.body, current.children);
      }
      if (accent->hasAnnotationSourceOrigin)
        paintSourceSubtree(accent->annotationNode,
                           accent->annotationSourceOrigin,
                           accent->annotationContent, current.children);
      for (const MathCssPaintOperation& child : current.children)
        paintOperation(child);
      paintAccent(*accent);
      return;
    }

    if (const auto* script =
            std::get_if<MathCssScriptOperation>(&current.payload)) {
      paintOwnedSubtree(script->baseNode, script->base, current.children);
      paintOwnedSubtree(
          script->superscriptNode, script->superscript, current.children);
      paintOwnedSubtree(
          script->subscriptNode, script->subscript, current.children);
    } else if (const auto* radical =
                   std::get_if<MathCssRadicalOperation>(&current.payload)) {
      paintOwnedSubtree(radical->bodyNode, radical->body, current.children);
    }
    for (const MathCssPaintOperation& child : current.children)
      paintOperation(child);
    if (const auto* radical =
            std::get_if<MathCssRadicalOperation>(&current.payload))
      paintRadical(*radical);
  };
  paintOperation(*operation);

  QPainterPath outside;
  const qreal operationLeft = operation->container().left();
  const qreal operationRight = operation->container().right();
  outside.addRect(QRectF(0.0, 0.0, std::max<qreal>(0.0, operationLeft),
                         box.height));
  outside.addRect(QRectF(operationRight, 0.0,
                         std::max<qreal>(0.0, box.width - operationRight),
                         box.height));
  paintContent(outside);
  return true;
}

FlowLabelMathStructure flowMathStructure(muffin::math::MathSemanticKind kind) {
  switch (kind) {
    case muffin::math::MathSemanticKind::Fraction: return FlowLabelMathStructure::Fraction;
    case muffin::math::MathSemanticKind::Radical: return FlowLabelMathStructure::Radical;
    case muffin::math::MathSemanticKind::SupSub: return FlowLabelMathStructure::SupSub;
    case muffin::math::MathSemanticKind::Array: return FlowLabelMathStructure::Array;
    case muffin::math::MathSemanticKind::None: return FlowLabelMathStructure::Plain;
  }
  return FlowLabelMathStructure::Plain;
}

MathMlInlineMetrics sequenceMathMlMetrics(const muffin::math::MathLayoutResult& layout,
                                          qreal renderFontPixelSize) {
  const muffin::math::MathCssBox box = muffin::math::layoutMathMlCssBox(
      layout, renderFontPixelSize, 16.0);
  return {box.width, box.advance, box.height, flowMathStructure(box.semanticKind),
          box.baseline, box.inkTop, box.inkBottom};
}

QString decodeEntity(QStringView source, qsizetype* consumed) {
  *consumed = 0;
  if (!source.startsWith(QLatin1Char('&'))) return {};
  const qsizetype semicolon = source.indexOf(QLatin1Char(';'));
  if (semicolon < 2 || semicolon > 12) return {};
  const QString name = source.mid(1, semicolon - 1).toString();
  static const QMap<QString, QString> named = {
      {QStringLiteral("amp"), QStringLiteral("&")},
      {QStringLiteral("apos"), QStringLiteral("'")},
      {QStringLiteral("gt"), QStringLiteral(">")},
      {QStringLiteral("lt"), QStringLiteral("<")},
      {QStringLiteral("nbsp"), QString(QChar(0x00a0))},
      {QStringLiteral("quot"), QStringLiteral("\"")},
  };
  QString value = named.value(name.toLower());
  if (value.isEmpty() && name.startsWith(QLatin1Char('#'))) {
    bool ok = false;
    const bool hexadecimal = name.size() > 2 && name.at(1).toLower() == QLatin1Char('x');
    const uint codepoint = name.mid(hexadecimal ? 2 : 1).toUInt(&ok, hexadecimal ? 16 : 10);
    if (ok && codepoint <= 0x10ffff && !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      const char32_t scalar = codepoint;
      value = QString::fromUcs4(&scalar, 1);
    }
  }
  if (value.isEmpty()) return {};
  *consumed = semicolon + 1;
  return value;
}

QString normalizeBreaks(QString text) {
  static const QRegularExpression br(QStringLiteral(R"(<\s*br\s*/?\s*>)"),
                                      QRegularExpression::CaseInsensitiveOption);
  text.replace(br, QStringLiteral("\n"));
  return text;
}

qreal chromiumFallbackWidth(const FlowLabelDocument& label, qsizetype lineStart,
                            const QString& text, qreal qtWidth, qreal fontPixelSize) {
  const bool japaneseRun = std::any_of(text.cbegin(), text.cend(), [](QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x3040 && code <= 0x30ff) || (code >= 0x31f0 && code <= 0x31ff);
  });
  qreal width = qtWidth;
  const qsizetype fullWidthCharacters = std::count_if(text.cbegin(), text.cend(), [](QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x2e80 && code <= 0x9fff) ||
           (code >= 0x3040 && code <= 0x30ff) ||
           (code >= 0xac00 && code <= 0xd7af);
  });
  if (japaneseRun) {
    // DirectWrite exposes Yu Gothic's 2040/2048-em advance to Chromium, while
    // Qt normalizes the same fallback glyphs to exactly one em.
    width -= fullWidthCharacters * fontPixelSize * (8.0 / 2048.0);
  }
  qsizetype syntheticBoldCjk = 0;
  bool lineOnlyArabicWhitespace = true;
  qsizetype arabicCharacters = 0;
  for (QChar ch : text) {
    const ushort code = ch.unicode();
    if (code >= 0x0600 && code <= 0x06ff)
      ++arabicCharacters;
    else if (!ch.isSpace())
      lineOnlyArabicWhitespace = false;
  }
  for (const auto& range : label.formats) {
    if (range.format.fontWeight() < QFont::Bold) continue;
    const qsizetype begin = std::max<qsizetype>(lineStart, range.start);
    const qsizetype end = std::min<qsizetype>(lineStart + text.size(),
                                              range.start + range.length);
    for (qsizetype index = begin; index < end; ++index) {
      const ushort code = label.text.at(index).unicode();
      if ((code >= 0x2e80 && code <= 0x9fff) ||
          (code >= 0x3040 && code <= 0x30ff))
        ++syntheticBoldCjk;
    }
  }
  // Qt synthesizes bold for SimSun by widening each glyph; Chromium selects a
  // DirectWrite fallback face whose CJK advance remains one em.
  width -= syntheticBoldCjk * fontPixelSize * (37.0 / 2048.0);
  if (lineOnlyArabicWhitespace)
    width += arabicCharacters * fontPixelSize * (8.0 / 2048.0);
  return width;
}

void appendFormatted(FlowLabelDocument& result, const QString& text,
                     const QTextCharFormat& format) {
  if (text.isEmpty()) return;
  if (format.isEmpty()) {
    result.text += text;
    return;
  }
  QTextLayout::FormatRange range;
  range.start = result.text.size();
  range.length = text.size();
  range.format = format;
  result.text += text;
  result.formats.push_back(range);
}

FlowLabelDocument parseMarkup(QString source, bool markdown, bool mathEnabled) {
  FlowLabelDocument result;
  source = normalizeBreaks(std::move(source));
  QVector<Marker> stack;
  QString plain;
  auto flush = [&]() {
    if (plain.isEmpty()) return;
    QTextCharFormat combined;
    for (const Marker& marker : stack) combined.merge(marker.format);
    appendFormatted(result, plain, combined);
    plain.clear();
  };

  for (qsizetype i = 0; i < source.size();) {
    QString token;
    QTextCharFormat format;
    qsizetype consumed = 0;
    const QStringView rest(source.constData() + i, source.size() - i);
    auto htmlToken = [&](QStringView name, bool closing) {
      const QString candidate = closing ? QStringLiteral("</%1>").arg(name)
                                        : QStringLiteral("<%1>").arg(name);
      if (rest.startsWith(candidate, Qt::CaseInsensitive)) {
        token = candidate.toLower();
        consumed = candidate.size();
        return true;
      }
      return false;
    };

    bool closing = false;
    if (mathEnabled && rest.startsWith(QStringLiteral("$$"))) {
      const qsizetype close = source.indexOf(QStringLiteral("$$"), i + 2);
      if (close >= 0) {
        flush();
        FlowLabelMathSpan math;
        math.start = result.text.size();
        math.source = source.mid(i + 2, close - i - 2);
        result.text += QChar::ObjectReplacementCharacter;
        result.math.push_back(std::move(math));
        i = close + 2;
        continue;
      }
    }
    if (markdown && rest.startsWith(QStringLiteral("**"))) {
      token = QStringLiteral("**"); consumed = 2; format.setFontWeight(QFont::Bold);
    } else if (markdown && rest.startsWith(QLatin1Char('*'))) {
      token = QStringLiteral("*"); consumed = 1; format.setFontItalic(true);
    } else if (markdown && rest.startsWith(QLatin1Char('`'))) {
      token = QStringLiteral("`"); consumed = 1; format.setFontFamilies({QStringLiteral("monospace")});
    } else if (htmlToken(QStringLiteral("strong"), false) || htmlToken(QStringLiteral("b"), false)) {
      format.setFontWeight(QFont::Bold);
    } else if ((closing = htmlToken(QStringLiteral("strong"), true)) ||
               (closing = htmlToken(QStringLiteral("b"), true))) {
    } else if (htmlToken(QStringLiteral("em"), false) || htmlToken(QStringLiteral("i"), false)) {
      format.setFontItalic(true);
    } else if ((closing = htmlToken(QStringLiteral("em"), true)) ||
               (closing = htmlToken(QStringLiteral("i"), true))) {
    } else if (htmlToken(QStringLiteral("code"), false)) {
      format.setFontFamilies({QStringLiteral("monospace")});
    } else if ((closing = htmlToken(QStringLiteral("code"), true))) {
    }

    if (consumed == 0) {
      qsizetype entityLength = 0;
      const QString entity = decodeEntity(rest, &entityLength);
      if (entityLength > 0) {
        plain += entity;
        i += entityLength;
        continue;
      }
      // Unknown tags are retained as text. They are never interpreted by an
      // HTML engine, which preserves content without creating an injection path.
      plain += source.at(i++);
      continue;
    }

    flush();
    if (!stack.isEmpty() && stack.last().token == token) {
      stack.removeLast();
    } else if (closing) {
      const QString open = token;
      for (qsizetype s = stack.size() - 1; s >= 0; --s) {
        if (open.contains(stack.at(s).token.mid(1).section(QLatin1Char('>'), 0, 0), Qt::CaseInsensitive)) {
          stack.remove(s);
          break;
        }
      }
    } else {
      stack.push_back({token, format});
    }
    i += consumed;
  }
  flush();
  return result;
}

}  // namespace

FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled) {
  if (labelType == QLatin1String("markdown")) {
    // Mermaid's SVG-text Math path bypasses Markdown when a Markdown label also
    // contains an HTML break. The markers stay literal and the break collapses
    // between the adjacent text/Math spans.
    if (mathEnabled && source.contains(QStringLiteral("$$")) &&
        source.contains(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                           QRegularExpression::CaseInsensitiveOption))) {
      QString collapsed = normalizeBreaks(source);
      collapsed.remove(QLatin1Char('\n'));
      FlowLabelDocument result = parseMarkup(std::move(collapsed), false, true);
      result.literalMarkdownMathFallback = true;
      return result;
    }
    return parseMarkup(source, true, mathEnabled);
  }
  if (source.contains(QLatin1Char('<')) ||
      (mathEnabled && source.contains(QStringLiteral("$$"))))
    return parseMarkup(source, false, mathEnabled);
  return {.text = normalizeBreaks(source)};
}

qreal measureTextRange(const FlowLabelDocument& label, qsizetype start, qsizetype length,
                       const QFont& font) {
  if (length <= 0) return 0.0;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setTextDirection(label.direction);
  layout.setTextOption(option);
  QVector<QTextLayout::FormatRange> ranges;
  for (const auto& range : label.formats) {
    const qsizetype overlapStart = std::max<qsizetype>(range.start, start);
    const qsizetype overlapEnd = std::min<qsizetype>(range.start + range.length, start + length);
    if (overlapEnd <= overlapStart) continue;
    auto local = range;
    local.start = overlapStart - start;
    local.length = overlapEnd - overlapStart;
    ranges.push_back(local);
  }
  layout.setFormats(ranges);
  layout.beginLayout();
  QTextLine line = layout.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  layout.endLayout();
  return line.isValid() ? line.naturalTextWidth() : 0.0;
}

QVector<QTextLayout::FormatRange> localFormats(const FlowLabelDocument& label,
                                                qsizetype start, qsizetype length) {
  QVector<QTextLayout::FormatRange> ranges;
  for (const auto& range : label.formats) {
    const qsizetype overlapStart = std::max<qsizetype>(range.start, start);
    const qsizetype overlapEnd = std::min<qsizetype>(range.start + range.length,
                                                     start + length);
    if (overlapEnd <= overlapStart) continue;
    auto local = range;
    local.start = overlapStart - start;
    local.length = overlapEnd - overlapStart;
    ranges.push_back(local);
  }
  return ranges;
}

void drawTextRange(QPainter& painter, const FlowLabelDocument& label,
                   qsizetype start, qsizetype length, const QFont& font,
                   QPointF topLeft, qreal scaleX) {
  if (length <= 0) return;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setTextDirection(label.direction);
  layout.setTextOption(option);
  layout.setFormats(localFormats(label, start, length));
  layout.beginLayout();
  QTextLine line = layout.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  layout.endLayout();
  painter.save();
  painter.translate(topLeft);
  painter.scale(scaleX, 1.0);
  layout.draw(&painter, QPointF());
  painter.restore();
}

QSizeF measureFlowLabel(const FlowLabelDocument& label, const QString& fontFamily,
                        qreal fontPixelSize, qreal lineHeight) {
  return layoutFlowLabel(label, fontFamily, fontPixelSize, lineHeight).size;
}

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize, qreal lineHeight) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  const QRectF inkMetrics = metrics.tightBoundingRect(QStringLiteral("Mg"));
  // Canvas TextMetrics reports the pixel-aligned ink box used by Chromium's
  // foreignObject labels. Qt exposes the fractional outline box; align its top
  // outward and bottom inward to reproduce actualBoundingBoxAscent/Descent.
  const qreal cssAscent = std::ceil(std::max<qreal>(0.0, -inkMetrics.top()));
  const qreal cssDescent = std::floor(std::max<qreal>(0.0, inkMetrics.bottom()));
  FlowLabelLayoutMetrics result;
  const QStringList lines = label.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  for (const QString& line : lines) {
    FlowLabelLineMetrics measured;
    measured.start = offset;
    measured.length = line.size();
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : label.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);
    if (!mathSpans.isEmpty()) {
      qreal lineWidth = 0.0;
      qreal actualLineHeight = lineHeight;
      qsizetype cursor = offset;
      muffin::math::MathRenderer renderer;
      auto rangeIsRtl = [&](qsizetype start, qsizetype length) {
        if (length <= 0) return false;
        const QString directionText = label.text.mid(start, length).trimmed();
        if (directionText.isEmpty()) return false;
        QTextLayout directionLayout(directionText, font);
        QTextOption directionOption;
        directionOption.setUseDesignMetrics(true);
        directionOption.setTextDirection(label.direction);
        directionLayout.setTextOption(directionOption);
        directionLayout.beginLayout();
        QTextLine directionLine = directionLayout.createLine();
        if (directionLine.isValid()) directionLine.setLineWidth(1e9);
        directionLayout.endLayout();
        const auto runs = directionLine.isValid()
            ? directionLine.glyphRuns(0, -1, QTextLayout::RetrieveAll) : QList<QGlyphRun>{};
        return !runs.isEmpty() &&
            std::all_of(runs.cbegin(), runs.cend(), [](const QGlyphRun& run) {
              return run.isRightToLeft();
            });
      };
      auto mathTextWidth = [&](qsizetype start, qsizetype length) {
        const bool literalMarkdownMarkers = label.text.contains(QLatin1Char('`')) ||
                                            label.text.contains(QStringLiteral("**"));
        if (label.sequenceMathMlModel && !literalMarkdownMarkers) {
          while (length > 0 && label.text.at(start).isSpace()) {
            ++start;
            --length;
          }
          while (length > 0 && label.text.at(start + length - 1).isSpace())
            --length;
        }
        qreal width = 0.0;
        const qsizetype end = start + length;
        const bool segmentHasFullWidth = std::any_of(
            label.text.cbegin() + start, label.text.cbegin() + end, [](QChar ch) {
              const ushort code = ch.unicode();
              return (code >= 0x2e80 && code <= 0x9fff) ||
                     (code >= 0x3040 && code <= 0x30ff) ||
                     (code >= 0xac00 && code <= 0xd7af);
            });
        for (qsizetype runStart = start; runStart < end;) {
          while (segmentHasFullWidth && runStart < end &&
                 label.text.at(runStart).isSpace()) ++runStart;
          if (runStart >= end) break;
          const ushort code = label.text.at(runStart).unicode();
          const bool fullWidth = (code >= 0x2e80 && code <= 0x9fff) ||
                                 (code >= 0x3040 && code <= 0x30ff) ||
                                 (code >= 0xac00 && code <= 0xd7af);
          qsizetype runEnd = runStart + 1;
          while (runEnd < end &&
                 !(segmentHasFullWidth && label.text.at(runEnd).isSpace())) {
            const ushort next = label.text.at(runEnd).unicode();
            const bool nextFullWidth = (next >= 0x2e80 && next <= 0x9fff) ||
                                       (next >= 0x3040 && next <= 0x30ff) ||
                                       (next >= 0xac00 && next <= 0xd7af);
            if (nextFullWidth != fullWidth) break;
            ++runEnd;
          }
          const qreal raw = measureTextRange(label, runStart, runEnd - runStart, font);
          qreal measuredWidth = fullWidth
              ? chromiumFallbackWidth(label, runStart,
                                      label.text.mid(runStart, runEnd - runStart),
                                      raw, fontPixelSize)
              : raw * (label.literalMarkdownMathFallback ? 1.0
                                                         : kFlowMathMlTextScaleX);
          if (label.sequenceMathMlModel && !literalMarkdownMarkers && !fullWidth) {
            const QString segment = label.text.mid(runStart, runEnd - runStart);
            const bool arabic = std::any_of(segment.cbegin(), segment.cend(), [](QChar ch) {
              return ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff;
            });
            measuredWidth *= arabic ? (33.672 / 34.421875)
                                    : (183.375 / 183.1875);
          }
          width += measuredWidth;
          runStart = runEnd;
        }
        return width;
      };
      qreal visualRight = 0.0;
      for (const FlowLabelMathSpan& math : mathSpans) {
        const qreal textWidth = mathTextWidth(cursor, math.start - cursor);
        if (math.start > cursor)
          measured.runs.push_back({cursor, math.start - cursor, lineWidth, textWidth,
                                   rangeIsRtl(cursor, math.start - cursor), false, font.family()});
        lineWidth += textWidth;
        const muffin::math::MathLayoutResult layout = renderer.render(
            math.source, fontPixelSize * 1.21, Qt::black, true);
        if (layout.valid()) {
          const qreal mathScaleX = label.literalMarkdownMathFallback
                                       ? kFlowMathMlLiteralFallbackScaleX
                                       : kFlowMathMlScaleX;
          const qreal nativeMathWidth = layout.naturalSize.width() * mathScaleX;
          const qreal nativeMathHeight = layout.naturalSize.height() * kFlowMathMlScaleY;
          const MathMlInlineMetrics mathMetrics = label.sequenceMathMlModel
              ? sequenceMathMlMetrics(layout, fontPixelSize * 1.21)
              : MathMlInlineMetrics{nativeMathWidth, nativeMathWidth, nativeMathHeight,
                                    FlowLabelMathStructure::Plain};
          measured.runs.push_back({math.start, math.length, lineWidth, mathMetrics.visualWidth,
                                   false, true, QStringLiteral("KaTeX_Main"),
                                   mathMetrics.structure});
          auto& mathRun = measured.runs.back();
          mathRun.mathBoxHeight = mathMetrics.height;
          mathRun.mathBaseline = mathMetrics.baseline;
          mathRun.mathInkTop = mathMetrics.inkTop;
          mathRun.mathInkBottom = mathMetrics.inkBottom;
          visualRight = std::max(visualRight, lineWidth + mathMetrics.visualWidth);
          lineWidth += mathMetrics.advance;
          actualLineHeight = std::max(actualLineHeight, mathMetrics.height);
        }
        cursor = math.start + math.length;
      }
      const qreal tailWidth = mathTextWidth(cursor, offset + line.size() - cursor);
      if (cursor < offset + line.size())
        measured.runs.push_back({cursor, offset + line.size() - cursor, lineWidth,
                                 tailWidth, rangeIsRtl(cursor, offset + line.size() - cursor),
                                 false, font.family()});
      lineWidth += tailWidth;
      if (cursor >= offset + line.size() && !measured.runs.isEmpty() &&
          measured.runs.back().math &&
          measured.runs.back().mathStructure == FlowLabelMathStructure::Fraction)
        lineWidth = std::max<qreal>(0.0, lineWidth - 1.0);
      visualRight = std::max(visualRight, lineWidth);
      if (label.sequenceMathMlModel && actualLineHeight > lineHeight) {
        const bool mixedFallback = std::any_of(line.cbegin(), line.cend(), [](QChar ch) {
          return (ch.unicode() >= 0x2e80 && ch.unicode() <= 0x9fff) ||
                 (ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff);
        });
        if (mixedFallback) actualLineHeight = std::max(actualLineHeight, 34.0);
      }
      measured.width = lineWidth;
      // The fallback is emitted as one SVG text range (17px ink box) inside
      // the normal 24px label block.
      measured.height = label.literalMarkdownMathFallback ? 17.0 : actualLineHeight;
      measured.ascent = cssAscent;
      measured.descent = cssDescent;
      measured.baseline = (actualLineHeight - cssAscent - cssDescent) / 2.0 + cssAscent;
      result.size.setWidth(std::max(result.size.width(), label.sequenceMathMlModel
          ? std::round(visualRight) : lineWidth));
      result.size.setHeight(result.size.height() + actualLineHeight);
      result.lines.push_back(std::move(measured));
      offset += line.size() + 1;
      continue;
    }
    QTextLayout layout(line, font);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setTextDirection(label.direction);
    layout.setTextOption(option);
    QVector<QTextLayout::FormatRange> ranges;
    for (const auto& range : label.formats) {
      const int start = std::max<int>(range.start, offset);
      const int end = std::min<int>(range.start + range.length, offset + line.size());
      if (end > start) {
        auto local = range;
        local.start = start - offset;
        local.length = end - start;
        ranges.push_back(local);
      }
    }
    layout.setFormats(ranges);
    layout.beginLayout();
    QTextLine textLine = layout.createLine();
    if (textLine.isValid()) textLine.setLineWidth(1e9);
    layout.endLayout();
    const qreal qtWidth = textLine.isValid() ? textLine.naturalTextWidth()
                                             : metrics.horizontalAdvance(line);
    measured.width = chromiumFallbackWidth(label, offset, line, qtWidth, fontPixelSize);
    measured.height = lineHeight;
    measured.ascent = cssAscent;
    measured.descent = cssDescent;
    measured.baseline = (lineHeight - cssAscent - cssDescent) / 2.0 + cssAscent;
    if (textLine.isValid()) {
      const auto glyphRuns = textLine.glyphRuns(0, -1, QTextLayout::RetrieveAll);
      bool hasGlyphRun = false;
      bool allRunsRtl = true;
      for (const QGlyphRun& run : glyphRuns) {
        const QList<qsizetype> indexes = run.stringIndexes();
        if (indexes.isEmpty()) continue;
        const auto [minimum, maximum] = std::minmax_element(indexes.cbegin(), indexes.cend());
        const QRectF bounds = run.boundingRect();
        measured.runs.push_back({offset + *minimum, *maximum - *minimum + 1,
                                 bounds.x(), bounds.width(), run.isRightToLeft(), false,
                                 run.rawFont().familyName()});
        hasGlyphRun = true;
        allRunsRtl = allRunsRtl && run.isRightToLeft();
      }
      // For the bundled Noto faces, SVG getBBox() uses the painted glyph bounds
      // while QTextLine includes direction-dependent advances/bearings. Keep
      // the legacy system-font correction path unchanged.
      if (hasGlyphRun && allRunsRtl &&
          font.family().contains(QStringLiteral("Noto Sans"), Qt::CaseInsensitive))
        measured.width = metrics.tightBoundingRect(line).width();
      std::sort(measured.runs.begin(), measured.runs.end(),
                [](const FlowLabelVisualRun& a, const FlowLabelVisualRun& b) {
                  return a.x < b.x;
                });
      if (qtWidth > 0.0 && measured.width != qtWidth) {
        const qreal scale = measured.width / qtWidth;
        for (auto& run : measured.runs) {
          run.x *= scale;
          run.width *= scale;
        }
      }
    }
    result.size.setWidth(std::max(result.size.width(), measured.width));
    result.size.setHeight(result.size.height() + lineHeight);
    result.lines.push_back(std::move(measured));
    offset += line.size() + 1;
  }
  result.size.setHeight(std::max(lineHeight, result.size.height()));
  return result;
}

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  FlowLabelDocument paintedLabel = label;
  bool autoWrapped = false;
  const qreal availableWidth = std::max<qreal>(0.0, rect.width() - 4.0);
  if (paintedLabel.math.isEmpty() && !paintedLabel.text.contains(QLatin1Char('\n')) &&
      availableWidth > 0.0 &&
      measureTextRange(paintedLabel, 0, paintedLabel.text.size(), font) > availableWidth) {
    qsizetype lineStart = 0;
    qsizetype separator = paintedLabel.text.indexOf(QLatin1Char(' '));
    while (separator >= 0) {
      const qsizetype nextSeparator = paintedLabel.text.indexOf(QLatin1Char(' '), separator + 1);
      const qsizetype candidateEnd = nextSeparator >= 0 ? nextSeparator : paintedLabel.text.size();
      if (measureTextRange(paintedLabel, lineStart, candidateEnd - lineStart, font) >
              availableWidth && separator > lineStart) {
        paintedLabel.text[separator] = QLatin1Char('\n');
        autoWrapped = true;
        lineStart = separator + 1;
      }
      separator = nextSeparator;
    }
  }
  const qreal effectiveLineHeight = autoWrapped ? 19.3 : lineHeight;
  const FlowLabelLayoutMetrics layoutMetrics =
      layoutFlowLabel(paintedLabel, fontFamily, fontPixelSize, effectiveLineHeight);
  const QSizeF measured = layoutMetrics.size;
  const qreal qtAscent = QFontMetricsF(font).ascent();
  qreal lineTop = centerVertically
                      ? rect.top() + std::max<qreal>(0.0, (rect.height() - measured.height()) / 2.0)
                      : rect.top();
  painter.setPen(color);
  const QStringList lines = paintedLabel.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  qsizetype lineIndex = 0;
  muffin::math::MathRenderer renderer;
  for (const QString& line : lines) {
    const FlowLabelLineMetrics& measuredLine = layoutMetrics.lines.at(lineIndex++);
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : paintedLabel.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);

    qreal actualLineHeight = effectiveLineHeight;
    std::vector<muffin::math::MathLayoutResult> mathLayouts;
    std::vector<MathMlInlineMetrics> mathMetrics;
    mathLayouts.reserve(mathSpans.size());
    mathMetrics.reserve(mathSpans.size());
    for (const FlowLabelMathSpan& math : mathSpans) {
      mathLayouts.push_back(renderer.render(math.source, fontPixelSize * 1.21, color, true));
      if (mathLayouts.back().valid()) {
        const qreal mathScaleX = paintedLabel.literalMarkdownMathFallback
                                     ? kFlowMathMlLiteralFallbackScaleX
                                     : kFlowMathMlScaleX;
        const qreal nativeWidth = mathLayouts.back().naturalSize.width() * mathScaleX;
        const qreal nativeHeight = mathLayouts.back().naturalSize.height() * kFlowMathMlScaleY;
        mathMetrics.push_back(paintedLabel.sequenceMathMlModel
            ? sequenceMathMlMetrics(mathLayouts.back(), fontPixelSize * 1.21)
            : MathMlInlineMetrics{nativeWidth, nativeWidth, nativeHeight,
                                  FlowLabelMathStructure::Plain});
        actualLineHeight = std::max(actualLineHeight, mathMetrics.back().height);
      } else {
        mathMetrics.push_back({});
      }
    }

    const qreal lineWidth = measuredLine.width;
    qreal x = rect.left() + (rect.width() - lineWidth) / 2.0;
    if (mathSpans.isEmpty()) {
      const qreal rawWidth = measureTextRange(paintedLabel, offset, line.size(), font);
      drawTextRange(painter, paintedLabel, offset, line.size(), font,
                    QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                    rawWidth > 0.0 ? measuredLine.width / rawWidth : 1.0);
    } else {
      qsizetype cursor = offset;
      qsizetype runIndex = 0;
      for (qsizetype i = 0; i < mathSpans.size(); ++i) {
        const qsizetype textLength = mathSpans.at(i).start - cursor;
        const qreal rawTextWidth = measureTextRange(paintedLabel, cursor, textLength, font);
        const FlowLabelVisualRun* textRun = nullptr;
        if (textLength > 0 && runIndex < measuredLine.runs.size() &&
            !measuredLine.runs.at(runIndex).math)
          textRun = &measuredLine.runs.at(runIndex++);
        const qreal textWidth = textRun ? textRun->width : 0.0;
        drawTextRange(painter, paintedLabel, cursor, textLength, font,
                      QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                      rawTextWidth > 0.0 ? textWidth / rawTextWidth : 1.0);
        x += textWidth;
        const FlowLabelVisualRun* mathRun = nullptr;
        if (runIndex < measuredLine.runs.size() && measuredLine.runs.at(runIndex).math)
          mathRun = &measuredLine.runs.at(runIndex++);
        const auto& mathLayout = mathLayouts.at(static_cast<size_t>(i));
        if (mathLayout.valid()) {
          const auto& inlineMetrics = mathMetrics.at(static_cast<size_t>(i));
          const bool radical = paintedLabel.sequenceMathMlModel &&
                               inlineMetrics.structure == FlowLabelMathStructure::Radical;
          const muffin::math::MathCssBox cssBox = paintedLabel.sequenceMathMlModel
              ? muffin::math::layoutMathMlCssBox(mathLayout, fontPixelSize * 1.21, 16.0)
              : muffin::math::MathCssBox{};
          const QRectF inkBounds = radical && mathLayout.root
              ? mathLayout.root->boundsAt(QPointF(0.0, mathLayout.baseline)) : QRectF{};
          const qreal scaleX = radical && inkBounds.width() > 0.0
              ? inlineMetrics.visualWidth / inkBounds.width()
              : mathRun ? mathRun->width / mathLayout.naturalSize.width() : 1.0;
          const qreal scaleY = radical && inkBounds.height() > 0.0
              ? inlineMetrics.height / inkBounds.height()
              : inlineMetrics.height / mathLayout.naturalSize.height();
          const qreal targetTop = lineTop + measuredLine.baseline -
                                  mathLayout.baseline * scaleY;
          bool paintedAsMathMlAssembly = false;
          if (paintedLabel.sequenceMathMlModel) {
            painter.save();
            painter.translate(x, lineTop + measuredLine.baseline - cssBox.baseline);
            paintedAsMathMlAssembly = paintMathMlOperations(
                painter, mathLayout, cssBox, color, fontPixelSize * 1.21);
            painter.restore();
          }
          if (paintedAsMathMlAssembly) {
            x = runIndex < measuredLine.runs.size()
                ? rect.left() + (rect.width() - lineWidth) / 2.0 + measuredLine.runs.at(runIndex).x
                : x + inlineMetrics.advance;
            cursor = mathSpans.at(i).start + mathSpans.at(i).length;
            continue;
          }
          painter.save();
          painter.translate(x - (radical ? inkBounds.left() * scaleX : 0.0),
                            targetTop - (radical ? inkBounds.top() * scaleY : 0.0));
          painter.scale(scaleX, scaleY);
          mathLayout.paint(painter, QPointF());
          painter.restore();
          x = runIndex < measuredLine.runs.size()
              ? rect.left() + (rect.width() - lineWidth) / 2.0 + measuredLine.runs.at(runIndex).x
              : x + inlineMetrics.advance;
        }
        cursor = mathSpans.at(i).start + mathSpans.at(i).length;
      }
      const qsizetype textLength = offset + line.size() - cursor;
      const qreal rawTextWidth = measureTextRange(paintedLabel, cursor, textLength, font);
      const FlowLabelVisualRun* textRun = nullptr;
      if (textLength > 0 && runIndex < measuredLine.runs.size() &&
          !measuredLine.runs.at(runIndex).math)
        textRun = &measuredLine.runs.at(runIndex);
      drawTextRange(painter, paintedLabel, cursor, textLength, font,
                    QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                    rawTextWidth > 0.0 && textRun ? textRun->width / rawTextWidth : 1.0);
    }
    lineTop += actualLineHeight;
    offset += line.size() + 1;
  }
}

}  // namespace muffin::mermaid::flowchart
