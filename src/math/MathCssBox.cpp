#include "math/MathCssBox.h"
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

qreal arrayCssHeight(const MathRenderNode* array, qreal scale) {
  const MathRenderNode* table = arrayTableBody(array, array->columns);
  if (!table) return 0.0;
  qreal height = 0.0;
  for (int row = 0; row < array->rows; ++row) {
    qreal cellHeight = 0.0;
    bool plainDescender = false;
    for (const auto& column : table->children) {
      const MathRenderNode* rows = columnRows(column.get(), array->rows);
      if (rows && row < static_cast<int>(rows->children.size())) {
        const MathRenderNode* cell = rows->children[static_cast<size_t>(row)].get();
        if ((array->arrayEnvironment == QLatin1String("cases") ||
             array->arrayEnvironment == QLatin1String("aligned")) &&
            cell && cell->children.size() == 1)
          cell = cell->children.front().get();
        qreal currentCellHeight = cssNodeHeight(cell, scale);
        if (const MathRenderNode* cellSemantic = primarySemanticNode(cell);
            cellSemantic && cellSemantic->semanticKind == MathSemanticKind::Fraction &&
            cellSemantic->fractionStyleSize > 0) {
          currentCellHeight += std::floor(
              OpenTypeMathFont::instance().constants().fractionRuleThickness *
              cellSemantic->fractionSizeMultiplier);
        }
        cellHeight = std::max(cellHeight, currentCellHeight);
        plainDescender = plainDescender || hasPlainLatinDescender(cell);
      }
    }
    if (array->arrayEnvironment == QLatin1String("cases") ||
        array->arrayEnvironment == QLatin1String("aligned")) {
      const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
      qreal rowHeight = OpenTypeMathFont::instance().pixelSize() * kMathTableRowHeightEm;
      bool operatorOverflow = false;
      bool scriptOverflow = false;
      for (const auto& column : table->children) {
        const MathRenderNode* rows = columnRows(column.get(), array->rows);
        if (!rows || row >= static_cast<int>(rows->children.size())) continue;
        const MathRenderNode* cell = rows->children[static_cast<size_t>(row)].get();
        operatorOverflow = operatorOverflow || hasMathMlOperatorOverflow(cell);
        scriptOverflow = scriptOverflow ||
            hasNestedSemantic(cell, MathSemanticKind::SupSub);
      }
      if (operatorOverflow) rowHeight += constants.fractionRuleThickness;
      if (scriptOverflow)
        rowHeight += constants.spaceAfterScript + constants.fractionRuleThickness;
      height += rowHeight;
    } else {
      height += cellHeight + OpenTypeMathFont::instance().pixelSize() *
                             (kMathTableCellVerticalPaddingEm +
                              (plainDescender ? kMathTableDescenderExpansionEm : 0.0));
    }
  }
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
  const qreal radicalWidth = widthVariant
      ? widthVariant->advance + constants.radicalRuleThickness / 2.0
      : font.pixelSize();
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
  if (node->semanticKind == MathSemanticKind::Radical) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const MathFontConstants& constants = font.constants();
    const MathRenderNode* radicalBox = node->radicalIndex
        ? nestedSemanticNode(node, MathSemanticKind::Radical) : node;
    const MathRenderNode* body = radicalBody(radicalBox);
    if (!body) return result;
    const GlyphInkExtents bodyInk = glyphInkExtents(body, fontScale);
    qreal bodyHeight = std::ceil(bodyInk.top + bodyInk.bottom);
    if (const auto glyph = nativeGlyphBox(singleSymbol(body)))
      bodyHeight = std::round(glyph->height * fontScale);
    else if (symbolCount(body) > 1)
      bodyHeight += 1.0;
    const qreal target = bodyInk.top + bodyInk.bottom +
                         constants.radicalVerticalGap +
                         constants.radicalRuleThickness;
    const auto variant = font.verticalVariant(QString(QChar(0x221A)), target);
    if (!variant) return bodyInk;
    const qreal bodyAscent = std::min(bodyHeight, bodyInk.top);
    result.top = std::max(
        bodyAscent, bodyInk.top + constants.radicalVerticalGap +
                        constants.radicalRuleThickness +
                        constants.radicalExtraAscender);
    result.bottom = std::max(
        bodyHeight - bodyAscent,
        variant->extent + constants.radicalExtraAscender - result.top);
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
    return extents.top + extents.bottom;
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
    const MathRenderNode* body = radicalBody(node->radicalIndex
        ? nestedSemanticNode(node, MathSemanticKind::Radical) : node);
    const qreal required = cssNodeHeight(body, scale) + constants.radicalVerticalGap +
                           constants.radicalRuleThickness + constants.radicalExtraAscender;
    const auto variant = font.verticalVariant(QString(QChar(0x221A)), required);
    if (!variant) return std::ceil((node->height + node->depth) * scale);
    return variant->extent + constants.radicalExtraAscender;
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
      // glyphInkExtents() already returns the complete MathML radical line
      // box, including the body row's operator leading.
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

qreal braceAccentCssHeight(const MathRenderNode* container,
                           const MathRenderNode* accent,
                           qreal scale) {
  if (!container || !accent || accent->children.size() < 2) return 0.0;
  const bool over = accent->accentKind == MathAccentKind::OverBrace;
  const MathRenderNode* body = over ? accent->children.back().get()
                                    : accent->children.front().get();
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
  qreal bodyHeight = bodyInk.top + bodyInk.bottom;
  if (hasMathMlOperatorOverflow(body))
    bodyHeight += std::floor(constants.fractionRuleThickness);
  bodyHeight = std::ceil(bodyHeight);
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
      const qreal bodyHeight = body ? (body->height + body->depth) * scale : 0.0;
      const qreal requiredExtent = bodyHeight + constants.radicalVerticalGap +
          constants.radicalRuleThickness + constants.radicalExtraAscender;
      const auto heightVariant = mathFont.verticalVariant(QString(QChar(0x221A)), requiredExtent);
      root.width = radicalCssWidth(semantic, scale);
      root.advance = heightVariant
          ? cssNodeWidth(body, scale) + heightVariant->advance - constants.spaceAfterScript +
                (semantic->radicalIndex ? constants.radicalKernBeforeDegree : 0.0)
          : root.width;
      if (root.height == 0.0 && heightVariant) {
        qreal extra = semantic->radicalIndex || heightVariant->extent > 30.0
            ? constants.radicalExtraAscender
            : constants.radicalRuleThickness / 2.0;
        if (nestedSemanticNode(semantic, MathSemanticKind::Array))
          extra += constants.radicalExtraAscender -
                   constants.radicalRuleThickness / 2.0;
        root.height = heightVariant->extent + extra;
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
  GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
  qreal bodyHeight = bodyInk.top + bodyInk.bottom;
  if (hasMathMlOperatorOverflow(body))
    bodyHeight += std::floor(constants.fractionRuleThickness);
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

void collectImmediateFractions(const MathRenderNode* node,
                               QVector<const MathRenderNode*>* fractions) {
  if (!node || !fractions) return;
  if (node->semanticKind == MathSemanticKind::Fraction) {
    fractions->push_back(node);
    return;
  }
  for (const auto& child : node->children)
    collectImmediateFractions(child.get(), fractions);
}

void collectImmediateScripts(const MathRenderNode* node,
                             QVector<const MathRenderNode*>* scripts) {
  if (!node || !scripts) return;
  if (node->semanticKind == MathSemanticKind::SupSub) {
    scripts->push_back(node);
    return;
  }
  if (node->semanticKind == MathSemanticKind::Fraction ||
      node->semanticKind == MathSemanticKind::Radical)
    return;
  for (const auto& child : node->children)
    collectImmediateScripts(child.get(), scripts);
}

void collectImmediateRadicals(const MathRenderNode* node,
                              QVector<const MathRenderNode*>* radicals) {
  if (!node || !radicals) return;
  if (node->semanticKind == MathSemanticKind::Radical) {
    radicals->push_back(node);
    return;
  }
  if (node->semanticKind == MathSemanticKind::Fraction ||
      node->semanticKind == MathSemanticKind::SupSub)
    return;
  for (const auto& child : node->children)
    collectImmediateRadicals(child.get(), radicals);
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
    QRectF containingRect, qreal renderScale) {
  if (!radical || radical->semanticKind != MathSemanticKind::Radical)
    return std::nullopt;
  const MathRenderNode* radicalBox = radical->radicalIndex
      ? nestedSemanticNode(radical, MathSemanticKind::Radical) : radical;
  const MathRenderNode* body = radicalBody(radicalBox);
  if (!body) return std::nullopt;

  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const MathFontConstants& constants = font.constants();
  const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
  const qreal bodyWidth = cssNodeWidth(body, renderScale);
  qreal bodyHeight = std::ceil(bodyInk.top + bodyInk.bottom);
  if (const auto glyph = nativeGlyphBox(singleSymbol(body)))
    bodyHeight = std::round(glyph->height);
  else if (symbolCount(body) > 1)
    bodyHeight += 1.0;
  const qreal target = bodyInk.top + bodyInk.bottom +
                       constants.radicalVerticalGap +
                       constants.radicalRuleThickness;
  const auto variant = font.verticalVariant(QString(QChar(0x221A)), target);
  if (!variant) return std::nullopt;

  const qreal bodyAscent = std::min(bodyHeight, bodyInk.top);
  const qreal lineAscent = std::max(
      bodyAscent, bodyInk.top + constants.radicalVerticalGap +
                      constants.radicalRuleThickness +
                      constants.radicalExtraAscender);
  const qreal lineDescent = std::max(
      bodyHeight - bodyAscent,
      variant->extent + constants.radicalExtraAscender - lineAscent);
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
  result.body = QRectF(left + variant->advance,
                       top + lineAscent - bodyAscent,
                       bodyWidth, bodyHeight);
  result.rule = QRectF(result.body.left(),
                       top + constants.radicalExtraAscender,
                       bodyWidth, constants.radicalRuleThickness);
  result.glyph = QRectF(left, top + constants.radicalExtraAscender,
                        variant->advance, variant->extent);
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

std::optional<MathCssFractionOperation> buildFractionOperation(
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
  const auto includeImmediateFractionHeight = [&](const MathRenderNode* row,
                                                   qreal height) {
    QVector<const MathRenderNode*> nestedFractions;
    collectImmediateFractions(row, &nestedFractions);
    for (const MathRenderNode* nested : nestedFractions)
      height = std::max(height, nestedFractionCssHeight(
          nested, cssRootFontPixelSize, renderScale));
    return snapLayoutUnit(height);
  };
  numeratorHeight = includeImmediateFractionHeight(
      metrics->numerator, numeratorHeight);
  denominatorHeight = includeImmediateFractionHeight(
      metrics->denominator, denominatorHeight);
  const auto includeImmediateScriptHeight = [&](const MathRenderNode* row,
                                                 qreal height) {
    QVector<const MathRenderNode*> scripts;
    collectImmediateScripts(row, &scripts);
    const QRectF intrinsicRow(0.0, 0.0, cssNodeWidth(row, renderScale), 0.0);
    for (const MathRenderNode* script : scripts) {
      const auto child = buildScriptOperation(
          script, row, intrinsicRow, renderScale);
      if (!child) continue;
      height = std::max({height,
                         child->lineAscent + child->lineDescent,
                         child->base.bottom(),
                         child->superscript.bottom(),
                         child->subscript.bottom()});
    }
    return height;
  };
  numeratorHeight = includeImmediateScriptHeight(
      metrics->numerator, numeratorHeight);
  denominatorHeight = includeImmediateScriptHeight(
      metrics->denominator, denominatorHeight);
  const auto includeImmediateRadicalHeight = [&](const MathRenderNode* row,
                                                  qreal height) {
    QVector<const MathRenderNode*> radicals;
    collectImmediateRadicals(row, &radicals);
    const QRectF intrinsicRow(0.0, 0.0, cssNodeWidth(row, renderScale), 0.0);
    for (const MathRenderNode* radical : radicals) {
      const auto child = buildRadicalOperation(
          radical, row, intrinsicRow, renderScale);
      if (!child) continue;
      height = std::max({height,
                         child->lineAscent + child->lineDescent,
                         child->glyph.bottom(),
                         child->rule.bottom(),
                         child->body.bottom()});
    }
    return snapLayoutUnit(height);
  };
  numeratorHeight = includeImmediateRadicalHeight(
      metrics->numerator, numeratorHeight);
  denominatorHeight = includeImmediateRadicalHeight(
      metrics->denominator, denominatorHeight);
  MathCssFractionOperation operation;
  operation.numeratorNode = metrics->numerator;
  operation.denominatorNode = metrics->denominator;
  operation.nested = nested;
  MathCssFractionBox& result = operation.box;
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
    QVector<const MathRenderNode*> nestedFractions;
    collectImmediateFractions(row, &nestedFractions);
    for (const MathRenderNode* nested : nestedFractions) {
      const qreal nestedLeft = rowRect.left() + cssNodeOffset(
          row, nested, renderScale).value_or(0.0);
      const qreal nestedFractionHeight = nestedFractionCssHeight(
          nested, cssRootFontPixelSize, renderScale);
      qreal nestedHeight = nestedFractionHeight;
      qreal delimiterTarget = nestedFractionHeight;
      if (nested->fractionHasBarLine) {
        delimiterTarget += nested->fractionLineThicknessEm >= 0.0
            ? nested->fractionLineThicknessEm * cssRootFontPixelSize *
                  nested->fractionSizeMultiplier
            : OpenTypeMathFont::instance().constants().fractionRuleThickness *
                  nested->fractionSizeMultiplier;
      }
      nestedHeight = std::max(nestedHeight, std::ceil(
          fractionDelimiterExtent(nested, delimiterTarget)));
      const qreal nestedTop = rowRect.top() +
          (rowRect.height() - nestedHeight) / 2.0;
      if (auto child = buildFractionOperation(
              nested, nestedLeft, nestedTop, nestedHeight,
              renderScale, cssRootFontPixelSize, true))
        operation.children.push_back(std::move(*child));
    }
  };
  appendChildren(metrics->numerator, result.numerator);
  appendChildren(metrics->denominator, result.denominator);
  const auto appendScripts = [&](const MathRenderNode* row, QRectF rowRect) {
    QVector<const MathRenderNode*> scripts;
    collectImmediateScripts(row, &scripts);
    for (const MathRenderNode* script : scripts) {
      if (auto child = buildScriptOperation(
              script, row, rowRect, renderScale))
        operation.scripts.push_back(std::move(*child));
    }
  };
  appendScripts(metrics->numerator, result.numerator);
  appendScripts(metrics->denominator, result.denominator);
  const auto appendRadicals = [&](const MathRenderNode* row, QRectF rowRect) {
    QVector<const MathRenderNode*> radicals;
    collectImmediateRadicals(row, &radicals);
    for (const MathRenderNode* radical : radicals) {
      if (auto child = buildRadicalOperation(
              radical, row, rowRect, renderScale))
        operation.radicals.push_back(std::move(*child));
    }
  };
  appendRadicals(metrics->numerator, result.numerator);
  appendRadicals(metrics->denominator, result.denominator);

  const auto rowLineAscent = [&](const MathRenderNode* row, QRectF rowRect) {
    for (const MathCssFractionOperation& child : operation.children) {
      if (child.box.container.intersects(rowRect))
        return child.box.container.top() - rowRect.top() + child.lineAscent;
    }
    for (const MathCssScriptOperation& child : operation.scripts) {
      if (child.container.intersects(rowRect))
        return child.container.top() - rowRect.top() + child.lineAscent;
    }
    for (const MathCssRadicalOperation& child : operation.radicals) {
      if (child.container.intersects(rowRect))
        return child.container.top() - rowRect.top() + child.lineAscent;
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
    operation.lineAscent = std::max({
        metrics->numeratorShift + numeratorAscent,
        -metrics->denominatorShift + denominatorAscent,
        constants.axisHeight + metrics->ruleThickness / 2.0});
    const qreal ruleCenter = fractionTop + operation.lineAscent -
                             constants.axisHeight;
    result.rule = QRectF(fractionLeft + 1.0,
                         ruleCenter - metrics->ruleThickness / 2.0,
                         contentWidth - 2.0, metrics->ruleThickness);
  } else {
    operation.lineAscent = metrics->extents.top;
  }
  return operation;
}

}  // namespace

std::optional<MathCssFractionOperation> layoutMathMlFractionOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  if (!layout.valid() || renderFontPixelSize <= 0.0 ||
      cssRootFontPixelSize <= 0.0)
    return std::nullopt;
  const qreal renderScale = cssRootFontPixelSize / renderFontPixelSize;
  const MathRenderNode* fraction = primarySemanticNode(layout.root.get());
  if (!fraction || fraction->semanticKind != MathSemanticKind::Fraction)
    return std::nullopt;
  const MathCssBox root = layoutMathMlCssBox(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  const qreal fractionOffset = cssNodeOffset(
      layout.root.get(), fraction, renderScale).value_or(0.0);
  return buildFractionOperation(
      fraction, fractionOffset, 0.0, root.height, renderScale,
      cssRootFontPixelSize, false);
}

std::optional<MathCssFractionBox> layoutMathMlFractionBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  const auto operation = layoutMathMlFractionOperations(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  return operation ? std::optional<MathCssFractionBox>{operation->box}
                   : std::nullopt;
}

}  // namespace muffin::math
