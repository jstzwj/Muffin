#include "mermaid/math/MathMlCssLayout.h"
#include "math/OpenTypeMathFont.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace muffin::math {
namespace {

constexpr qreal kChromiumMathAxisOffsetPx = 6.0;
constexpr qreal kMathTableRowHeightEm = 1.22265625;
constexpr qreal kMathTableDescenderExpansionEm = 0.25;
constexpr qreal kMathTableExtraColumnCorrectionPx = 1.8;
constexpr qreal kMathTableCellVerticalPaddingEm = 0.47265625;

qreal snapEighth(qreal value) {
  return std::round(value * 8.0) / 8.0;
}

qreal snapLayoutUnit(qreal value) {
  return std::round(value * 64.0) / 64.0;
}

qreal mathStyleScale(const MathRenderNode* node) {
  if (!node) return 1.0;
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  return node->mathStyleSize >= 3
      ? constants.scriptScriptPercentScaleDown
      : node->mathStyleSize >= 2 ? constants.scriptPercentScaleDown : 1.0;
}

QRectF snapVerticalLayoutRect(QRectF rect) {
  const qreal top = snapLayoutUnit(rect.top());
  const qreal height = snapLayoutUnit(rect.height());
  rect.setY(top);
  rect.setHeight(height);
  return rect;
}

const MathRenderNode* primarySemanticNode(const MathRenderNode* node) {
  if (node == nullptr) return nullptr;
  if (node->semanticKind != MathSemanticKind::None) return node;
  for (const auto& child : node->children)
    if (const auto* result = primarySemanticNode(child.get())) return result;
  return nullptr;
}

qreal widestDescendant(const MathRenderNode* node, MathRenderKind kind) {
  if (node == nullptr) return 0.0;
  qreal width = node->kind == kind ? node->width : 0.0;
  for (const auto& child : node->children)
    width = std::max(width, widestDescendant(child.get(), kind));
  return width;
}

const MathRenderNode* directChild(const MathRenderNode* node, MathRenderKind kind) {
  if (node == nullptr) return nullptr;
  for (const auto& child : node->children)
    if (child && child->kind == kind) return child.get();
  return nullptr;
}

bool hasNestedSemantic(const MathRenderNode* node, MathSemanticKind kind) {
  if (node == nullptr) return false;
  for (const auto& child : node->children) {
    if (!child) continue;
    if (child->semanticKind == kind || hasNestedSemantic(child.get(), kind)) return true;
  }
  return false;
}

const MathRenderNode* nestedSemanticNode(const MathRenderNode* node,
                                         MathSemanticKind kind) {
  if (node == nullptr) return nullptr;
  for (const auto& child : node->children) {
    if (!child) continue;
    if (child->semanticKind == kind) return child.get();
    if (const auto* result = nestedSemanticNode(child.get(), kind)) return result;
  }
  return nullptr;
}

qreal cssNodeWidth(const MathRenderNode* node, qreal scale);
const MathRenderNode* radicalBody(const MathRenderNode* radical);
qreal cssNodeHeight(const MathRenderNode* node, qreal scale);
std::optional<MathCssScriptOperation> buildScriptOperation(
    const MathRenderNode* script, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale);
qreal fractionMathMlHeight(const MathRenderNode* fraction, qreal cssFontSize,
                           qreal renderScale);
qreal arrayCssHeight(const MathRenderNode* array, qreal scale);
const MathRenderNode* singleSymbol(const MathRenderNode* node);

QString mathDelimiterCharacter(const QString& delimiter) {
  if (delimiter == QLatin1String("\\lbrace")) return QStringLiteral("{");
  if (delimiter == QLatin1String("\\rbrace")) return QStringLiteral("}");
  return delimiter;
}

qreal arrayDelimiterWidth(const QString& delimiter, qreal targetHeight) {
  if (delimiter.isEmpty() || delimiter == QLatin1String(".")) return 0.0;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QString character = mathDelimiterCharacter(delimiter);
  const auto assembly = font.verticalAssembly(character, targetHeight);
  const auto fixed = font.verticalVariant(
      character, std::numeric_limits<qreal>::max());
  if (assembly && fixed) {
    if (character == QLatin1String("{") || character == QLatin1String("}"))
      return std::max(assembly->advance, fixed->advance);
    return fixed->advance;
  }
  if (assembly) return assembly->advance;
  if (fixed) return fixed->advance;
  const auto base = font.glyph(character);
  return base ? base->advance : 0.0;
}

const MathRenderNode* arrayTableBody(const MathRenderNode* node, int columns) {
  if (node == nullptr) return nullptr;
  if (node->kind == MathRenderKind::VList &&
      static_cast<int>(node->children.size()) == columns && columns > 0)
    return node;
  for (const auto& child : node->children)
    if (const MathRenderNode* body = arrayTableBody(child.get(), columns)) return body;
  return nullptr;
}

qreal arrayCssWidth(const MathRenderNode* array, qreal scale) {
  qreal width = 0.0;
  const qreal cssFontSize = OpenTypeMathFont::instance().pixelSize();
  const MathRenderNode* table = arrayTableBody(array, array->columns);
  for (int column = 0; column < array->columns; ++column) {
    qreal columnWidth = column < static_cast<int>(array->arrayColumnWidths.size())
        ? array->arrayColumnWidths[static_cast<size_t>(column)] * scale : 0.0;
    if (table && column < static_cast<int>(table->children.size())) {
      columnWidth = cssNodeWidth(table->children[static_cast<size_t>(column)].get(), scale);
    } else {
      columnWidth = std::ceil(columnWidth);
    }
    width += columnWidth + cssFontSize * 0.8;
  }
  if (!table)
    width -= std::max(0, array->columns - 2) * kMathTableExtraColumnCorrectionPx;
  const qreal targetHeight = arrayCssHeight(array, scale);
  width += arrayDelimiterWidth(array->arrayLeftDelimiter, targetHeight);
  width += arrayDelimiterWidth(array->arrayRightDelimiter, targetHeight);
  return std::floor(width * 16.0) / 16.0;
}

const MathRenderNode* columnRows(const MathRenderNode* node, int rows) {
  if (node == nullptr) return nullptr;
  if (node->kind == MathRenderKind::VList &&
      static_cast<int>(node->children.size()) == rows && rows > 0)
    return node;
  for (const auto& child : node->children)
    if (const MathRenderNode* result = columnRows(child.get(), rows)) return result;
  return nullptr;
}

const MathRenderNode* arrayCellNode(const MathRenderNode* array,
                                    const MathRenderNode* table,
                                    int row, int column) {
  if (!array || !table || row < 0 || column < 0 ||
      column >= static_cast<int>(table->children.size()))
    return nullptr;
  const MathRenderNode* rows = columnRows(
      table->children[static_cast<size_t>(column)].get(), array->rows);
  if (!rows || row >= static_cast<int>(rows->children.size())) return nullptr;
  const MathRenderNode* cell = rows->children[static_cast<size_t>(row)].get();
  if ((array->arrayEnvironment == QLatin1String("cases") ||
       array->arrayEnvironment == QLatin1String("aligned")) &&
      cell && cell->children.size() == 1)
    cell = cell->children.front().get();
  return cell;
}

bool hasPlainLatinDescender(const MathRenderNode* node) {
  if (node == nullptr || primarySemanticNode(node)) return false;
  if (node->kind == MathRenderKind::Symbol) {
    for (QChar character : node->text)
      if (QStringLiteral("gjpqy").contains(character, Qt::CaseInsensitive)) return true;
  }
  for (const auto& child : node->children)
    if (hasPlainLatinDescender(child.get())) return true;
  return false;
}

bool hasMathMlOperatorOverflow(const MathRenderNode* node) {
  if (!node) return false;
  if (node->kind == MathRenderKind::Symbol) {
    if (node->text == QLatin1String("+")) return true;
    if (node->atomClass == QLatin1String("mrel") && node->text.size() == 1 &&
        node->text.front().unicode() > 0x7f)
      return true;
  }
  for (const auto& child : node->children)
    if (hasMathMlOperatorOverflow(child.get())) return true;
  return false;
}

bool hasOperatorKind(const MathRenderNode* node, MathOperatorKind kind) {
  if (!node) return false;
  if (node->operatorKind == kind) return true;
  for (const auto& child : node->children)
    if (hasOperatorKind(child.get(), kind)) return true;
  return false;
}

QVector<qreal> arrayCssRowHeights(const MathRenderNode* array,
                                  const MathRenderNode* table,
                                  qreal scale) {
  QVector<qreal> heights;
  if (!array || !table || array->rows <= 0) return heights;
  heights.resize(array->rows);
  const bool fixedLineRows = array->arrayEnvironment == QLatin1String("cases") ||
                             array->arrayEnvironment == QLatin1String("aligned");
  const qreal cssFontSize = OpenTypeMathFont::instance().pixelSize();
  for (int row = 0; row < array->rows; ++row) {
    qreal contentHeight = 0.0;
    bool plainDescender = false;
    bool operatorOverflow = false;
    bool scriptOverflow = false;
    for (int column = 0; column < array->columns; ++column) {
      const MathRenderNode* cell = arrayCellNode(array, table, row, column);
      qreal currentHeight = cssNodeHeight(cell, scale);
      if (const MathRenderNode* semantic = primarySemanticNode(cell);
          semantic && semantic->semanticKind == MathSemanticKind::Fraction &&
          semantic->fractionStyleSize > 0) {
        currentHeight += std::floor(
            OpenTypeMathFont::instance().constants().fractionRuleThickness *
            semantic->fractionSizeMultiplier);
      }
      contentHeight = std::max(contentHeight, currentHeight);
      plainDescender = plainDescender || hasPlainLatinDescender(cell);
      operatorOverflow = operatorOverflow || hasMathMlOperatorOverflow(cell);
      scriptOverflow = scriptOverflow ||
          hasNestedSemantic(cell, MathSemanticKind::SupSub);
    }
    if (fixedLineRows) {
      const MathFontConstants& constants =
          OpenTypeMathFont::instance().constants();
      qreal rowHeight = cssFontSize * kMathTableRowHeightEm;
      if (operatorOverflow) rowHeight += constants.fractionRuleThickness;
      if (scriptOverflow)
        rowHeight += constants.spaceAfterScript +
                     constants.fractionRuleThickness;
      heights[row] = rowHeight;
    } else {
      heights[row] = contentHeight +
          cssFontSize * (kMathTableCellVerticalPaddingEm +
                         (plainDescender ? kMathTableDescenderExpansionEm
                                         : 0.0));
    }
  }
  return heights;
}

qreal arrayCssHeight(const MathRenderNode* array, qreal scale) {
  const MathRenderNode* table = arrayTableBody(array, array->columns);
  if (!table) return 0.0;
  qreal height = 0.0;
  for (qreal rowHeight : arrayCssRowHeights(array, table, scale))
    height += rowHeight;
  return height;
}

qreal fractionCssWidth(const MathRenderNode* fraction, qreal scale) {
  const MathRenderNode* body = directChild(fraction, MathRenderKind::VList);
  const qreal contentWidth = body ? cssNodeWidth(body, scale)
                                  : widestDescendant(fraction, MathRenderKind::VList) * scale;
  qreal width = std::max<qreal>(0.0, contentWidth + 2.0);
  const qreal targetHeight = (fraction->height + fraction->depth) * scale;
  width += arrayDelimiterWidth(fraction->leftDelimiter, targetHeight);
  width += arrayDelimiterWidth(fraction->rightDelimiter, targetHeight);
  return width;
}

qreal fractionDelimiterExtent(const MathRenderNode* fraction, qreal minimumExtent) {
  if (!fraction) return {};
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  qreal extent = 0.0;
  for (const QString& delimiter : {fraction->leftDelimiter, fraction->rightDelimiter}) {
    if (delimiter.isEmpty() || delimiter == QLatin1String(".")) continue;
    const auto variant = font.verticalVariant(
        mathDelimiterCharacter(delimiter), minimumExtent);
    if (variant) extent = std::max(extent, variant->extent);
  }
  return extent;
}

qreal radicalCssWidth(const MathRenderNode* radical, qreal scale) {
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const MathFontConstants& constants = font.constants();
  const MathRenderNode* radicalBox = radical->radicalIndex
      ? nestedSemanticNode(radical, MathSemanticKind::Radical) : radical;
  const auto widthVariant = font.verticalVariant(
      QString(QChar(0x221A)), font.pixelSize() * 1.875);
  const qreal styleScale = mathStyleScale(radical);
  const qreal radicalWidth = widthVariant
      ? (widthVariant->advance + constants.radicalRuleThickness / 2.0) *
            styleScale
      : font.pixelSize() * styleScale;
  return radicalWidth + cssNodeWidth(radicalBody(radicalBox), scale) +
         (radical->radicalIndex ? constants.radicalKernBeforeDegree : 0.0);
}

int symbolCount(const MathRenderNode* node) {
  if (node == nullptr) return 0;
  int count = node->kind == MathRenderKind::Symbol ? 1 : 0;
  for (const auto& child : node->children) count += symbolCount(child.get());
  return count;
}

bool hasAtomClass(const MathRenderNode* node, QLatin1StringView atomClass) {
  if (node == nullptr) return false;
  if (node->atomClass == atomClass) return true;
  for (const auto& child : node->children)
    if (hasAtomClass(child.get(), atomClass)) return true;
  return false;
}

qreal maxLargeOperatorExtent(const MathRenderNode* node) {
  if (node == nullptr) return 0.0;
  qreal extent = 0.0;
  if (node->kind == MathRenderKind::Symbol && node->atomClass == QLatin1String("mop")) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto variant = font.verticalVariant(node->text,
                                              font.constants().displayOperatorMinHeight);
    if (variant) extent = variant->extent;
  }
  for (const auto& child : node->children)
    extent = std::max(extent, maxLargeOperatorExtent(child.get()));
  return extent;
}

const MathRenderNode* firstKind(const MathRenderNode* node, MathRenderKind kind) {
  if (node == nullptr) return nullptr;
  if (node->kind == kind) return node;
  for (const auto& child : node->children)
    if (const MathRenderNode* found = firstKind(child.get(), kind)) return found;
  return nullptr;
}

bool containsNode(const MathRenderNode* root, const MathRenderNode* target) {
  if (!root || !target) return false;
  if (root == target) return true;
  for (const auto& child : root->children)
    if (containsNode(child.get(), target)) return true;
  return false;
}

const MathRenderNode* enclosingKind(const MathRenderNode* node,
                                    const MathRenderNode* target,
                                    MathRenderKind kind) {
  if (!node || !containsNode(node, target)) return nullptr;
  if (node->kind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* enclosing = enclosingKind(child.get(), target, kind))
      return enclosing;
  return nullptr;
}

const MathRenderNode* singleSymbol(const MathRenderNode* node) {
  if (node == nullptr || symbolCount(node) != 1) return nullptr;
  if (node->kind == MathRenderKind::Symbol) return node;
  for (const auto& child : node->children) {
    const MathRenderNode* candidate = singleSymbol(child.get());
    if (candidate != nullptr) return candidate;
  }
  return nullptr;
}

struct NativeGlyphBox {
  qreal width = 0.0;
  qreal height = 0.0;
};

struct GlyphInkExtents {
  qreal top = 0.0;
  qreal bottom = 0.0;
};

std::optional<NativeGlyphBox> nativeGlyphBox(const MathRenderNode* symbol);

struct FractionMetrics {
  const MathRenderNode* numerator = nullptr;
  const MathRenderNode* denominator = nullptr;
  GlyphInkExtents numeratorInk;
  GlyphInkExtents denominatorInk;
  qreal numeratorShift = 0.0;
  qreal denominatorShift = 0.0;
  qreal ruleThickness = 0.0;
  GlyphInkExtents extents;
};

GlyphInkExtents fractionMathMlExtents(const MathRenderNode* fraction,
                                      qreal cssFontSize,
                                      qreal renderScale);
std::optional<FractionMetrics> fractionMetrics(const MathRenderNode* fraction,
                                               qreal cssFontSize,
                                               qreal renderScale);
qreal braceAccentCssHeight(const MathRenderNode* container,
                           const MathRenderNode* accent,
                           qreal scale);

GlyphInkExtents glyphInkExtents(const MathRenderNode* node, qreal fontScale) {
  GlyphInkExtents result;
  if (node == nullptr) return result;
  if (node->semanticKind == MathSemanticKind::SupSub && node->children.size() == 2) {
    const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
    const qreal baseScale = node->mathStyleSize >= 3
        ? constants.scriptScriptPercentScaleDown
        : node->mathStyleSize >= 2 ? constants.scriptPercentScaleDown : 1.0;
    const GlyphInkExtents base = glyphInkExtents(node->children.front().get(), baseScale);
    const MathRenderNode* scripts = node->children.back().get();
    GlyphInkExtents superscript;
    GlyphInkExtents subscript;
    if (node->scriptKind == MathScriptKind::SubSup && scripts->children.size() >= 2) {
      subscript = glyphInkExtents(scripts->children.front().get(), baseScale);
      superscript = glyphInkExtents(scripts->children.back().get(), baseScale);
    } else if (node->scriptKind == MathScriptKind::Subscript) {
      subscript = glyphInkExtents(scripts, baseScale);
    } else {
      superscript = glyphInkExtents(scripts, baseScale);
    }
    const qreal supShift = constants.superscriptShiftUp * baseScale;
    qreal subShift = constants.subscriptShiftDown * baseScale;
    if (node->scriptKind == MathScriptKind::SubSup) {
      const qreal gap = (supShift - superscript.bottom) -
                        (subscript.top - subShift);
      subShift += std::max<qreal>(
          0.0, constants.subSuperscriptGapMin * baseScale - gap);
    }
    result.top = node->scriptKind == MathScriptKind::Subscript
        ? base.top : std::max(base.top, supShift + superscript.top);
    result.bottom = node->scriptKind == MathScriptKind::Superscript
        ? base.bottom : std::max(base.bottom, subShift + subscript.bottom);
    return result;
  }
  if (node->semanticKind == MathSemanticKind::Fraction)
    return fractionMathMlExtents(
        node, OpenTypeMathFont::instance().pixelSize(), fontScale);
  if (node->semanticKind == MathSemanticKind::Radical) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const MathFontConstants& constants = font.constants();
    const MathRenderNode* radicalBox = node->radicalIndex
        ? nestedSemanticNode(node, MathSemanticKind::Radical) : node;
    const MathRenderNode* body = radicalBody(radicalBox);
    if (!body) return result;
    const qreal styleScale = mathStyleScale(node);
    const qreal radicalGap = constants.radicalVerticalGap * styleScale;
    const qreal radicalRule = constants.radicalRuleThickness * styleScale;
    const qreal radicalExtra = constants.radicalExtraAscender * styleScale;
    const GlyphInkExtents bodyInk = glyphInkExtents(body, styleScale);
    qreal bodyHeight = primarySemanticNode(body)
        ? cssNodeHeight(body, styleScale)
        : std::ceil(bodyInk.top + bodyInk.bottom);
    if (const auto glyph = nativeGlyphBox(singleSymbol(body)))
      bodyHeight = std::round(glyph->height * styleScale);
    else if (!primarySemanticNode(body) && symbolCount(body) > 1)
      bodyHeight += 1.0;
    const qreal target = std::max(bodyInk.top + bodyInk.bottom, bodyHeight) +
                         radicalGap + radicalRule;
    const auto variant = font.verticalVariant(
        QString(QChar(0x221A)), target / styleScale);
    if (!variant) return bodyInk;
    const qreal bodyAscent = std::min(bodyHeight, bodyInk.top);
    result.top = std::max(
        bodyAscent, bodyInk.top + radicalGap + radicalRule + radicalExtra);
    result.bottom = std::max(
        bodyHeight - bodyAscent,
        variant->extent * styleScale + radicalExtra - result.top);
    return result;
  }
  if (node->kind == MathRenderKind::Symbol && node->text.size() == 1) {
    const QChar character = node->text.front();
    const bool italic = character.isLetter() &&
        (node->fontClass == QLatin1String("mathnormal") ||
         node->fontClass == QLatin1String("mathit"));
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    if (node->mathStyleSize >= 3)
      fontScale = font.constants().scriptScriptPercentScaleDown;
    else if (node->mathStyleSize >= 2)
      fontScale = font.constants().scriptPercentScaleDown;
    else if (!node->tightSpacing)
      fontScale = 1.0;
    const auto glyph = italic ? font.mathItalicGlyph(character) : font.glyph(node->text);
    if (glyph) {
      const auto pixelRound = [fontScale](qreal value) {
        return fontScale < 0.999 ? std::round(value * fontScale)
                                : std::ceil(value * fontScale);
      };
      const bool latinDescender = QStringLiteral("gjpqy").contains(
          character, Qt::CaseInsensitive);
      const bool nonLatinLetter = character.isLetter() &&
                                  character.script() != QChar::Script_Latin;
      const bool usesInkDescent = latinDescender || nonLatinLetter;
      result.top = fontScale < 0.999 && !usesInkDescent
          ? std::round(glyph->inkBounds.height() * fontScale)
          : pixelRound(std::max<qreal>(0.0, -glyph->inkBounds.top()));
      if (nonLatinLetter) {
        const qreal total = std::round(glyph->inkBounds.height() * fontScale);
        result.bottom = std::max<qreal>(0.0, total - result.top);
      } else if (latinDescender) {
        result.bottom = pixelRound(std::max<qreal>(0.0, glyph->inkBounds.bottom()));
      }
      return result;
    }
  }
  for (const auto& child : node->children) {
    const GlyphInkExtents extents = glyphInkExtents(child.get(), fontScale);
    result.top = std::max(result.top, extents.top);
    result.bottom = std::max(result.bottom, extents.bottom);
  }
  return result;
}

std::optional<NativeGlyphBox> nativeGlyphBox(const MathRenderNode* symbol) {
  if (symbol == nullptr || symbol->text.size() != 1) return std::nullopt;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QChar character = symbol->text.front();
  const bool italicMathIdentifier = character.isLetter() &&
      (symbol->fontClass == QLatin1String("mathnormal") ||
       symbol->fontClass == QLatin1String("mathit"));
  const auto glyph = italicMathIdentifier ? font.mathItalicGlyph(character)
                                          : font.glyph(symbol->text);
  if (!glyph) return std::nullopt;
  const qreal styleScale = symbol->mathStyleSize >= 3
      ? font.constants().scriptScriptPercentScaleDown
      : symbol->mathStyleSize >= 2 || symbol->tightSpacing
          ? font.constants().scriptPercentScaleDown : 1.0;
  qreal width = glyph->advance * styleScale;
  if (symbol->atomClass == QLatin1String("mop")) {
    const auto variant = font.verticalVariant(
        symbol->text, font.constants().displayOperatorMinHeight);
    if (variant) width = variant->advance * styleScale;
  }
  if (symbol->atomClass == QLatin1String("mbin") || symbol->text == QLatin1String("+"))
    width += 2.0 * 4.0 / 18.0 * font.pixelSize() * styleScale;
  else if (symbol->atomClass == QLatin1String("mrel"))
    width += 2.0 * 5.0 / 18.0 * font.pixelSize() * styleScale;
  else if (symbol->atomClass == QLatin1String("mop"))
    width += 2.0 * 3.0 / 18.0 * font.pixelSize() * styleScale;
  const bool binaryOperator = symbol->atomClass == QLatin1String("mbin") ||
                              symbol->text == QLatin1String("+");
  const bool usesInkDescent = (character.isLetter() &&
                               character.script() != QChar::Script_Latin) ||
      QStringLiteral("gjpqy").contains(character, Qt::CaseInsensitive);
  qreal height = std::ceil(std::max<qreal>(0.0, -glyph->inkBounds.top()) +
                           (usesInkDescent
                                ? std::max<qreal>(0.0, glyph->inkBounds.bottom())
                                : 0.0));
  if (binaryOperator) height += 2.0;
  return NativeGlyphBox{width, height};
}

qreal cssNodeWidth(const MathRenderNode* node, qreal scale) {
  if (node == nullptr) return 0.0;
  if (node->kind == MathRenderKind::Rule || node->text == QLatin1String("nulldelimiter"))
    return 0.0;
  if (node->atomClass == QLatin1String("mspace") && node->text == QLatin1String("glue"))
    return 0.0;
  if (node->operatorKind == MathOperatorKind::Limits) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const bool symbolOperator = hasOperatorKind(node, MathOperatorKind::Symbol);
    const qreal operatorSpacing = symbolOperator
        ? 2.0 * 3.0 / 18.0 * font.pixelSize() : 0.0;
    qreal width = 0.0;
    for (const auto& child : node->children) {
      qreal childWidth = cssNodeWidth(child.get(), scale);
      if (hasOperatorKind(child.get(), MathOperatorKind::Symbol))
        childWidth -= operatorSpacing;
      width = std::max(width, childWidth);
    }
    return width - font.constants().spaceAfterScript / 2.0 +
           operatorSpacing;
  }
  if (node->kind == MathRenderKind::Accent &&
      node->accentKind != MathAccentKind::None && !node->children.empty()) {
    const bool bodyIsLast = node->accentKind == MathAccentKind::Under ||
                            node->accentKind == MathAccentKind::OverBrace;
    const MathRenderNode* body = bodyIsLast ? node->children.back().get()
                                            : node->children.front().get();
    return cssNodeWidth(body, scale);
  }
  if (node->semanticKind == MathSemanticKind::SupSub) {
    if (const MathRenderNode* brace = firstKind(node, MathRenderKind::Accent);
        brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                  brace->accentKind == MathAccentKind::OverBrace))
      return cssNodeWidth(brace, scale);
  }
  if (node->kind == MathRenderKind::LeftRight) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const qreal targetHeight = std::max(
        (node->height + node->depth) * scale,
        font.pixelSize() * kMathTableRowHeightEm);
    qreal width = arrayDelimiterWidth(node->leftDelimiter, targetHeight) +
                  arrayDelimiterWidth(node->rightDelimiter, targetHeight);
    size_t firstBody = node->leftDelimiter == QLatin1String(".") ? 0 : 1;
    size_t bodyEnd = node->children.size() -
                     (node->rightDelimiter == QLatin1String(".") ? 0 : 1);
    for (size_t i = firstBody; i < bodyEnd; ++i)
      width += cssNodeWidth(node->children[i].get(), scale);
    return width;
  }
  if (node->semanticKind == MathSemanticKind::Fraction)
    return fractionCssWidth(node, scale);
  if (node->semanticKind == MathSemanticKind::Radical)
    return radicalCssWidth(node, scale);
  if (node->semanticKind == MathSemanticKind::Array)
    return arrayCssWidth(node, scale);
  if (node->semanticKind == MathSemanticKind::SupSub && node->children.size() == 2) {
    const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
    const qreal nodeStyleScale = node->mathStyleSize >= 3
        ? constants.scriptScriptPercentScaleDown
        : node->mathStyleSize >= 2 ? constants.scriptPercentScaleDown : 1.0;
    const qreal base = cssNodeWidth(node->children.front().get(), scale);
    const qreal scripts = cssNodeWidth(node->children.back().get(), scale);
    qreal width = snapEighth(base + scripts + constants.spaceAfterScript * nodeStyleScale);
    return width;
  }
  if (node->kind == MathRenderKind::Symbol) {
    if (const auto glyph = nativeGlyphBox(node)) return glyph->width;
    return std::round(node->width * scale);
  }
  if (node->children.empty()) return node->width * scale;
  if (node->kind == MathRenderKind::VList || node->kind == MathRenderKind::Accent) {
    qreal width = 0.0;
    for (const auto& child : node->children)
      width = std::max(width, cssNodeWidth(child.get(), scale));
    return width;
  }
  qreal width = 0.0;
  for (const auto& child : node->children) width += cssNodeWidth(child.get(), scale);
  return width;
}

qreal cssNodeHeight(const MathRenderNode* node, qreal scale) {
  if (node == nullptr) return 0.0;
  const MathRenderNode* semantic = primarySemanticNode(node);
  if (semantic && semantic != node) return cssNodeHeight(semantic, scale);
  if (node->semanticKind == MathSemanticKind::SupSub) {
    const GlyphInkExtents extents = glyphInkExtents(node, 1.0);
    qreal height = extents.top + extents.bottom;
    if (hasNestedSemantic(node, MathSemanticKind::Fraction) ||
        hasNestedSemantic(node, MathSemanticKind::Radical)) {
      if (const auto operation = buildScriptOperation(
              node, node, QRectF(0.0, 0.0, cssNodeWidth(node, scale), 0.0),
              scale)) {
        qreal top = 0.0;
        qreal bottom = 0.0;
        for (const QRectF component : {operation->base,
                                       operation->superscript,
                                       operation->subscript}) {
          if (component.isEmpty()) continue;
          top = std::min(top, component.top());
          bottom = std::max(bottom, component.bottom());
        }
        height = std::max(height, std::ceil(bottom - top));
      }
    }
    return height;
  }
  if (node->semanticKind == MathSemanticKind::Fraction) {
    const qreal height = fractionMathMlHeight(
        node, OpenTypeMathFont::instance().pixelSize(), scale);
    if (height > 0.0) return height;
    return std::ceil((node->height + node->depth) * scale);
  }
  if (node->semanticKind == MathSemanticKind::Radical) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const MathFontConstants& constants = font.constants();
    const qreal styleScale = mathStyleScale(node);
    const MathRenderNode* body = radicalBody(node->radicalIndex
        ? nestedSemanticNode(node, MathSemanticKind::Radical) : node);
    const qreal required = cssNodeHeight(body, scale) +
                           (constants.radicalVerticalGap +
                            constants.radicalRuleThickness) * styleScale;
    const auto variant = font.verticalVariant(
        QString(QChar(0x221A)), required / styleScale);
    if (!variant) return std::ceil((node->height + node->depth) * scale);
    return variant->extent * styleScale +
           constants.radicalExtraAscender * styleScale;
  }
  if (node->semanticKind == MathSemanticKind::Array) {
    const qreal height = arrayCssHeight(node, scale);
    if (height > 0.0) return height;
  }
  if (node->kind == MathRenderKind::Symbol) {
    if (const auto glyph = nativeGlyphBox(node)) return glyph->height;
  }
  if (symbolCount(node) == 1) {
    if (const auto glyph = nativeGlyphBox(singleSymbol(node))) return glyph->height;
  }
  return std::ceil((node->height + node->depth) * scale) +
         (hasAtomClass(node, QLatin1StringView("mbin")) ? 1.0 : 0.0);
}

std::optional<qreal> cssNodeOffset(const MathRenderNode* node,
                                   const MathRenderNode* target,
                                   qreal scale) {
  if (!node || !target) return std::nullopt;
  if (node == target) return 0.0;
  qreal sequentialOffset = 0.0;
  const bool sequential = node->kind == MathRenderKind::Span;
  for (const auto& child : node->children) {
    if (!child) continue;
    if (const auto nested = cssNodeOffset(child.get(), target, scale)) {
      const qreal localOffset = sequential
          ? sequentialOffset
          : child->xOffset * scale;
      return localOffset + *nested;
    }
    if (sequential) sequentialOffset += cssNodeWidth(child.get(), scale);
  }
  return std::nullopt;
}

qreal fractionMathMlHeight(const MathRenderNode* fraction, qreal cssFontSize,
                           qreal renderScale) {
  const GlyphInkExtents extents = fractionMathMlExtents(
      fraction, cssFontSize, renderScale);
  return extents.top + extents.bottom;
}

GlyphInkExtents fractionMathMlExtents(const MathRenderNode* fraction,
                                      qreal cssFontSize,
                                      qreal renderScale) {
  const auto metrics = fractionMetrics(fraction, cssFontSize, renderScale);
  return metrics ? metrics->extents : GlyphInkExtents{};
}

std::optional<FractionMetrics> fractionMetrics(const MathRenderNode* fraction,
                                               qreal cssFontSize,
                                               qreal renderScale) {
  if (!fraction) return {};
  const MathRenderNode* stack = directChild(fraction, MathRenderKind::VList);
  if (!stack) return {};
  const MathRenderNode* denominator = nullptr;
  const MathRenderNode* numerator = nullptr;
  for (const auto& child : stack->children) {
    if (!child || child->kind == MathRenderKind::Rule) continue;
    if (!denominator) denominator = child.get();
    numerator = child.get();
  }
  if (!numerator || !denominator || numerator == denominator) return std::nullopt;

  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const bool displayStyle = fraction->fractionStyleSize == 0;
  const qreal styleScale = fraction->fractionSizeMultiplier;
  GlyphInkExtents numeratorInk = glyphInkExtents(numerator, 1.0);
  GlyphInkExtents denominatorInk = glyphInkExtents(denominator, 1.0);
  const auto includeMathMlRowBox = [cssFontSize, renderScale, styleScale, &constants](
                                        const MathRenderNode* child,
                                        GlyphInkExtents* ink,
                                        bool includeOperatorLeading) {
    if (!child || !ink || symbolCount(child) <= 1) return;
    if (const MathRenderNode* nested = primarySemanticNode(child);
        nested && nested->semanticKind == MathSemanticKind::Fraction) {
      *ink = fractionMathMlExtents(nested, cssFontSize, renderScale);
      ink->top += std::floor(constants.fractionRuleThickness *
                             nested->fractionSizeMultiplier);
      return;
    }
    if (const MathRenderNode* nested = primarySemanticNode(child);
        nested && nested->semanticKind == MathSemanticKind::Radical) {
      if (hasNestedSemantic(nested, MathSemanticKind::Fraction) ||
          hasNestedSemantic(nested, MathSemanticKind::Radical) ||
          hasNestedSemantic(nested, MathSemanticKind::SupSub)) {
        const qreal radicalScale = mathStyleScale(nested);
        const qreal rowHeight = cssNodeHeight(nested, renderScale) -
            constants.radicalRuleThickness * radicalScale;
        ink->top = std::max<qreal>(0.0, rowHeight - ink->bottom);
      }
      return;
    }
    if (const MathRenderNode* nested = primarySemanticNode(child);
        nested && nested->semanticKind == MathSemanticKind::SupSub) {
      if (const MathRenderNode* brace = firstKind(nested, MathRenderKind::Accent);
          brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                    brace->accentKind == MathAccentKind::OverBrace)) {
        *ink = {braceAccentCssHeight(nested, brace, renderScale), 0.0};
        return;
      }
    }
    if (includeOperatorLeading && hasMathMlOperatorOverflow(child))
      ink->top += std::floor(constants.fractionRuleThickness * styleScale);
    if (firstKind(child, MathRenderKind::LeftRight))
      ink->top += std::floor(2.0 * constants.fractionRuleThickness * styleScale);
    if (hasNestedSemantic(child, MathSemanticKind::Fraction) ||
        hasNestedSemantic(child, MathSemanticKind::Radical) ||
        hasNestedSemantic(child, MathSemanticKind::Array)) {
      const qreal rowHeight = cssNodeHeight(child, renderScale);
      ink->top += std::max<qreal>(0.0, rowHeight - ink->top - ink->bottom);
    }
  };
  includeMathMlRowBox(numerator, &numeratorInk, true);
  includeMathMlRowBox(denominator, &denominatorInk, numeratorInk.bottom > 0.0);
  const bool hasScriptRow = hasNestedSemantic(numerator, MathSemanticKind::SupSub) ||
                            hasNestedSemantic(denominator, MathSemanticKind::SupSub);
  qreal topShift = styleScale * (displayStyle
      ? constants.stackTopDisplayStyleShiftUp : constants.stackTopShiftUp);
  qreal bottomShift = styleScale * (displayStyle
      ? constants.stackBottomDisplayStyleShiftDown : constants.stackBottomShiftDown);
  if (!fraction->fractionHasBarLine) {
    const qreal minimumGap = styleScale * (displayStyle
        ? constants.stackDisplayStyleGapMin : constants.stackGapMin);
    const qreal currentGap = topShift + bottomShift - numeratorInk.bottom -
                             denominatorInk.top;
    const qreal adjustment = std::max<qreal>(0.0, minimumGap - currentGap) / 2.0;
    topShift += adjustment;
    bottomShift += adjustment;
    FractionMetrics result;
    result.numerator = numerator;
    result.denominator = denominator;
    result.numeratorInk = numeratorInk;
    result.denominatorInk = denominatorInk;
    result.numeratorShift = topShift;
    result.denominatorShift = bottomShift;
    result.extents = {topShift + numeratorInk.top,
                      bottomShift + denominatorInk.bottom};
    const qreal snappedHeight = std::round(
        (result.extents.top + result.extents.bottom) * 2.0) / 2.0;
    result.extents.bottom += snappedHeight - result.extents.top -
                             result.extents.bottom;
    if (!displayStyle)
      result.extents.top += constants.fractionRuleThickness * styleScale;
    return result;
  }

  topShift = styleScale * (displayStyle
      ? constants.fractionNumeratorDisplayStyleShiftUp
      : constants.fractionNumeratorShiftUp);
  bottomShift = styleScale * (displayStyle
      ? constants.fractionDenominatorDisplayStyleShiftDown
      : constants.fractionDenominatorShiftDown);
  const qreal numeratorGap = styleScale * (displayStyle
      ? constants.fractionNumeratorDisplayStyleGapMin
      : constants.fractionNumeratorGapMin);
  const qreal denominatorGap = styleScale * (displayStyle
      ? constants.fractionDenominatorDisplayStyleGapMin
      : constants.fractionDenominatorGapMin);
  const qreal ruleThickness = fraction->fractionLineThicknessEm >= 0.0
      ? fraction->fractionLineThicknessEm * cssFontSize * styleScale
      : constants.fractionRuleThickness * styleScale;
  const qreal numeratorClearance = topShift - numeratorInk.bottom -
                                   constants.axisHeight * styleScale -
                                   ruleThickness / 2.0;
  topShift += std::max<qreal>(0.0, numeratorGap - numeratorClearance);
  const qreal denominatorClearance = constants.axisHeight * styleScale -
                                     ruleThickness / 2.0 -
                                     denominatorInk.top + bottomShift;
  bottomShift += std::max<qreal>(0.0, denominatorGap - denominatorClearance);
  FractionMetrics result;
  result.numerator = numerator;
  result.denominator = denominator;
  result.numeratorInk = numeratorInk;
  result.denominatorInk = denominatorInk;
  result.numeratorShift = topShift;
  result.denominatorShift = bottomShift;
  result.ruleThickness = ruleThickness;
  result.extents = {
      topShift + numeratorInk.top +
          (hasScriptRow ? constants.fractionRuleThickness * styleScale / 2.0 : 0.0),
      bottomShift + denominatorInk.bottom};
  return result;
}

qreal maxLeftRightFenceExtent(const MathRenderNode* node, qreal minimumExtent) {
  if (!node) return 0.0;
  qreal extent = 0.0;
  if (node->kind == MathRenderKind::LeftRight) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    for (const QString& delimiter : {node->leftDelimiter, node->rightDelimiter}) {
      if (delimiter.isEmpty() || delimiter == QLatin1String(".")) continue;
      const QString character = mathDelimiterCharacter(delimiter);
      const bool tallFence = minimumExtent > font.pixelSize() * kMathTableRowHeightEm;
      const qreal fenceLeading = 2.0 * std::ceil(
          font.constants().fractionRuleThickness);
      const auto largestFixed = font.verticalVariant(
          character, std::numeric_limits<qreal>::max());
      const bool requiresAssembly = largestFixed &&
          minimumExtent > largestFixed->extent;
      const MathRenderNode* fenceSemantic = primarySemanticNode(node);
      const bool fractionFence = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Fraction;
      const qreal targetExtent = minimumExtent + (requiresAssembly
          ? fractionFence
              ? 2.0 * font.constants().axisHeight +
                    font.constants().fractionRuleThickness / 2.0
              : font.constants().fractionRuleThickness / 2.0
          : tallFence ? fenceLeading : 0.0);
      const auto variant = font.verticalVariant(character, targetExtent, true);
      if (variant) {
        const bool assembled = largestFixed &&
            variant->extent > largestFixed->extent + 0.001;
        qreal visualExtent = variant->extent;
        if (tallFence && !assembled)
          visualExtent = std::round(visualExtent) + fenceLeading;
        extent = std::max(extent, visualExtent);
      }
    }
  }
  for (const auto& child : node->children)
    extent = std::max(extent, maxLeftRightFenceExtent(child.get(), minimumExtent));
  return extent;
}

qreal siblingSemanticHeight(const MathRenderNode* node,
                            const MathRenderNode* primary,
                            qreal scale) {
  if (!node || node == primary) return 0.0;
  if (node->semanticKind == MathSemanticKind::Fraction)
    return (node->height + node->depth) * scale -
           OpenTypeMathFont::instance().constants().fractionRuleThickness;
  if (node->semanticKind != MathSemanticKind::None)
    return cssNodeHeight(node, scale);
  qreal height = 0.0;
  for (const auto& child : node->children)
    height = std::max(height, siblingSemanticHeight(child.get(), primary, scale));
  return height;
}

qreal heightOutsideAccent(const MathRenderNode* node,
                          const MathRenderNode* accent,
                          qreal scale) {
  if (!node || node == accent) return 0.0;
  if (node->kind == MathRenderKind::Symbol) {
    const GlyphInkExtents ink = glyphInkExtents(node, 1.0);
    return ink.top + ink.bottom;
  }
  qreal height = 0.0;
  for (const auto& child : node->children)
    height = std::max(height, heightOutsideAccent(child.get(), accent, scale));
  return height;
}

qreal accentBodyCssHeight(const MathRenderNode* body, qreal scale) {
  if (!body) return 0.0;
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const MathRenderNode* semantic = primarySemanticNode(body);
  if (!semantic) {
    const GlyphInkExtents ink = glyphInkExtents(body, 1.0);
    qreal height = ink.top + ink.bottom;
    if (hasMathMlOperatorOverflow(body))
      height += std::floor(constants.fractionRuleThickness);
    return std::ceil(height);
  }

  qreal height = cssNodeHeight(body, scale);
  if (semantic->semanticKind == MathSemanticKind::Fraction &&
      semantic->fractionHasBarLine) {
    height -= constants.fractionRuleThickness *
              semantic->fractionSizeMultiplier;
  } else if (semantic->semanticKind == MathSemanticKind::Radical ||
             semantic->semanticKind == MathSemanticKind::Array) {
    // Embedded MathML blocks contribute their ink extent to mover/munder;
    // Chromium excludes the root inline-block extra ascender.
    height -= constants.radicalExtraAscender -
              constants.radicalRuleThickness / 2.0;
  }
  return height;
}

qreal braceAccentCssHeight(const MathRenderNode* container,
                           const MathRenderNode* accent,
                           qreal scale) {
  if (!container || !accent || accent->children.size() < 2) return 0.0;
  const bool over = accent->accentKind == MathAccentKind::OverBrace;
  const MathRenderNode* body = over ? accent->children.back().get()
                                    : accent->children.front().get();
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const qreal bodyHeight = accentBodyCssHeight(body, scale);
  const qreal gap = over ? constants.overbarVerticalGap
                         : constants.underbarVerticalGap;
  const qreal rule = over ? constants.overbarRuleThickness
                          : constants.underbarRuleThickness;
  const qreal braceHeight = std::round(gap + 2.0 * rule);
  const qreal extra = over ? constants.overbarExtraAscender
                           : constants.underbarExtraDescender;
  qreal height = bodyHeight + gap + braceHeight + extra;
  qreal annotationHeight = heightOutsideAccent(container, accent, scale);
  if (annotationHeight > 0.0)
    annotationHeight += std::floor(constants.fractionRuleThickness);
  if (annotationHeight > 0.0)
    height += gap + annotationHeight + extra;
  return height;
}

const MathRenderNode* radicalBody(const MathRenderNode* radical) {
  if (radical == nullptr) return nullptr;
  for (const auto& child : radical->children)
    if (child && child->kind != MathRenderKind::Stretchy) return child.get();
  return nullptr;
}

MathCssBoxRole roleFor(const MathRenderNode& node) {
  switch (node.semanticKind) {
    case MathSemanticKind::Fraction: return MathCssBoxRole::Fraction;
    case MathSemanticKind::Radical: return MathCssBoxRole::Radical;
    case MathSemanticKind::SupSub: return MathCssBoxRole::SupSub;
    case MathSemanticKind::Array: return MathCssBoxRole::Array;
    case MathSemanticKind::None: break;
  }
  return node.kind == MathRenderKind::Symbol ? MathCssBoxRole::Glyph
                                             : MathCssBoxRole::Row;
}

MathCssBox convertTree(const MathRenderNode& node, qreal scale) {
  MathCssBox box;
  box.role = roleFor(node);
  box.semanticKind = node.semanticKind;
  box.scriptKind = node.scriptKind;
  box.radicalIndex = node.radicalIndex;
  box.text = node.text;
  box.x = node.xOffset * scale;
  box.y = node.yOffset * scale;
  box.width = node.width * scale;
  box.height = (node.height + node.depth) * scale;
  box.advance = box.width;
  box.baseline = node.height * scale;
  box.inkTop = 0.0;
  box.inkBottom = box.height;
  box.children.reserve(static_cast<qsizetype>(node.children.size()));
  for (const auto& child : node.children)
    if (child) box.children.push_back(convertTree(*child, scale));
  return box;
}

}  // namespace

MathCssBox layoutMathMlCssBox(const MathLayoutResult& layout,
                              qreal renderFontPixelSize,
                              qreal cssRootFontPixelSize) {
  MathCssBox root;
  root.role = MathCssBoxRole::Root;
  if (!layout.valid() || renderFontPixelSize <= 0.0 || cssRootFontPixelSize <= 0.0)
    return root;

  const qreal scale = cssRootFontPixelSize / renderFontPixelSize;
  root.children.push_back(convertTree(*layout.root, scale));
  const MathRenderNode* semantic = primarySemanticNode(layout.root.get());
  root.semanticKind = semantic ? semantic->semanticKind : MathSemanticKind::None;
  root.scriptKind = semantic ? semantic->scriptKind : MathScriptKind::None;
  root.radicalIndex = semantic && semantic->radicalIndex;
  const qreal scaledWidth = layout.naturalSize.width() * scale;
  const qreal scaledHeight = layout.naturalSize.height() * scale;

  switch (root.semanticKind) {
    case MathSemanticKind::Fraction: {
      const qreal fractionHeight = fractionMathMlHeight(
          semantic, cssRootFontPixelSize, scale);
      if (fractionHeight > 0.0) {
        root.width = fractionCssWidth(semantic, scale);
        root.advance = root.width;
        root.height = fractionHeight;
      } else {
        root.width = fractionCssWidth(semantic, scale);
        root.height = std::ceil(layout.naturalSize.height() * scale);
      }
      root.advance = root.width;
      if (semantic && symbolCount(layout.root.get()) > symbolCount(semantic)) {
        root.width = cssNodeWidth(layout.root.get(), scale);
        root.advance = root.width;
      }
      if (enclosingKind(layout.root.get(), semantic, MathRenderKind::LeftRight)) {
        root.width = cssNodeWidth(layout.root.get(), scale);
        root.advance = root.width;
      }
      break;
    }
    case MathSemanticKind::Radical:
      {
      const OpenTypeMathFont& mathFont = OpenTypeMathFont::instance();
      const MathFontConstants& constants = mathFont.constants();
      const MathRenderNode* radicalBox = semantic->radicalIndex
          ? nestedSemanticNode(semantic, MathSemanticKind::Radical) : semantic;
      const MathRenderNode* body = radicalBody(radicalBox);
      const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
      const qreal radicalPadding = constants.radicalVerticalGap +
                                   constants.radicalRuleThickness;
      const qreal recursiveNaturalHeight =
          hasNestedSemantic(body, MathSemanticKind::SupSub) ||
                  hasNestedSemantic(body, MathSemanticKind::Radical)
              ? scaledHeight
              : 0.0;
      const qreal requiredExtent = std::max({
          bodyInk.top + bodyInk.bottom + radicalPadding,
          cssNodeHeight(body, scale) + radicalPadding,
          root.height, recursiveNaturalHeight});
      const auto heightVariant = mathFont.verticalVariant(QString(QChar(0x221A)), requiredExtent);
      root.width = radicalCssWidth(semantic, scale);
      root.advance = heightVariant
          ? cssNodeWidth(body, scale) + heightVariant->advance - constants.spaceAfterScript +
                (semantic->radicalIndex ? constants.radicalKernBeforeDegree : 0.0)
          : root.width;
      if (heightVariant) {
        qreal extra = semantic->radicalIndex || heightVariant->extent > 30.0
            ? constants.radicalExtraAscender
            : constants.radicalRuleThickness / 2.0;
        if (nestedSemanticNode(semantic, MathSemanticKind::Array))
          extra += constants.radicalExtraAscender -
                   constants.radicalRuleThickness / 2.0;
        root.height = std::max(root.height, heightVariant->extent + extra);
      }
      if (root.height == 0.0) root.height = std::ceil(scaledHeight);
      break;
      }
    case MathSemanticKind::SupSub:
      if (const MathRenderNode* brace = firstKind(semantic, MathRenderKind::Accent);
          brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                    brace->accentKind == MathAccentKind::OverBrace)) {
        root.width = cssNodeWidth(brace, scale);
        root.advance = root.width;
        root.height = braceAccentCssHeight(semantic, brace, scale);
        break;
      }
      root.width = scaledWidth;
      if (semantic && semantic->children.size() == 2) {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        const qreal base = cssNodeWidth(semantic->children.front().get(), scale);
        const qreal scripts = cssNodeWidth(semantic->children.back().get(), scale);
        root.width = snapEighth(base + scripts + constants.spaceAfterScript);
      }
      if (semantic && hasAtomClass(semantic, QLatin1StringView("mop")))
        root.width += 2.0 * OpenTypeMathFont::instance().constants().spaceAfterScript;
      if (semantic && symbolCount(layout.root.get()) > symbolCount(semantic)) {
        root.width = cssNodeWidth(layout.root.get(), scale);
        if (semantic->operatorKind != MathOperatorKind::Limits)
          root.width -= OpenTypeMathFont::instance().constants().spaceAfterScript;
      }
      root.advance = root.width;
      {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        if (semantic->children.size() >= 3 &&
            hasAtomClass(semantic, QLatin1StringView("mop"))) {
          GlyphInkExtents sub = glyphInkExtents(
              semantic->children.front().get(), constants.scriptPercentScaleDown);
          GlyphInkExtents sup = glyphInkExtents(
              semantic->children.back().get(), constants.scriptPercentScaleDown);
          const qreal operatorHeight = std::ceil(
              maxLargeOperatorExtent(semantic->children[1].get()));
          sub.top = std::max(sub.top, std::round(2.0 * constants.axisHeight));
          sup.top = std::max(
              sup.top,
              std::round(2.0 * constants.axisHeight *
                         constants.scriptPercentScaleDown));
          const qreal upperGap = std::max(constants.upperLimitGapMin,
                                          constants.upperLimitBaselineRiseMin);
          const qreal lowerGap = std::max(
              constants.lowerLimitGapMin,
              constants.lowerLimitBaselineDropMin - sub.top);
          root.height = sup.top + sup.bottom + upperGap + operatorHeight +
                        lowerGap + sub.top + sub.bottom;
          break;
        }
        if (semantic->children.size() == 2 &&
            hasAtomClass(semantic, QLatin1StringView("mop"))) {
          root.height = maxLargeOperatorExtent(semantic) +
                        constants.superscriptBaselineDropMax +
                        constants.subscriptBaselineDropMin;
          if (semantic->operatorKind == MathOperatorKind::Limits) {
            const qreal siblingHeight = siblingSemanticHeight(
                layout.root.get(), semantic, scale);
            if (siblingHeight > 0.0)
              root.height = std::max(
                  root.height, siblingHeight + constants.spaceAfterScript);
          }
          break;
        }
        const GlyphInkExtents base = glyphInkExtents(
            semantic->children.empty() ? nullptr : semantic->children.front().get(), 1.0);
        const MathRenderNode* scripts = semantic->children.size() > 1
            ? semantic->children.back().get() : nullptr;
        GlyphInkExtents superscript;
        GlyphInkExtents subscript;
        if (root.scriptKind == MathScriptKind::SubSup && scripts && scripts->children.size() >= 2) {
          subscript = glyphInkExtents(scripts->children.front().get(),
                                      constants.scriptPercentScaleDown);
          superscript = glyphInkExtents(scripts->children.back().get(),
                                        constants.scriptPercentScaleDown);
        } else if (root.scriptKind == MathScriptKind::Subscript) {
          subscript = glyphInkExtents(scripts, constants.scriptPercentScaleDown);
        } else {
          superscript = glyphInkExtents(scripts, constants.scriptPercentScaleDown);
        }
        qreal subShift = constants.subscriptShiftDown;
        if (root.scriptKind == MathScriptKind::SubSup) {
          const qreal gap = (constants.superscriptShiftUp - superscript.bottom) -
                            (subscript.top - subShift);
          subShift += std::max<qreal>(0.0, constants.subSuperscriptGapMin - gap);
        }
        const qreal top = root.scriptKind == MathScriptKind::Subscript
            ? base.top
            : std::max(base.top, constants.superscriptShiftUp + superscript.top);
        const qreal bottom = root.scriptKind == MathScriptKind::Superscript
            ? base.bottom
            : std::max(base.bottom, subShift + subscript.bottom);
        root.height = top + bottom;
      }
      if (semantic && semantic->operatorKind == MathOperatorKind::Limits) {
        const qreal siblingHeight = siblingSemanticHeight(
            layout.root.get(), semantic, scale);
        if (siblingHeight > 0.0)
          root.height = std::max(
              root.height,
              siblingHeight + OpenTypeMathFont::instance().constants().spaceAfterScript);
      }
      if (semantic &&
          (hasNestedSemantic(semantic, MathSemanticKind::Fraction) ||
           hasNestedSemantic(semantic, MathSemanticKind::Radical))) {
        root.height = std::max(root.height, scaledHeight);
        root.height = std::max(root.height, cssNodeHeight(semantic, scale));
        if (semantic->children.size() == 2 &&
            root.scriptKind == MathScriptKind::Subscript) {
          const MathFontConstants& constants =
              OpenTypeMathFont::instance().constants();
          const MathRenderNode* baseNode = semantic->children.front().get();
          const MathRenderNode* scriptNode = semantic->children.back().get();
          const GlyphInkExtents baseInk = glyphInkExtents(baseNode, 1.0);
          const GlyphInkExtents scriptInk = glyphInkExtents(
              scriptNode, constants.scriptPercentScaleDown);
          const qreal subShift = std::max({
              constants.subscriptShiftDown,
              scriptInk.top - constants.subscriptTopMax,
              constants.subscriptBaselineDropMin + baseInk.bottom});
          const qreal scriptTop = baseInk.top + subShift - scriptInk.top;
          root.height = std::max(
              root.height, scriptTop + cssNodeHeight(scriptNode, scale));
          root.height = snapLayoutUnit(root.height);
        }
      }
      break;
    case MathSemanticKind::Array:
      root.width = arrayCssWidth(semantic, scale);
      if (firstKind(layout.root.get(), MathRenderKind::LeftRight))
        root.width = cssNodeWidth(layout.root.get(), scale);
      root.advance = root.width - cssRootFontPixelSize * 0.4;
      root.height = arrayCssHeight(semantic, scale);
      if (root.height == 0.0) {
        for (size_t row = 0; row < semantic->arrayRowHeights.size(); ++row) {
          const bool inkDescender = row < semantic->arrayRowInkDescenders.size() &&
                                    semantic->arrayRowInkDescenders[row];
          root.height += cssRootFontPixelSize *
              (kMathTableRowHeightEm +
               (inkDescender ? kMathTableDescenderExpansionEm : 0.0));
        }
      }
      break;
    case MathSemanticKind::None:
      if (symbolCount(layout.root.get()) == 1) {
        const auto glyph = nativeGlyphBox(singleSymbol(layout.root.get()));
        if (glyph) {
          root.width = glyph->width;
          root.height = glyph->height;
        } else {
          root.width = std::round(scaledWidth);
          root.height = std::ceil((layout.root->children.empty()
              ? layout.root->height + layout.root->depth
              : layout.root->children.front()->height + layout.root->children.front()->depth) * scale);
        }
      } else {
        root.width = snapEighth(cssNodeWidth(layout.root.get(), scale));
        root.height = (layout.root->children.empty()
            ? layout.root->height + layout.root->depth
            : layout.root->children.front()->height + layout.root->children.front()->depth) * scale;
        root.height = std::ceil(root.height);
        if (hasAtomClass(layout.root.get(), QLatin1StringView("mbin"))) root.height += 1.0;
        root.height = std::max(root.height,
                               std::ceil(maxLargeOperatorExtent(layout.root.get())));
        if (const MathRenderNode* accent = firstKind(layout.root.get(), MathRenderKind::Accent);
            accent && accent->children.size() >= 2) {
          const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                                  accent->accentKind == MathAccentKind::OverBrace;
          const MathRenderNode* body = bodyIsLast ? accent->children.back().get()
                                                  : accent->children.front().get();
          qreal bodyHeight = std::ceil((body->height + body->depth) * scale);
          if (accent->accentKind == MathAccentKind::Under) {
            const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
            bodyHeight = bodyInk.top + bodyInk.bottom;
          }
          if (symbolCount(body) == 1) {
            if (const auto glyph = nativeGlyphBox(singleSymbol(body))) bodyHeight = glyph->height;
          } else if (hasAtomClass(body, QLatin1StringView("mbin"))) {
            bodyHeight += 1.0;
          }
          if (accent->accentKind == MathAccentKind::Underline &&
              hasMathMlOperatorOverflow(body))
            bodyHeight += std::floor(
                OpenTypeMathFont::instance().constants().fractionRuleThickness);
          const MathRenderNode* accentPart = bodyIsLast
              ? accent->children.front().get() : accent->children.back().get();
          qreal accentHeight = 0.0;
          if (accent->accentKind == MathAccentKind::Overline)
            accentHeight = OpenTypeMathFont::instance().constants().fractionRuleThickness;
          if (accent->accentKind == MathAccentKind::Over ||
              accent->accentKind == MathAccentKind::Under) {
            if (const MathRenderNode* stretchy = firstKind(
                    accentPart, MathRenderKind::Stretchy))
              accentHeight = accent->accentKind == MathAccentKind::Under
                  ? std::floor((stretchy->height + stretchy->depth) * scale)
                  : std::ceil((stretchy->height + stretchy->depth) * scale);
          }
          if (const MathRenderNode* symbol = singleSymbol(accentPart)) {
            if (const auto glyph = OpenTypeMathFont::instance().glyph(symbol->text))
              accentHeight = accent->accentKind == MathAccentKind::Under
                  ? std::floor(glyph->inkBounds.height())
                  : std::ceil(glyph->inkBounds.height());
          } else if (const MathRenderNode* stretchy = firstKind(accentPart, MathRenderKind::Stretchy);
                     stretchy && stretchy->pathName == QLatin1String("vec")) {
            if (const auto glyph = OpenTypeMathFont::instance().glyph(QString(QChar(0x20D7))))
              accentHeight = std::ceil(glyph->inkBounds.height());
          }
          if (accent->accentKind == MathAccentKind::Under &&
              !accent->accentCharacter.isEmpty()) {
            if (const auto glyph = OpenTypeMathFont::instance().glyph(
                    accent->accentCharacter))
              accentHeight = std::ceil(glyph->inkBounds.height());
          }
          root.height = bodyHeight + accentHeight +
                        OpenTypeMathFont::instance().constants().fractionRuleThickness;
        }
      }
      root.advance = root.width;
      break;
  }
  root.height = std::max(root.height, std::ceil(
      maxLeftRightFenceExtent(layout.root.get(), root.height)));
  if (root.semanticKind == MathSemanticKind::Fraction) {
    const qreal fractionHeight = fractionMathMlHeight(
        semantic, cssRootFontPixelSize, scale);
    qreal delimiterTarget = fractionHeight > 0.0 ? fractionHeight : root.height;
    if (semantic->fractionHasBarLine) {
      delimiterTarget += semantic->fractionLineThicknessEm >= 0.0
          ? semantic->fractionLineThicknessEm * cssRootFontPixelSize *
                semantic->fractionSizeMultiplier
          : OpenTypeMathFont::instance().constants().fractionRuleThickness *
                semantic->fractionSizeMultiplier;
    }
    root.height = std::max(root.height, std::ceil(
        fractionDelimiterExtent(semantic, delimiterTarget)));
  }
  root.baseline = root.height / 2.0 + kChromiumMathAxisOffsetPx;
  if (root.semanticKind == MathSemanticKind::Fraction)
    root.baseline = std::round(root.baseline * 2.0) / 2.0;
  if (root.semanticKind == MathSemanticKind::SupSub && semantic &&
      semantic->children.size() >= 3 &&
      hasAtomClass(semantic, QLatin1StringView("mop")))
    root.baseline = std::round(root.baseline * 2.0) / 2.0;
  root.inkTop = 0.0;
  root.inkBottom = root.height;
  switch (root.semanticKind) {
    case MathSemanticKind::Fraction:
      root.inkTop = -4.0;
      root.inkBottom = root.height + 4.0;
      break;
    case MathSemanticKind::Radical:
      if (hasNestedSemantic(semantic, MathSemanticKind::SupSub)) {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        root.inkTop = constants.radicalVerticalGap + constants.radicalExtraAscender +
                      constants.radicalRuleThickness / 2.0;
        root.inkBottom = root.baseline + 1.6;
      }
      break;
    case MathSemanticKind::Array:
      root.inkTop = OpenTypeMathFont::instance().constants().axisHeight -
                    OpenTypeMathFont::instance().constants().fractionRuleThickness / 4.0;
      root.inkBottom = root.height + 0.5;
      break;
    case MathSemanticKind::SupSub:
    case MathSemanticKind::None:
      break;
  }
  return root;
}

std::optional<MathCssAccentBox> layoutMathMlAccentBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  if (!layout.valid() || renderFontPixelSize <= 0.0 || cssRootFontPixelSize <= 0.0)
    return std::nullopt;
  const MathRenderNode* semantic = primarySemanticNode(layout.root.get());
  const MathRenderNode* accent = semantic &&
          semantic->semanticKind == MathSemanticKind::SupSub
      ? firstKind(semantic, MathRenderKind::Accent)
      : firstKind(layout.root.get(), MathRenderKind::Accent);
  if (!accent || accent->children.size() < 2)
    return std::nullopt;

  const bool brace = accent->accentKind == MathAccentKind::UnderBrace ||
                     accent->accentKind == MathAccentKind::OverBrace;
  const bool arrow = (accent->accentKind == MathAccentKind::Under ||
                      accent->accentKind == MathAccentKind::Over) &&
                     !accent->accentCharacter.isEmpty();
  if (!brace && !arrow) return std::nullopt;
  const bool over = accent->accentKind == MathAccentKind::OverBrace ||
                    accent->accentKind == MathAccentKind::Over;
  const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                          accent->accentKind == MathAccentKind::OverBrace;
  const MathRenderNode* body = bodyIsLast ? accent->children.back().get()
                                         : accent->children.front().get();
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const qreal bodyHeight = accentBodyCssHeight(
      body, cssRootFontPixelSize / renderFontPixelSize);
  const qreal gap = over ? constants.overbarVerticalGap
                         : constants.underbarVerticalGap;
  const qreal accentHeight = std::round(gap + 2.0 *
      (over ? constants.overbarRuleThickness : constants.underbarRuleThickness));
  const qreal extra = over ? constants.overbarExtraAscender
                           : constants.underbarExtraDescender;
  const MathCssBox root = layoutMathMlCssBox(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  const qreal rootHeight = snapEighth(root.height);

  if (arrow) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto fixed = font.horizontalVariant(accent->accentCharacter, root.width);
    const bool useFixed = fixed && fixed->extent >= root.width;
    const auto assembly = useFixed ? std::optional<MathGlyphAssembly>{}
        : font.horizontalAssemblyParts(accent->accentCharacter, root.width);
    if (!useFixed && !assembly) return std::nullopt;
    const qreal arrowHeight = std::ceil(
        useFixed ? fixed->advance : assembly->advance);
    MathCssAccentBox result;
    result.over = over;
    result.character = accent->accentCharacter;
    result.body = QRectF(0.0, over ? rootHeight - bodyHeight : 0.0,
                         root.width, bodyHeight);
    result.accent = QRectF(0.0, over ? result.body.top() - arrowHeight
                                    : result.body.bottom(),
                           root.width, arrowHeight);
    return result;
  }

  const qreal annotationHeight = std::max<qreal>(
      0.0, rootHeight - bodyHeight - accentHeight - 2.0 * gap - extra);

  MathCssAccentBox result;
  result.over = over;
  result.character = QString(QChar(over ? 0x23DE : 0x23DF));
  result.fontScale = constants.scriptPercentScaleDown;
  if (over) {
    result.annotation = QRectF(0.0, 0.0, root.width, annotationHeight);
    result.body = QRectF(0.0, rootHeight - bodyHeight, root.width, bodyHeight);
    result.accent = QRectF(0.0, result.body.top() - gap - accentHeight,
                           root.width, accentHeight);
  } else {
    result.body = QRectF(0.0, 0.0, root.width, bodyHeight);
    result.accent = QRectF(0.0, result.body.bottom() + gap,
                           root.width, accentHeight);
    result.annotation = QRectF(0.0, rootHeight - annotationHeight,
                               root.width, annotationHeight);
  }
  return result;
}

namespace {

bool hasPaintOperation(MathSemanticKind kind) {
  return kind == MathSemanticKind::Fraction ||
         kind == MathSemanticKind::Radical ||
         kind == MathSemanticKind::SupSub ||
         kind == MathSemanticKind::Array;
}

bool isSupportedHorizontalAccent(const MathRenderNode* node) {
  if (!node || node->kind != MathRenderKind::Accent) return false;
  const bool brace = node->accentKind == MathAccentKind::UnderBrace ||
                     node->accentKind == MathAccentKind::OverBrace;
  const bool arrow = (node->accentKind == MathAccentKind::Under ||
                      node->accentKind == MathAccentKind::Over) &&
                     !node->accentCharacter.isEmpty();
  return brace || arrow;
}

const MathRenderNode* ownedHorizontalAccent(const MathRenderNode* node) {
  if (isSupportedHorizontalAccent(node)) return node;
  if (!node || node->semanticKind != MathSemanticKind::SupSub ||
      node->kind != MathRenderKind::VList)
    return nullptr;
  const MathRenderNode* accent = firstKind(node, MathRenderKind::Accent);
  return isSupportedHorizontalAccent(accent) ? accent : nullptr;
}

bool ownsAccentPaintOperation(const MathRenderNode* node) {
  return ownedHorizontalAccent(node) != nullptr;
}

bool ownsGenericPaintOperation(const MathRenderNode* node) {
  return node && hasPaintOperation(node->semanticKind) &&
         !(node->semanticKind == MathSemanticKind::SupSub &&
           firstKind(node, MathRenderKind::Accent));
}

bool ownsPaintOperation(const MathRenderNode* node) {
  return ownsAccentPaintOperation(node) || ownsGenericPaintOperation(node);
}

bool findPaintOrigin(const MathRenderNode* node,
                     const MathRenderNode* target, QPointF origin,
                     QPointF* result) {
  if (!node || !target || !result) return false;
  if (node == target) {
    *result = origin;
    return true;
  }
  const auto descend = [&](const MathRenderNode* child,
                           QPointF childOrigin) {
    return findPaintOrigin(child, target, childOrigin, result);
  };
  if (node->kind == MathRenderKind::Span) {
    qreal x = origin.x();
    for (const auto& child : node->children) {
      const QPointF childOrigin(
          x + child->xOffset,
          origin.y() + child->yOffset + child->shift);
      if (descend(child.get(), childOrigin)) return true;
      x += child->width;
    }
    return false;
  }
  if (node->kind == MathRenderKind::SupSub) {
    for (size_t index = 0; index < node->children.size(); ++index) {
      const auto& child = node->children[index];
      const QPointF childOrigin(
          origin.x() + child->xOffset,
          origin.y() + child->yOffset +
              (index == 0 ? 0.0 : child->shift));
      if (descend(child.get(), childOrigin)) return true;
    }
    return false;
  }
  if (node->kind == MathRenderKind::Fraction &&
      node->children.size() >= 2) {
    for (size_t index : {size_t{0}, node->children.size() - 1}) {
      const auto& child = node->children[index];
      const QPointF childOrigin(
          origin.x() + (node->width - child->width) / 2.0,
          origin.y() + child->shift);
      if (descend(child.get(), childOrigin)) return true;
    }
    if (node->children.size() >= 3 &&
        descend(node->children[1].get(), origin))
      return true;
    return false;
  }
  for (const auto& child : node->children) {
    const QPointF childOrigin(
        origin.x() + child->xOffset,
        origin.y() + child->yOffset + child->shift);
    if (descend(child.get(), childOrigin)) return true;
  }
  return false;
}

void bindSourceOrigins(MathCssPaintOperation* operation,
                       const MathLayoutResult& layout) {
  if (!operation || !layout.root) return;
  if (auto* accent = std::get_if<MathCssAccentOperation>(
          &operation->payload)) {
    const QPointF rootOrigin(0.0, layout.baseline);
    accent->hasBodySourceOrigin = findPaintOrigin(
        layout.root.get(), accent->bodyNode, rootOrigin,
        &accent->bodySourceOrigin);
    accent->hasAnnotationSourceOrigin = findPaintOrigin(
        layout.root.get(), accent->annotationNode, rootOrigin,
        &accent->annotationSourceOrigin);
  }
  for (MathCssPaintOperation& child : operation->children)
    bindSourceOrigins(&child, layout);
}

void collectImmediatePaintNodes(const MathRenderNode* node,
                                QVector<const MathRenderNode*>* operations) {
  if (!node || !operations) return;
  if (ownsPaintOperation(node)) {
    operations->push_back(node);
    return;
  }
  if (node->semanticKind != MathSemanticKind::None) return;
  for (const auto& child : node->children)
    collectImmediatePaintNodes(child.get(), operations);
}

GlyphInkExtents preciseTokenInkExtents(const MathRenderNode* node,
                                       qreal fontScale) {
  const MathRenderNode* symbol = singleSymbol(node);
  if (!symbol || symbol->text.size() != 1)
    return glyphInkExtents(node, fontScale);
  const QChar character = symbol->text.front();
  const bool italic = character.isLetter() &&
      (symbol->fontClass == QLatin1String("mathnormal") ||
       symbol->fontClass == QLatin1String("mathit"));
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const auto glyph = italic ? font.mathItalicGlyph(character)
                            : font.glyph(symbol->text);
  if (!glyph) return glyphInkExtents(node, fontScale);
  return {std::max<qreal>(0.0, -glyph->inkBounds.top()) * fontScale,
          std::max<qreal>(0.0, glyph->inkBounds.bottom()) * fontScale};
}

struct CssTokenLineMetrics {
  GlyphInkExtents ink;
  qreal ascent = 0.0;
  qreal descent = 0.0;

  qreal height() const { return ascent + descent; }
};

CssTokenLineMetrics cssTokenLineMetrics(const MathRenderNode* node,
                                        qreal renderScale,
                                        qreal fontScale) {
  CssTokenLineMetrics result;
  result.ink = preciseTokenInkExtents(node, fontScale);
  if (const MathRenderNode* symbol = singleSymbol(node)) {
    const qreal height = std::round(result.ink.top + result.ink.bottom);
    result.ascent = std::min(height, std::round(result.ink.top));
    result.descent = std::max<qreal>(0.0, height - result.ascent);
    if (fontScale >= 0.999) {
      if (const auto glyph = nativeGlyphBox(symbol)) {
        const qreal glyphHeight = std::round(glyph->height);
        result.descent += std::max<qreal>(0.0, glyphHeight - result.height());
      }
    }
    return result;
  }
  const qreal height = cssNodeHeight(node, renderScale);
  result.ascent = std::min(height, std::round(result.ink.top));
  result.descent = std::max<qreal>(0.0, height - result.ascent);
  return result;
}

std::optional<MathCssScriptOperation> buildScriptOperation(
    const MathRenderNode* script, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale) {
  if (!script || script->semanticKind != MathSemanticKind::SupSub ||
      script->children.size() != 2)
    return std::nullopt;
  const MathRenderNode* base = script->children.front().get();
  const MathRenderNode* scriptBox = script->children.back().get();
  const MathRenderNode* superscript = nullptr;
  const MathRenderNode* subscript = nullptr;
  if (script->scriptKind == MathScriptKind::SubSup &&
      scriptBox && scriptBox->children.size() >= 2) {
    subscript = scriptBox->children.front().get();
    superscript = scriptBox->children.back().get();
  } else if (script->scriptKind == MathScriptKind::Subscript) {
    subscript = scriptBox;
  } else {
    superscript = scriptBox;
  }

  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const CssTokenLineMetrics baseMetrics = cssTokenLineMetrics(
      base, renderScale, 1.0);
  const CssTokenLineMetrics supMetrics = cssTokenLineMetrics(
      superscript, renderScale, constants.scriptPercentScaleDown);
  const CssTokenLineMetrics subMetrics = cssTokenLineMetrics(
      subscript, renderScale, constants.scriptPercentScaleDown);
  const GlyphInkExtents& baseInk = baseMetrics.ink;
  const GlyphInkExtents& supInk = supMetrics.ink;
  const GlyphInkExtents& subInk = subMetrics.ink;
  qreal supShift = std::max({
      constants.superscriptShiftUpCramped,
      constants.superscriptBottomMin + supInk.bottom,
      baseInk.top - constants.superscriptBaselineDropMax});
  qreal subShift = std::max({
      constants.subscriptShiftDown,
      subInk.top - constants.subscriptTopMax,
      constants.subscriptBaselineDropMin + baseInk.bottom});
  if (script->scriptKind == MathScriptKind::SubSup) {
    qreal gap = supShift + subShift - supInk.bottom - subInk.top;
    const qreal missingGap = constants.subSuperscriptGapMin - gap;
    if (missingGap > 0.0) {
      const qreal superscriptRoom =
          constants.superscriptBottomMaxWithSubscript -
          (supShift - supInk.bottom);
      const qreal raise = std::max<qreal>(
          0.0, std::min(superscriptRoom, missingGap));
      supShift += raise;
      gap += raise;
      subShift += std::max<qreal>(
          0.0, constants.subSuperscriptGapMin - gap);
    }
  }
  const qreal lineAscent = script->scriptKind == MathScriptKind::Subscript
      ? baseInk.top
      : std::max(baseInk.top, supShift + supInk.top);
  const qreal lineDescent = script->scriptKind == MathScriptKind::Superscript
      ? baseInk.bottom
      : std::max(baseInk.bottom, subShift + subInk.bottom);
  const qreal width = cssNodeWidth(script, renderScale);
  const qreal height = lineAscent + lineDescent;
  const qreal left = containingRect.left() + cssNodeOffset(
      containingNode, script, renderScale).value_or(0.0);
  const bool fillsContainingRow = primarySemanticNode(containingNode) == script;
  const qreal top = fillsContainingRow
      ? containingRect.top()
      : containingRect.top() + (containingRect.height() - height) / 2.0;
  const qreal baseWidth = cssNodeWidth(base, renderScale);

  MathCssScriptOperation result;
  result.kind = script->scriptKind;
  result.lineAscent = lineAscent;
  result.lineDescent = lineDescent;
  result.container = fillsContainingRow
      ? QRectF(left, containingRect.top(), width, containingRect.height())
      : QRectF(left, top, width, height);
  result.baseNode = base;
  result.superscriptNode = superscript;
  result.subscriptNode = subscript;
  result.base = QRectF(left, top + lineAscent - baseMetrics.ascent,
                       baseWidth, baseMetrics.height());
  if (superscript) {
    result.superscript = QRectF(
        left + baseWidth, top + lineAscent - supShift - supInk.top,
        cssNodeWidth(superscript, renderScale),
        supMetrics.height());
  }
  if (subscript) {
    result.subscript = QRectF(
        left + baseWidth, top + lineAscent + subShift - subInk.top,
        cssNodeWidth(subscript, renderScale),
        subMetrics.height());
  }
  result.container = snapVerticalLayoutRect(result.container);
  result.base = snapVerticalLayoutRect(result.base);
  result.superscript = snapVerticalLayoutRect(result.superscript);
  result.subscript = snapVerticalLayoutRect(result.subscript);
  return result;
}

std::optional<MathCssRadicalOperation> buildRadicalOperation(
    const MathRenderNode* radical, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, bool topLevel = false) {
  if (!radical || radical->semanticKind != MathSemanticKind::Radical)
    return std::nullopt;
  const MathRenderNode* radicalBox = radical->radicalIndex
      ? nestedSemanticNode(radical, MathSemanticKind::Radical) : radical;
  const MathRenderNode* body = radicalBody(radicalBox);
  if (!body) return std::nullopt;

  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const MathFontConstants& constants = font.constants();
  const qreal styleScale = mathStyleScale(radical);
  const qreal radicalGap = constants.radicalVerticalGap * styleScale;
  const qreal radicalRule = constants.radicalRuleThickness * styleScale;
  const qreal radicalExtra = constants.radicalExtraAscender * styleScale;
  const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
  const qreal bodyWidth = cssNodeWidth(body, renderScale);
  qreal bodyHeight = primarySemanticNode(body)
      ? cssNodeHeight(body, renderScale)
      : std::ceil(bodyInk.top + bodyInk.bottom);
  if (const auto glyph = nativeGlyphBox(singleSymbol(body)))
    bodyHeight = std::round(glyph->height);
  else if (!primarySemanticNode(body) && symbolCount(body) > 1)
    bodyHeight += 1.0;
  qreal target = std::max(bodyInk.top + bodyInk.bottom, bodyHeight) +
                 radicalGap + radicalRule;
  if (topLevel)
    target = std::max(
        target, containingRect.height() - radicalExtra);
  const auto variant = font.verticalVariant(
      QString(QChar(0x221A)), target / styleScale);
  if (!variant) return std::nullopt;

  const qreal bodyAscent = std::min(bodyHeight, bodyInk.top);
  const qreal lineAscent = std::max(
      bodyAscent, bodyInk.top + radicalGap +
                      radicalRule + radicalExtra);
  const qreal lineDescent = std::max(
      bodyHeight - bodyAscent,
      variant->extent * styleScale + radicalExtra - lineAscent);
  const qreal height = lineAscent + lineDescent;
  const qreal left = containingRect.left() + cssNodeOffset(
      containingNode, radical, renderScale).value_or(0.0);
  const bool fillsContainingRow = primarySemanticNode(containingNode) == radical;
  const qreal top = fillsContainingRow
      ? containingRect.top()
      : containingRect.top() + (containingRect.height() - height) / 2.0;

  MathCssRadicalOperation result;
  result.lineAscent = lineAscent;
  result.lineDescent = lineDescent;
  result.container = fillsContainingRow
      ? QRectF(left, containingRect.top(), radicalCssWidth(radical, renderScale),
               containingRect.height())
      : QRectF(left, top, radicalCssWidth(radical, renderScale), height);
  result.body = QRectF(left + variant->advance * styleScale,
                       top + lineAscent - bodyAscent,
                       bodyWidth, bodyHeight);
  result.rule = QRectF(result.body.left(),
                       top + radicalExtra,
                       bodyWidth, radicalRule);
  result.glyph = QRectF(left, top + radicalExtra,
                        variant->advance * styleScale,
                        variant->extent * styleScale);
  result.bodyNode = body;
  result.glyphIndex = variant->glyphIndex;
  result.container = snapVerticalLayoutRect(result.container);
  result.body = snapVerticalLayoutRect(result.body);
  result.rule = snapVerticalLayoutRect(result.rule);
  result.glyph = snapVerticalLayoutRect(result.glyph);
  return result;
}

qreal nestedFractionCssHeight(const MathRenderNode* fraction,
                              qreal cssRootFontPixelSize,
                              qreal renderScale) {
  return fractionMathMlHeight(
             fraction, cssRootFontPixelSize, renderScale) +
         std::round(OpenTypeMathFont::instance().constants()
                        .fractionRuleThickness *
                    fraction->fractionSizeMultiplier);
}

std::optional<MathCssPaintOperation> buildPaintOperation(
    const MathRenderNode* operationNode, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize,
    bool topLevel = false);

std::optional<MathCssPaintOperation> buildAccentOperation(
    const MathRenderNode* owner, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize) {
  const MathRenderNode* accent = ownedHorizontalAccent(owner);
  if (!accent || accent->children.size() < 2) return std::nullopt;

  MathLayoutResult probe;
  probe.root = cloneNode(*owner);
  probe.naturalSize = QSizeF(owner->width, owner->height + owner->depth);
  probe.size = probe.naturalSize;
  probe.baseline = owner->height;
  const qreal renderFontPixelSize = cssRootFontPixelSize / renderScale;
  const auto localBox = layoutMathMlAccentBox(
      probe, renderFontPixelSize, cssRootFontPixelSize);
  if (!localBox) return std::nullopt;
  const MathCssBox localRoot = layoutMathMlCssBox(
      probe, renderFontPixelSize, cssRootFontPixelSize);

  const qreal left = containingRect.left() +
      cssNodeOffset(containingNode, owner, renderScale).value_or(0.0);
  const qreal localHeight = std::max({localBox->body.bottom(),
                                     localBox->accent.bottom(),
                                     localBox->annotation.bottom()});
  const qreal top = containingRect.top() +
                    (containingRect.height() - localHeight) / 2.0;
  const QPointF translation(left, top);

  const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                          accent->accentKind == MathAccentKind::OverBrace;
  const MathRenderNode* body = bodyIsLast
      ? accent->children.back().get() : accent->children.front().get();
  const MathRenderNode* annotation = nullptr;
  if (owner->semanticKind == MathSemanticKind::SupSub) {
    for (const auto& child : owner->children) {
      if (child && !containsNode(child.get(), accent)) {
        annotation = child.get();
        break;
      }
    }
  }

  MathCssPaintOperation operation;
  operation.payload = MathCssAccentOperation{};
  MathCssAccentOperation& result =
      std::get<MathCssAccentOperation>(operation.payload);
  result.box = *localBox;
  result.bodyUsesLayoutScale = accent->accentKind == MathAccentKind::Under ||
                               accent->accentKind == MathAccentKind::Over;
  const MathRenderNode* bodySemantic = primarySemanticNode(body);
  result.fixedVariantUsesNaturalScale = bodySemantic != nullptr;
  if (bodySemantic && bodySemantic->semanticKind == MathSemanticKind::Array) {
    result.fixedVariantTargetWidth = std::max<qreal>(
        0.0, result.box.accent.width() -
                 OpenTypeMathFont::instance().pixelSize() * 0.8);
  }
  result.box.body.translate(translation);
  result.box.accent.translate(translation);
  result.box.annotation.translate(translation);
  result.container = QRectF(left, top, localRoot.width, localHeight);
  result.bodyNode = body;
  result.annotationNode = annotation;
  if (annotation && !result.box.annotation.isEmpty()) {
    const qreal annotationWidth = cssNodeWidth(annotation, renderScale);
    const qreal annotationHeight = std::round(
        cssNodeHeight(annotation, renderScale) *
        OpenTypeMathFont::instance().constants().scriptPercentScaleDown);
    result.annotationContent = QRectF(
        result.box.annotation.center().x() - annotationWidth / 2.0,
        result.box.over
            ? result.box.annotation.bottom() - annotationHeight
            : result.box.annotation.top(),
        annotationWidth, annotationHeight);
  }
  result.lineAscent = result.container.height() / 2.0 +
                      kChromiumMathAxisOffsetPx;

  const auto appendRegion = [&](const MathRenderNode* regionNode,
                                QRectF regionRect) {
    if (!regionNode || regionRect.isEmpty()) return;
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(regionNode, &nodes);
    for (const MathRenderNode* nested : nodes) {
      if (auto child = buildPaintOperation(
              nested, regionNode, regionRect, renderScale,
              cssRootFontPixelSize))
        operation.children.push_back(std::move(*child));
    }
  };
  appendRegion(body, result.box.body);
  appendRegion(annotation, result.annotationContent);
  return operation;
}

std::optional<MathCssPaintOperation> buildArrayOperation(
    const MathRenderNode* array, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize) {
  if (!array || array->semanticKind != MathSemanticKind::Array ||
      array->rows <= 0 || array->columns <= 0)
    return std::nullopt;
  const MathRenderNode* table = arrayTableBody(array, array->columns);
  if (!table) return std::nullopt;

  QString leftDelimiter = array->arrayLeftDelimiter;
  QString rightDelimiter = array->arrayRightDelimiter;
  if (const MathRenderNode* leftRight = enclosingKind(
          containingNode, array, MathRenderKind::LeftRight)) {
    if (leftDelimiter.isEmpty()) leftDelimiter = leftRight->leftDelimiter;
    if (rightDelimiter.isEmpty()) rightDelimiter = leftRight->rightDelimiter;
  }

  const QVector<qreal> rowHeights = arrayCssRowHeights(
      array, table, renderScale);
  qreal tableHeight = 0.0;
  for (qreal height : rowHeights) tableHeight += height;
  QVector<qreal> columnWidths(array->columns);
  qreal tableWidth = 0.0;
  const qreal horizontalPadding =
      OpenTypeMathFont::instance().pixelSize() * 0.8;
  for (int column = 0; column < array->columns; ++column) {
    const qreal contentWidth = column < static_cast<int>(table->children.size())
        ? cssNodeWidth(table->children[static_cast<size_t>(column)].get(),
                       renderScale)
        : 0.0;
    columnWidths[column] = contentWidth + horizontalPadding;
    tableWidth += columnWidths[column];
  }

  const qreal leftWidth = arrayDelimiterWidth(leftDelimiter, tableHeight);
  const qreal rightWidth = arrayDelimiterWidth(rightDelimiter, tableHeight);
  const qreal intrinsicWidth = leftWidth + tableWidth + rightWidth;
  const bool fillsContainingRegion =
      primarySemanticNode(containingNode) == array;
  const qreal left = fillsContainingRegion
      ? containingRect.left()
      : containingRect.left() + cssNodeOffset(
            containingNode, array, renderScale).value_or(0.0);
  qreal delimiterHeight = 0.0;
  for (const QString& delimiter : {leftDelimiter, rightDelimiter}) {
    if (delimiter.isEmpty() || delimiter == QLatin1String(".")) continue;
    if (const auto variant = OpenTypeMathFont::instance().verticalVariant(
            mathDelimiterCharacter(delimiter), tableHeight))
      delimiterHeight = std::max(delimiterHeight, variant->extent);
  }
  const qreal height = fillsContainingRegion
      ? containingRect.height()
      : std::max(tableHeight, std::ceil(delimiterHeight));
  const qreal top = fillsContainingRegion
      ? containingRect.top()
      : containingRect.top() + (containingRect.height() - height) / 2.0;

  MathCssPaintOperation operation;
  operation.payload = MathCssArrayOperation{};
  MathCssArrayOperation& result =
      std::get<MathCssArrayOperation>(operation.payload);
  result.container = QRectF(
      left, top,
      fillsContainingRegion ? containingRect.width() : intrinsicWidth,
      height);
  result.table = QRectF(
      left + leftWidth,
      top + (height - tableHeight) / 2.0,
      tableWidth, tableHeight);
  result.leftDelimiter = QRectF(left, top, leftWidth, height);
  result.rightDelimiter = QRectF(
      result.table.right(), top, rightWidth, height);
  result.leftDelimiterCharacter = mathDelimiterCharacter(leftDelimiter);
  result.rightDelimiterCharacter = mathDelimiterCharacter(rightDelimiter);
  result.lineAscent = result.container.height() / 2.0 +
                      kChromiumMathAxisOffsetPx;

  struct CellMetrics {
    const MathRenderNode* node = nullptr;
    qreal width = 0.0;
    qreal height = 0.0;
    qreal baseline = 0.0;
  };
  QVector<CellMetrics> cells(array->rows * array->columns);
  const auto cellIndex = [array](int row, int column) {
    return row * array->columns + column;
  };
  for (int row = 0; row < array->rows; ++row) {
    for (int column = 0; column < array->columns; ++column) {
      CellMetrics& cell = cells[cellIndex(row, column)];
      cell.node = arrayCellNode(array, table, row, column);
      cell.width = cssNodeWidth(cell.node, renderScale);
      cell.height = cssNodeHeight(cell.node, renderScale);
      const MathRenderNode* semantic = primarySemanticNode(cell.node);
      if (!semantic && symbolCount(cell.node) > 1) {
        const GlyphInkExtents ink = glyphInkExtents(cell.node, 1.0);
        cell.height = std::ceil(ink.top + ink.bottom) +
            (hasMathMlOperatorOverflow(cell.node)
                 ? std::floor(OpenTypeMathFont::instance().constants()
                                  .fractionRuleThickness)
                 : 0.0);
        if (hasAtomClass(cell.node, QLatin1StringView("mrel")))
          cell.height = std::max<qreal>(
              cell.height, cssRootFontPixelSize * 0.75 +
                  (hasMathMlOperatorOverflow(cell.node)
                       ? std::floor(OpenTypeMathFont::instance().constants()
                                            .fractionRuleThickness)
                       : 0.0));
      }
      if (semantic && semantic->semanticKind == MathSemanticKind::Fraction &&
          semantic->fractionStyleSize > 0) {
        cell.height = nestedFractionCssHeight(
            semantic, cssRootFontPixelSize, renderScale);
      }
      if (semantic && ownsGenericPaintOperation(semantic)) {
        const QRectF probeRect(0.0, 0.0, cell.width, cell.height);
        if (const auto probe = buildPaintOperation(
                semantic, cell.node, probeRect, renderScale,
                cssRootFontPixelSize)) {
          cell.baseline = semantic->semanticKind == MathSemanticKind::SupSub &&
                                  semantic->scriptKind == MathScriptKind::Superscript
              ? semantic->height
              : semantic->semanticKind == MathSemanticKind::SupSub &&
                        semantic->scriptKind == MathScriptKind::Subscript
                  ? std::round(semantic->height)
              : semantic->semanticKind == MathSemanticKind::SupSub ||
                        semantic->semanticKind == MathSemanticKind::Radical
                  ? probe->lineAscent()
                  : probe->alignmentBaseline();
        }
      }
      if (cell.baseline <= 0.0) {
        const GlyphInkExtents ink = glyphInkExtents(cell.node, 1.0);
        cell.baseline = std::min(cell.height, ink.top);
      }
      if (!semantic && array->arrayEnvironment == QLatin1String("cases")) {
        cell.baseline = cell.node->height + 2.0 * cell.node->depth;
      }
    }
  }

  const qreal verticalPadding =
      OpenTypeMathFont::instance().pixelSize() *
      kMathTableCellVerticalPaddingEm / 2.0;
  qreal rowTop = result.table.top();
  for (int row = 0; row < array->rows; ++row) {
    const QRectF rowRect(result.table.left(), rowTop,
                         result.table.width(), rowHeights[row]);
    result.rows.push_back(rowRect);
    qreal maximumAscent = 0.0;
    for (int column = 0; column < array->columns; ++column)
      maximumAscent = std::max(
          maximumAscent, cells[cellIndex(row, column)].baseline);

    qreal columnLeft = result.table.left();
    for (int column = 0; column < array->columns; ++column) {
      const CellMetrics& measured = cells[cellIndex(row, column)];
      MathCssArrayCell cell;
      cell.row = row;
      cell.column = column;
      cell.box = QRectF(columnLeft, rowTop,
                        columnWidths[column], rowHeights[row]);
      cell.content = QRectF(
          columnLeft + (columnWidths[column] - measured.width) / 2.0,
          rowTop + verticalPadding + maximumAscent - measured.baseline,
          measured.width, measured.height);
      cell.contentNode = measured.node;
      result.cells.push_back(cell);
      columnLeft += columnWidths[column];
    }
    rowTop += rowHeights[row];
  }

  for (const MathCssArrayCell& cell : result.cells) {
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(cell.contentNode, &nodes);
    for (const MathRenderNode* node : nodes) {
      if (auto child = buildPaintOperation(
              node, cell.contentNode, cell.content, renderScale,
              cssRootFontPixelSize))
        operation.children.push_back(std::move(*child));
    }
  }
  return operation;
}

std::optional<MathCssPaintOperation> buildFractionOperation(
    const MathRenderNode* fraction, qreal fractionOffset, qreal semanticTop,
    qreal semanticHeight, qreal renderScale, qreal cssRootFontPixelSize,
    bool nested) {
  const auto metrics = fractionMetrics(
      fraction, cssRootFontPixelSize, renderScale);
  if (!metrics) return std::nullopt;
  const MathRenderNode* stack = directChild(fraction, MathRenderKind::VList);
  if (!stack) return std::nullopt;
  const qreal contentWidth = cssNodeWidth(stack, renderScale) + 2.0;
  const qreal targetHeight = (fraction->height + fraction->depth) * renderScale;
  const qreal leftWidth = arrayDelimiterWidth(
      fraction->leftDelimiter, targetHeight);
  const qreal rightWidth = arrayDelimiterWidth(
      fraction->rightDelimiter, targetHeight);
  const qreal fractionLeft = fractionOffset + leftWidth;
  qreal fractionHeight = metrics->extents.top + metrics->extents.bottom;
  const qreal alignmentRule = metrics->ruleThickness > 0.0
      ? metrics->ruleThickness
      : OpenTypeMathFont::instance().constants().fractionRuleThickness *
            fraction->fractionSizeMultiplier;
  const bool hasDelimiter = leftWidth > 0.0 || rightWidth > 0.0;
  if (nested && !hasDelimiter) {
    fractionHeight += std::round(
        OpenTypeMathFont::instance().constants().fractionRuleThickness *
        fraction->fractionSizeMultiplier);
  }
  if (!hasDelimiter) fractionHeight = std::max(fractionHeight, semanticHeight);
  const qreal fractionTop = hasDelimiter
      ? semanticTop + (semanticHeight - fractionHeight) / 2.0 -
            alignmentRule / 2.0
      : semanticTop;
  const qreal numeratorWidth = cssNodeWidth(metrics->numerator, renderScale);
  const qreal denominatorWidth = cssNodeWidth(metrics->denominator, renderScale);
  qreal numeratorHeight = metrics->numeratorInk.top + metrics->numeratorInk.bottom;
  qreal denominatorHeight = metrics->denominatorInk.top +
                            metrics->denominatorInk.bottom;
  const bool plainRows = !primarySemanticNode(metrics->numerator) &&
                         !primarySemanticNode(metrics->denominator);
  const int numeratorSymbols = symbolCount(metrics->numerator);
  const int denominatorSymbols = symbolCount(metrics->denominator);
  if (plainRows && numeratorSymbols > 1 && denominatorSymbols > 1) {
    // Native mfrac gives sibling token rows the same inline ink line box.
    const qreal sharedRowHeight = std::max(numeratorHeight, denominatorHeight);
    numeratorHeight = sharedRowHeight;
    denominatorHeight = sharedRowHeight;
  } else {
    const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
    const qreal childStyleScale = fraction->fractionStyleSize == 0
        ? 1.0
        : fraction->fractionStyleSize == 1
            ? constants.scriptPercentScaleDown
            : constants.scriptScriptPercentScaleDown;
    if (const auto glyph = nativeGlyphBox(singleSymbol(metrics->numerator)))
      numeratorHeight = std::round(glyph->height * childStyleScale);
    else
      numeratorHeight = cssNodeHeight(metrics->numerator, renderScale);
    if (const auto glyph = nativeGlyphBox(singleSymbol(metrics->denominator)))
      denominatorHeight = std::round(glyph->height * childStyleScale);
    else
      denominatorHeight = cssNodeHeight(metrics->denominator, renderScale);
  }
  const auto includeImmediateOperationHeight = [&](const MathRenderNode* row,
                                                    qreal height) {
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(row, &nodes);
    const QRectF intrinsicRow(0.0, 0.0, cssNodeWidth(row, renderScale), 0.0);
    for (const MathRenderNode* node : nodes) {
      const bool recursive = hasNestedSemantic(node, MathSemanticKind::Fraction) ||
                             hasNestedSemantic(node, MathSemanticKind::Radical) ||
                             hasNestedSemantic(node, MathSemanticKind::SupSub);
      if (recursive) {
        const qreal recursiveHeight = cssNodeHeight(node, renderScale);
        height = std::max(height, recursiveHeight);
        continue;
      }
      switch (node->semanticKind) {
        case MathSemanticKind::Fraction:
          height = std::max(height, nestedFractionCssHeight(
              node, cssRootFontPixelSize, renderScale));
          break;
        case MathSemanticKind::SupSub:
          if (const auto child = buildScriptOperation(
                  node, row, intrinsicRow, renderScale)) {
            height = std::max({height,
                               child->lineAscent + child->lineDescent,
                               child->base.bottom(),
                               child->superscript.bottom(),
                               child->subscript.bottom()});
          }
          break;
        case MathSemanticKind::Radical:
          if (const auto child = buildRadicalOperation(
                  node, row, intrinsicRow, renderScale)) {
            height = std::max({height,
                               child->lineAscent + child->lineDescent,
                               child->glyph.bottom(),
                               child->rule.bottom(),
                               child->body.bottom()});
          }
          break;
        case MathSemanticKind::Array:
        case MathSemanticKind::None:
          break;
      }
    }
    return snapLayoutUnit(height);
  };
  numeratorHeight = includeImmediateOperationHeight(
      metrics->numerator, numeratorHeight);
  denominatorHeight = includeImmediateOperationHeight(
      metrics->denominator, denominatorHeight);
  MathCssPaintOperation operation;
  operation.payload = MathCssFractionPaint{};
  MathCssFractionPaint& fractionPaint =
      std::get<MathCssFractionPaint>(operation.payload);
  fractionPaint.numeratorNode = metrics->numerator;
  fractionPaint.denominatorNode = metrics->denominator;
  fractionPaint.nested = nested;
  MathCssFractionBox& result = fractionPaint.box;
  result.hasRule = fraction->fractionHasBarLine;
  result.styleSize = fraction->fractionStyleSize;
  result.fontScale = fraction->fractionSizeMultiplier;
  result.container = QRectF(fractionOffset, semanticTop,
                            leftWidth + contentWidth + rightWidth,
                            semanticHeight);
  result.fraction = QRectF(fractionLeft, fractionTop, contentWidth,
                           fractionHeight);
  result.numerator = QRectF(
      fractionLeft + 1.0 + (contentWidth - 2.0 - numeratorWidth) / 2.0,
      fractionTop, numeratorWidth, numeratorHeight);
  result.denominator = QRectF(
      fractionLeft + 1.0 + (contentWidth - 2.0 - denominatorWidth) / 2.0,
      fractionTop + fractionHeight - denominatorHeight,
      denominatorWidth, denominatorHeight);
  result.leftDelimiter = QRectF(fractionOffset, semanticTop, leftWidth,
                                semanticHeight);
  result.rightDelimiter = QRectF(fractionLeft + contentWidth, semanticTop,
                                  rightWidth, semanticHeight);
  result.leftDelimiterCharacter = mathDelimiterCharacter(fraction->leftDelimiter);
  result.rightDelimiterCharacter = mathDelimiterCharacter(fraction->rightDelimiter);

  const auto appendChildren = [&](const MathRenderNode* row, QRectF rowRect) {
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(row, &nodes);
    for (const MathRenderNode* node : nodes) {
      if (auto child = buildPaintOperation(
              node, row, rowRect, renderScale, cssRootFontPixelSize))
        operation.children.push_back(std::move(*child));
    }
  };
  appendChildren(metrics->numerator, result.numerator);
  appendChildren(metrics->denominator, result.denominator);

  const auto rowLineAscent = [&](const MathRenderNode* row, QRectF rowRect) {
    for (const MathCssPaintOperation& child : operation.children) {
      if (child.container().intersects(rowRect))
        return child.container().top() - rowRect.top() + child.lineAscent();
    }
    const qreal operatorLeading = symbolCount(row) > 1 ? 1.0 : 0.0;
    return std::max<qreal>(0.0, rowRect.height() - operatorLeading);
  };
  const qreal numeratorAscent = rowLineAscent(
      metrics->numerator, result.numerator);
  const qreal denominatorAscent = rowLineAscent(
      metrics->denominator, result.denominator);
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  if (result.hasRule) {
    fractionPaint.lineAscent = std::max({
        metrics->numeratorShift + numeratorAscent,
        -metrics->denominatorShift + denominatorAscent,
        constants.axisHeight + metrics->ruleThickness / 2.0});
    const qreal ruleCenter = fractionTop + fractionPaint.lineAscent -
                             constants.axisHeight;
    result.rule = QRectF(fractionLeft + 1.0,
                         ruleCenter - metrics->ruleThickness / 2.0,
                         contentWidth - 2.0, metrics->ruleThickness);
  } else {
    fractionPaint.lineAscent = metrics->extents.top;
  }
  return operation;
}

std::optional<MathCssPaintOperation> buildPaintOperation(
    const MathRenderNode* operationNode, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize,
    bool topLevel) {
  if (!operationNode || !containingNode) return std::nullopt;
  if (ownsAccentPaintOperation(operationNode))
    return buildAccentOperation(
        operationNode, containingNode, containingRect, renderScale,
        cssRootFontPixelSize);
  if (operationNode->semanticKind == MathSemanticKind::Fraction) {
    const bool ownsRegion = primarySemanticNode(containingNode) == operationNode;
    const qreal left = ownsRegion
        ? containingRect.left()
        : containingRect.left() + cssNodeOffset(
              containingNode, operationNode, renderScale).value_or(0.0);
    const qreal fractionHeight = nestedFractionCssHeight(
        operationNode, cssRootFontPixelSize, renderScale);
    qreal height = fractionHeight;
    qreal delimiterTarget = fractionHeight;
    if (operationNode->fractionHasBarLine) {
      delimiterTarget += operationNode->fractionLineThicknessEm >= 0.0
          ? operationNode->fractionLineThicknessEm * cssRootFontPixelSize *
                operationNode->fractionSizeMultiplier
          : OpenTypeMathFont::instance().constants().fractionRuleThickness *
                operationNode->fractionSizeMultiplier;
    }
    height = std::max(height, std::ceil(
        fractionDelimiterExtent(operationNode, delimiterTarget)));
    if (ownsRegion) height = containingRect.height();
    const qreal top = ownsRegion
        ? containingRect.top()
        : containingRect.top() + (containingRect.height() - height) / 2.0;
    return buildFractionOperation(
        operationNode, left, top, height, renderScale,
        cssRootFontPixelSize, true);
  }
  if (operationNode->semanticKind == MathSemanticKind::Array)
    return buildArrayOperation(
        operationNode, containingNode, containingRect, renderScale,
        cssRootFontPixelSize);

  MathCssPaintOperation result;
  if (operationNode->semanticKind == MathSemanticKind::SupSub) {
    auto script = buildScriptOperation(
        operationNode, containingNode, containingRect, renderScale);
    if (!script) return std::nullopt;
    result.payload = std::move(*script);
  } else if (operationNode->semanticKind == MathSemanticKind::Radical) {
    auto radical = buildRadicalOperation(
        operationNode, containingNode, containingRect, renderScale, topLevel);
    if (!radical) return std::nullopt;
    result.payload = std::move(*radical);
  } else {
    return std::nullopt;
  }

  const auto appendRegion = [&](const MathRenderNode* regionNode,
                                QRectF regionRect) {
    if (!regionNode || regionRect.isEmpty()) return;
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(regionNode, &nodes);
    for (const MathRenderNode* node : nodes) {
      if (auto child = buildPaintOperation(
              node, regionNode, regionRect, renderScale,
              cssRootFontPixelSize))
        result.children.push_back(std::move(*child));
    }
  };
  if (auto* radical = std::get_if<MathCssRadicalOperation>(&result.payload);
      radical && topLevel) {
    QVector<const MathRenderNode*> bodyOperations;
    collectImmediatePaintNodes(radical->bodyNode, &bodyOperations);
    qreal bodyBaseline = cssRootFontPixelSize;
    if (bodyOperations.size() == 1 &&
        primarySemanticNode(radical->bodyNode) == bodyOperations.front()) {
      if (const auto probe = buildPaintOperation(
              bodyOperations.front(), radical->bodyNode, radical->body,
              renderScale, cssRootFontPixelSize)) {
        bodyBaseline = probe->alignmentBaseline();
        radical->body.moveTop(snapLayoutUnit(
            radical->container.top() + result.alignmentBaseline() -
            bodyBaseline));
        if (auto child = buildPaintOperation(
                bodyOperations.front(), radical->bodyNode, radical->body,
                renderScale, cssRootFontPixelSize))
          result.children.push_back(std::move(*child));
        return result;
      }
    }
    radical->body.moveTop(snapLayoutUnit(
        radical->container.top() + result.alignmentBaseline() -
        bodyBaseline));
  }
  if (const auto* script = std::get_if<MathCssScriptOperation>(&result.payload)) {
    appendRegion(script->baseNode, script->base);
    appendRegion(script->superscriptNode, script->superscript);
    appendRegion(script->subscriptNode, script->subscript);
  } else if (const auto* radical =
                 std::get_if<MathCssRadicalOperation>(&result.payload)) {
    appendRegion(radical->bodyNode, radical->body);
  }
  return result;
}

}  // namespace

MathCssPaintKind MathCssPaintOperation::kind() const {
  if (std::holds_alternative<MathCssFractionPaint>(payload))
    return MathCssPaintKind::Fraction;
  if (std::holds_alternative<MathCssScriptOperation>(payload))
    return MathCssPaintKind::SupSub;
  if (std::holds_alternative<MathCssRadicalOperation>(payload))
    return MathCssPaintKind::Radical;
  if (std::holds_alternative<MathCssArrayOperation>(payload))
    return MathCssPaintKind::Array;
  return MathCssPaintKind::Accent;
}

MathSemanticKind MathCssPaintOperation::semanticKind() const {
  if (std::holds_alternative<MathCssFractionPaint>(payload))
    return MathSemanticKind::Fraction;
  if (std::holds_alternative<MathCssScriptOperation>(payload))
    return MathSemanticKind::SupSub;
  if (std::holds_alternative<MathCssRadicalOperation>(payload))
    return MathSemanticKind::Radical;
  if (std::holds_alternative<MathCssArrayOperation>(payload))
    return MathSemanticKind::Array;
  return MathSemanticKind::None;
}

QRectF MathCssPaintOperation::container() const {
  if (const auto* fraction = std::get_if<MathCssFractionPaint>(&payload))
    return fraction->box.container;
  if (const auto* script = std::get_if<MathCssScriptOperation>(&payload))
    return script->container;
  if (const auto* radical = std::get_if<MathCssRadicalOperation>(&payload))
    return radical->container;
  if (const auto* array = std::get_if<MathCssArrayOperation>(&payload))
    return array->container;
  return std::get<MathCssAccentOperation>(payload).container;
}

qreal MathCssPaintOperation::lineAscent() const {
  if (const auto* fraction = std::get_if<MathCssFractionPaint>(&payload))
    return fraction->lineAscent;
  if (const auto* script = std::get_if<MathCssScriptOperation>(&payload))
    return script->lineAscent;
  if (const auto* radical = std::get_if<MathCssRadicalOperation>(&payload))
    return radical->lineAscent;
  if (const auto* array = std::get_if<MathCssArrayOperation>(&payload))
    return array->lineAscent;
  return std::get<MathCssAccentOperation>(payload).lineAscent;
}

qreal MathCssPaintOperation::alignmentBaseline() const {
  qreal baseline = container().height() / 2.0 + kChromiumMathAxisOffsetPx;
  if (kind() == MathCssPaintKind::Fraction)
    baseline = std::round(baseline * 2.0) / 2.0;
  return baseline;
}

std::optional<MathCssPaintOperation> layoutMathMlPaintOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  if (!layout.valid() || renderFontPixelSize <= 0.0 ||
      cssRootFontPixelSize <= 0.0)
    return std::nullopt;
  const qreal renderScale = cssRootFontPixelSize / renderFontPixelSize;
  const MathCssBox root = layoutMathMlCssBox(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  QVector<const MathRenderNode*> rootOperations;
  collectImmediatePaintNodes(layout.root.get(), &rootOperations);
  if (rootOperations.isEmpty()) return std::nullopt;
  const MathRenderNode* owner = rootOperations.front();
  if (ownsAccentPaintOperation(owner)) {
    auto operation = buildAccentOperation(
        owner, layout.root.get(),
        QRectF(0.0, 0.0, root.width, root.height), renderScale,
        cssRootFontPixelSize);
    if (operation) bindSourceOrigins(&*operation, layout);
    return operation;
  }
  if (!ownsGenericPaintOperation(owner))
    return std::nullopt;
  if (owner->semanticKind == MathSemanticKind::Fraction) {
    const qreal offset = cssNodeOffset(
        layout.root.get(), owner, renderScale).value_or(0.0);
    auto operation = buildFractionOperation(
        owner, offset, 0.0, root.height, renderScale,
        cssRootFontPixelSize, false);
    if (operation) bindSourceOrigins(&*operation, layout);
    return operation;
  }
  auto operation = buildPaintOperation(
      owner, layout.root.get(), QRectF(0.0, 0.0, root.width, root.height),
      renderScale, cssRootFontPixelSize, true);
  if (operation) bindSourceOrigins(&*operation, layout);
  return operation;
}

std::optional<MathCssFractionBox> layoutMathMlFractionBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  const auto operation = layoutMathMlPaintOperations(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  if (!operation) return std::nullopt;
  const auto* fraction = std::get_if<MathCssFractionPaint>(
      &operation->payload);
  return fraction ? std::optional<MathCssFractionBox>{fraction->box}
                  : std::nullopt;
}

}  // namespace muffin::math
