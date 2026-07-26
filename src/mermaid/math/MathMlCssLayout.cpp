#include "mermaid/math/MathMlCssLayout.h"

#include "mermaid/MermaidFontRegistry.h"
#include "math/OpenTypeMathFont.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <QJsonArray>

namespace muffin::math {
namespace {

constexpr qreal kChromiumMathAxisOffsetPx = 6.0;
constexpr qreal kMathTableRowHeightEm = 1.22265625;
constexpr qreal kMathTableDescenderExpansionEm = 0.25;
constexpr qreal kMathTableTextDescenderExpansionEm = 3.0 / 16.0;
constexpr qreal kMathTableExtraColumnCorrectionPx = 1.8;
constexpr qreal kMathTableCellVerticalPaddingEm = 0.47265625;
constexpr qreal kMathTableCellHorizontalPaddingEm = 0.8;
constexpr qreal kMathMlThickSpaceEm = 5.0 / 18.0;
constexpr qreal kMathMlMiddleSpaceEm = 0.05;

qreal snapEighth(qreal value) {
  return std::round(value * 8.0) / 8.0;
}

qreal snapLayoutUnit(qreal value) {
  return std::round(value * 64.0) / 64.0;
}

qreal mathMlInlineAscent(qreal fontScale = 1.0) {
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QRawFont raw = font.rasterFont(fontScale);
  const qreal lineHeight = font.pixelSize() * kMathTableRowHeightEm *
                           fontScale;
  const qreal leading = std::max<qreal>(
      0.0, lineHeight - raw.ascent() - raw.descent());
  return raw.ascent() + leading / 2.0;
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
bool hasTextModeDescender(const MathRenderNode* node);
std::optional<MathCssScriptOperation> buildScriptOperation(
    const MathRenderNode* script, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale);
std::optional<MathCssVerticalGlyphOperation> buildVerticalGlyphOperation(
    const QString& delimiter, QRectF target, bool normalizeDelimiter);
qreal fractionMathMlHeight(const MathRenderNode* fraction, qreal cssFontSize,
                           qreal renderScale);
qreal nestedFractionCssHeight(const MathRenderNode* fraction,
                              qreal cssRootFontPixelSize,
                              qreal renderScale);
qreal arrayCssHeight(const MathRenderNode* array, qreal scale);
const MathRenderNode* singleSymbol(const MathRenderNode* node);
const MathRenderNode* firstKind(const MathRenderNode* node,
                                MathRenderKind kind);
QString basicAccentCharacter(const MathRenderNode* accent);
qreal rootLeftRightFenceExtent(const MathRenderNode* node,
                               const MathRenderNode* rootSemantic,
                               qreal minimumExtent);
bool containsNode(const MathRenderNode* root, const MathRenderNode* target);
const MathRenderNode* indexedRadicalDegree(
    const MathRenderNode* root, const MathRenderNode* radicalBox);

struct HorizontalAccentSelection {
  std::optional<MathGlyphVariant> fixed;
  std::optional<MathGlyphAssembly> assembly;
  std::optional<MathShapedTextRun> text;

  qreal height() const {
    if (text) return std::ceil(text->inkBounds.height());
    if (fixed) return std::ceil(fixed->advance);
    return assembly ? std::ceil(assembly->inkBounds.height()) : 0.0;
  }

  qreal rasterHeight() const {
    if (!fixed) return height();
    const qreal inkHeight = OpenTypeMathFont::instance()
                                .rasterGlyphBounds(fixed->glyphIndex)
                                .height();
    return std::ceil(std::max(fixed->advance, inkHeight));
  }
};

HorizontalAccentSelection selectHorizontalAccent(
    const QString& character, qreal target, bool allowAssembly = false) {
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  HorizontalAccentSelection result;
  if (character.size() != 1) {
    result.text = font.shapeMathMlText(character);
    return result;
  }
  const auto fixed = font.horizontalVariant(
      character, allowAssembly ? target : 0.0);
  if (!allowAssembly) {
    result.fixed = fixed;
    return result;
  }
  const bool fixedReachesTarget = fixed && fixed->extent >= target;
  const auto assembly = fixedReachesTarget
      ? std::optional<MathGlyphAssembly>{}
      : font.horizontalAssemblyParts(character, target);
  if (fixed && (fixedReachesTarget || !assembly)) result.fixed = fixed;
  else result.assembly = assembly;
  return result;
}

QString mathDelimiterCharacter(const QString& delimiter) {
  if (delimiter == QLatin1String("\\lbrace")) return QStringLiteral("{");
  if (delimiter == QLatin1String("\\rbrace")) return QStringLiteral("}");
  if (delimiter == QLatin1String("|") ||
      delimiter == QLatin1String("\\vert") ||
      delimiter == QLatin1String("\\lvert") ||
      delimiter == QLatin1String("\\rvert"))
    return QString(QChar(0x2223));
  if (delimiter == QString(QChar(0x2016)) ||
      delimiter == QLatin1String("\\|") ||
      delimiter == QLatin1String("\\Vert") ||
      delimiter == QLatin1String("\\lVert") ||
      delimiter == QLatin1String("\\rVert"))
    return QString(QChar(0x2225));
  return delimiter;
}

QString mathMiddleDelimiterCharacter(const QString& delimiter) {
  if (delimiter == QLatin1String("|")) return delimiter;
  return mathDelimiterCharacter(delimiter);
}

QRectF middleDelimiterLayoutBounds(const QString& delimiter,
                                   qreal fontScale) {
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const auto glyph = font.glyph(mathMiddleDelimiterCharacter(delimiter));
  if (!glyph || fontScale <= 0.0) return {};
  const QRectF& ink = glyph->inkBounds;
  // Match the baseline-relative pixel rows used by the browser's <mo> box
  // without feeding backend-specific hinted QRawFont bounds into layout.
  const qreal top = std::floor(ink.top() * fontScale);
  const qreal bottom = std::round(ink.bottom() * fontScale);
  return QRectF(ink.x() * fontScale, top, ink.width() * fontScale,
                std::max<qreal>(0.0, bottom - top));
}

qreal arrayDelimiterWidth(const QString& delimiter, qreal targetHeight) {
  if (delimiter.isEmpty() || delimiter == QLatin1String(".")) return 0.0;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QString character = mathDelimiterCharacter(delimiter);
  const qreal operatorPadding =
      character == QString(QChar(0x2223)) ||
              character == QString(QChar(0x2225))
          ? snapLayoutUnit(font.pixelSize() * kMathMlThickSpaceEm)
          : 0.0;
  const auto assembly = font.verticalAssembly(character, targetHeight);
  const auto fixed = font.verticalVariant(
      character, std::numeric_limits<qreal>::max());
  qreal width = 0.0;
  if (assembly && fixed) {
    if (character == QLatin1String("{") || character == QLatin1String("}"))
      width = std::max(assembly->advance, fixed->advance);
    else
      width = fixed->advance;
  } else if (assembly) {
    width = assembly->advance;
  } else if (fixed) {
    width = fixed->advance;
  } else if (const auto base = font.glyph(character)) {
    width = base->advance;
  }
  return width + 2.0 * operatorPadding;
}

const MathRenderNode* middleDelimiterMarker(const MathRenderNode* node,
                                            bool root = true) {
  if (!node) return nullptr;
  if (!node->middleDelimiter.isEmpty()) return node;
  if (!root && node->kind == MathRenderKind::LeftRight) return nullptr;
  for (const auto& child : node->children)
    if (const MathRenderNode* marker =
            middleDelimiterMarker(child.get(), false))
      return marker;
  return nullptr;
}

const MathRenderNode* indexedRadicalBox(const MathRenderNode* node) {
  if (!node) return nullptr;
  const MathRenderNode* best = nullptr;
  for (const auto& child : node->children) {
    if (!child) continue;
    const MathRenderNode* candidate = child->semanticKind ==
            MathSemanticKind::Radical
        ? child.get()
        : indexedRadicalBox(child.get());
    if (candidate &&
        (!best || mathStyleScale(candidate) > mathStyleScale(best)))
      best = candidate;
  }
  return best;
}

bool findNodePath(const MathRenderNode* current,
                  const MathRenderNode* target,
                  const QString& currentPath,
                  QString* result) {
  if (!current || !target) return false;
  if (current == target) {
    *result = currentPath;
    return true;
  }
  for (size_t index = 0; index < current->children.size(); ++index) {
    if (findNodePath(current->children[index].get(), target,
                     currentPath + QLatin1Char('/') +
                         QString::number(index),
                     result))
      return true;
  }
  return false;
}

QString nodePath(const MathRenderNode* root, const MathRenderNode* target) {
  QString result;
  return findNodePath(root, target, QStringLiteral("$"), &result)
      ? result : QStringLiteral("$");
}

QString expectedMathMlTag(const MathRenderNode* node) {
  if (!node) return {};
  switch (node->semanticKind) {
    case MathSemanticKind::Fraction: return QStringLiteral("mfrac");
    case MathSemanticKind::Radical:
      return node->radicalIndex ? QStringLiteral("mroot")
                                : QStringLiteral("msqrt");
    case MathSemanticKind::SupSub:
      switch (node->scriptKind) {
        case MathScriptKind::Superscript: return QStringLiteral("msup");
        case MathScriptKind::Subscript: return QStringLiteral("msub");
        case MathScriptKind::SubSup: return QStringLiteral("msubsup");
        case MathScriptKind::None: break;
      }
      return QStringLiteral("mrow");
    case MathSemanticKind::Array: return QStringLiteral("mtable");
    case MathSemanticKind::None: break;
  }
  if (node->kind == MathRenderKind::Accent)
    return node->accentKind == MathAccentKind::Under ||
                   node->accentKind == MathAccentKind::Underline ||
                   node->accentKind == MathAccentKind::UnderBrace
        ? QStringLiteral("munder") : QStringLiteral("mover");
  if (node->kind == MathRenderKind::LeftRight ||
      node->kind == MathRenderKind::Span)
    return QStringLiteral("mrow");
  return {};
}

qreal leftRightBodyCssWidth(const MathRenderNode* node, qreal scale,
                            bool root = true) {
  if (!node) return 0.0;
  if (!node->middleDelimiter.isEmpty()) return cssNodeWidth(node, scale);
  if (!root && node->kind == MathRenderKind::LeftRight)
    return cssNodeWidth(node, scale);
  if (node->semanticKind != MathSemanticKind::None)
    return cssNodeWidth(node, scale);
  if (node->children.empty()) return cssNodeWidth(node, scale);
  if (node->kind != MathRenderKind::Span)
    return cssNodeWidth(node, scale);
  qreal width = 0.0;
  for (const auto& child : node->children)
    width += leftRightBodyCssWidth(child.get(), scale, false);
  return width;
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
    width += columnWidth +
             cssFontSize * kMathTableCellHorizontalPaddingEm;
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

bool hasMathNumberToken(const MathRenderNode* node) {
  if (!node) return false;
  if (node->kind == MathRenderKind::Symbol && !node->text.isEmpty() &&
      std::all_of(node->text.cbegin(), node->text.cend(),
                  [](QChar character) { return character.isNumber(); }))
    return true;
  for (const auto& child : node->children)
    if (hasMathNumberToken(child.get())) return true;
  return false;
}

bool hasOperatorKind(const MathRenderNode* node, MathOperatorKind kind) {
  if (!node) return false;
  if (node->operatorKind == kind) return true;
  for (const auto& child : node->children)
    if (hasOperatorKind(child.get(), kind)) return true;
  return false;
}

qreal embeddedBraceStyleReduction(const MathRenderNode* node) {
  const MathRenderNode* accent = firstKind(node, MathRenderKind::Accent);
  if (!accent || (accent->accentKind != MathAccentKind::UnderBrace &&
                  accent->accentKind != MathAccentKind::OverBrace))
    return 0.0;
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const qreal gap = accent->accentKind == MathAccentKind::UnderBrace
      ? constants.underbarVerticalGap : constants.overbarVerticalGap;
  const qreal rule = accent->accentKind == MathAccentKind::UnderBrace
      ? constants.underbarRuleThickness : constants.overbarRuleThickness;
  const qreal fullHeight = std::round(gap + 2.0 * rule);
  return fullHeight -
         std::floor(fullHeight * constants.scriptPercentScaleDown);
}

bool hasUnderBrace(const MathRenderNode* node) {
  const MathRenderNode* accent = firstKind(node, MathRenderKind::Accent);
  return accent && accent->accentKind == MathAccentKind::UnderBrace;
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
    bool textModeDescender = false;
    bool operatorOverflow = false;
    bool scriptOverflow = false;
    bool underAccentOverflow = false;
    bool structuredCell = false;
    for (int column = 0; column < array->columns; ++column) {
      const MathRenderNode* cell = arrayCellNode(array, table, row, column);
      structuredCell = structuredCell || primarySemanticNode(cell) ||
                       firstKind(cell, MathRenderKind::Accent);
      qreal currentHeight = cssNodeHeight(cell, scale);
      if (const MathRenderNode* semantic = primarySemanticNode(cell);
          semantic && semantic->semanticKind == MathSemanticKind::Fraction &&
          semantic->fractionStyleSize > 0) {
        currentHeight = nestedFractionCssHeight(
            semantic, cssFontSize, scale);
        currentHeight = std::max(currentHeight, std::ceil(
            rootLeftRightFenceExtent(cell, semantic, currentHeight)));
      }
      if (const MathRenderNode* semantic = primarySemanticNode(cell);
          semantic && semantic->semanticKind == MathSemanticKind::Radical &&
          hasNestedSemantic(semantic, MathSemanticKind::SupSub)) {
        const MathFontConstants& constants =
            OpenTypeMathFont::instance().constants();
        currentHeight -= constants.radicalExtraAscender -
                         constants.radicalRuleThickness / 2.0;
      }
      const qreal braceReduction = embeddedBraceStyleReduction(cell);
      currentHeight -= braceReduction;
      underAccentOverflow = underAccentOverflow ||
                            (braceReduction > 0.0 && hasUnderBrace(cell));
      contentHeight = std::max(contentHeight, currentHeight);
      plainDescender = plainDescender || hasPlainLatinDescender(cell);
      textModeDescender = textModeDescender || hasTextModeDescender(cell);
      operatorOverflow = operatorOverflow || hasMathMlOperatorOverflow(cell);
      scriptOverflow = scriptOverflow ||
          hasNestedSemantic(cell, MathSemanticKind::SupSub);
    }
    const bool singleRowTableLine = array->rows == 1 &&
                                    array->columns > 1 &&
                                    !structuredCell;
    if (fixedLineRows || singleRowTableLine) {
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
          cssFontSize *
              (kMathTableCellVerticalPaddingEm +
               ((plainDescender || underAccentOverflow)
                    ? kMathTableDescenderExpansionEm
                    : textModeDescender && array->columns > 1
                        ? kMathTableTextDescenderExpansionEm : 0.0));
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

qreal radicalDegreePaintWidth(const MathRenderNode* degree, qreal scale) {
  const MathRenderNode* semantic = primarySemanticNode(degree);
  if (!semantic)
    return cssNodeWidth(degree, scale);
  const MathFontConstants& constants =
      OpenTypeMathFont::instance().constants();
  if (semantic->semanticKind == MathSemanticKind::Fraction) {
    const qreal fractionWidth = fractionCssWidth(semantic, scale);
    return 2.0 + std::max<qreal>(0.0, fractionWidth - 2.0) *
                     constants.scriptPercentScaleDown;
  }
  if (semantic->semanticKind == MathSemanticKind::SupSub &&
      semantic->children.size() == 2) {
    const qreal base = cssNodeWidth(semantic->children.front().get(), scale);
    const qreal scripts = cssNodeWidth(semantic->children.back().get(), scale) *
                          constants.scriptPercentScaleDown;
    return snapEighth(base + scripts +
                      constants.spaceAfterScript * mathStyleScale(semantic));
  }
  return cssNodeWidth(degree, scale);
}

qreal radicalDegreeAdvanceWidth(const MathRenderNode* degree, qreal scale) {
  const MathRenderNode* semantic = primarySemanticNode(degree);
  const qreal width = cssNodeWidth(degree, scale);
  if (!semantic) return width;
  const MathFontConstants& constants =
      OpenTypeMathFont::instance().constants();
  if (semantic->semanticKind == MathSemanticKind::Fraction)
    return width * constants.scriptScriptPercentScaleDown;
  if (semantic->semanticKind == MathSemanticKind::SupSub &&
      semantic->children.size() == 2) {
    return std::max<qreal>(
        0.0, radicalDegreePaintWidth(degree, scale) -
                 constants.radicalKernBeforeDegree);
  }
  if (semantic->semanticKind == MathSemanticKind::Radical)
    return std::max<qreal>(
        0.0, width - constants.spaceAfterScript -
                 constants.radicalRuleThickness / 2.0 *
                     mathStyleScale(semantic));
  return width;
}

qreal radicalDegreePaintHeight(const MathRenderNode* degree, qreal scale) {
  qreal height = cssNodeHeight(degree, scale);
  const MathRenderNode* semantic = primarySemanticNode(degree);
  if (semantic && semantic->semanticKind == MathSemanticKind::Fraction) {
    const qreal ruleLeading = OpenTypeMathFont::instance()
                                  .constants()
                                  .fractionRuleThickness *
                              semantic->fractionSizeMultiplier / 4.0;
    height -= ruleLeading;
  } else if (semantic &&
             semantic->semanticKind == MathSemanticKind::Radical) {
    height += OpenTypeMathFont::instance()
                  .constants()
                  .radicalVerticalGap / 2.0 * mathStyleScale(semantic);
  } else if (semantic &&
             semantic->semanticKind == MathSemanticKind::SupSub) {
    const QRectF intrinsicRect(
        0.0, 0.0, radicalDegreePaintWidth(degree, scale), 0.0);
    if (const auto operation = buildScriptOperation(
            semantic, nullptr, intrinsicRect, scale))
      height = operation->container.height() *
               OpenTypeMathFont::instance()
                   .constants()
                   .scriptScriptPercentScaleDown;
  }
  return snapLayoutUnit(height);
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
      ? indexedRadicalBox(radical) : radical;
  const MathRenderNode* degree = radical->radicalIndex
      ? indexedRadicalDegree(radical, radicalBox) : nullptr;
  const auto widthVariant = font.verticalVariant(
      QString(QChar(0x221A)), font.pixelSize() * 1.875);
  const qreal styleScale = mathStyleScale(radical);
  const qreal radicalWidth = widthVariant
      ? (widthVariant->advance + constants.radicalRuleThickness / 2.0) *
            styleScale
      : font.pixelSize() * styleScale;
  const qreal degreeAdvance = degree
      ? std::max<qreal>(0.0, constants.radicalKernBeforeDegree +
                                radicalDegreeAdvanceWidth(degree, scale) +
                                constants.radicalKernAfterDegree)
      : 0.0;
  const qreal degreeWidthContribution = degree
      ? constants.radicalKernBeforeDegree + degreeAdvance
      : 0.0;
  return degreeWidthContribution + radicalWidth +
         cssNodeWidth(radicalBody(radicalBox), scale);
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

bool isIntegralOperator(const QString& character) {
  if (character.size() != 1) return false;
  const ushort codepoint = character.front().unicode();
  return (codepoint >= 0x222B && codepoint <= 0x2233) ||
         (codepoint >= 0x2A0B && codepoint <= 0x2A1C);
}

qreal largeOperatorAllocationHeight(const QString& character,
                                    const MathGlyphVariant& variant,
                                    qreal fontScale = 1.0) {
  const qreal mathExtent = std::ceil(variant.extent * fontScale);
  if (isIntegralOperator(character)) return mathExtent;
  const QRectF rasterInk = OpenTypeMathFont::instance().rasterGlyphBounds(
      variant.glyphIndex, fontScale);
  const qreal rasterLineHeight =
      std::ceil(std::max<qreal>(0.0, -rasterInk.top())) +
      std::ceil(std::max<qreal>(0.0, rasterInk.bottom()));
  return std::max(mathExtent, rasterLineHeight);
}

qreal maxLargeOperatorExtent(const MathRenderNode* node) {
  if (node == nullptr) return 0.0;
  qreal extent = 0.0;
  if (node->kind == MathRenderKind::Symbol && node->atomClass == QLatin1String("mop")) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto variant = font.verticalVariant(node->text,
                                              font.constants().displayOperatorMinHeight);
    if (variant)
      extent = isIntegralOperator(node->text)
          ? variant->extent
          : largeOperatorAllocationHeight(node->text, *variant);
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

const MathRenderNode* indexedRadicalDegree(
    const MathRenderNode* root, const MathRenderNode* radicalBox) {
  if (!root || !radicalBox || root == radicalBox ||
      !containsNode(root, radicalBox))
    return nullptr;
  const MathRenderNode* sibling = nullptr;
  const MathRenderNode* pathChild = nullptr;
  for (const auto& child : root->children) {
    if (!child) continue;
    if (containsNode(child.get(), radicalBox)) {
      pathChild = child.get();
    } else if (!sibling &&
               (primarySemanticNode(child.get()) ||
                singleSymbol(child.get()))) {
      sibling = child.get();
    }
  }
  if (const MathRenderNode* nested =
          indexedRadicalDegree(pathChild, radicalBox))
    return nested;
  return sibling;
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

const MathRenderNode* wrappedTextModeRun(const MathRenderNode* node) {
  const MathRenderNode* current = node;
  while (current) {
    if (current->textModeRun) return current;
    if (current->semanticKind != MathSemanticKind::None)
      return nullptr;
    if ((current->kind != MathRenderKind::Span &&
         current->kind != MathRenderKind::VList))
      return nullptr;
    const MathRenderNode* visibleChild = nullptr;
    for (const auto& child : current->children) {
      if (!child || (symbolCount(child.get()) == 0 &&
                     child->semanticKind == MathSemanticKind::None))
        continue;
      if (visibleChild) return nullptr;
      visibleChild = child.get();
    }
    current = visibleChild;
  }
  return nullptr;
}

bool containsTextModeRun(const MathRenderNode* node) {
  if (!node) return false;
  if (node->textModeRun) return true;
  return std::any_of(
      node->children.cbegin(), node->children.cend(),
      [](const std::unique_ptr<MathRenderNode>& child) {
        return containsTextModeRun(child.get());
      });
}

bool collectTextModeRun(const MathRenderNode* node, QString* text,
                        const MathRenderNode** firstSymbol = nullptr) {
  if (!node || !text || node->phantom) return true;
  if (node->kind == MathRenderKind::Span && node->children.empty() &&
      !node->text.isEmpty() &&
      std::all_of(node->text.cbegin(), node->text.cend(),
                  [](QChar character) {
                    return character.isSpace() ||
                           character.category() == QChar::Other_Format;
                  })) {
    *text += node->text;
    return true;
  }
  if (node->kind == MathRenderKind::Symbol) {
    if (node->text.isEmpty()) return true;
    const bool shapingOnly = std::all_of(
        node->text.cbegin(), node->text.cend(), [](QChar character) {
          return character.isSpace() ||
                 character.category() == QChar::Other_Format;
        });
    if (qFuzzyIsNull(node->width) && !shapingOnly) return true;
    if (firstSymbol && !*firstSymbol) *firstSymbol = node;
    *text += node->text;
    return true;
  }
  if (node->semanticKind != MathSemanticKind::None ||
      node->kind != MathRenderKind::Span)
    return false;
  for (const auto& child : node->children)
    if (!collectTextModeRun(child.get(), text, firstSymbol)) return false;
  return true;
}

std::optional<MathShapedText> shapeTextModeRun(const QString& text,
                                               qreal fontScale) {
  return OpenTypeMathFont::instance().shapeMathMlTextWithFallback(
      text, mermaid::MermaidFontRegistry::familyStack(), fontScale);
}

qreal shapedTextCssHeight(const MathShapedText& shaped) {
  if (shaped.fullEmLineBox)
    return std::max(std::round(shaped.fontPixelSize),
                    std::ceil(shaped.inkBounds.height()));
  if (shaped.formatControlledLineBox)
    return std::ceil(std::max<qreal>(0.0, -shaped.inkBounds.top())) +
           std::ceil(std::max<qreal>(0.0, shaped.inkBounds.bottom()));
  return shaped.compoundLineBox
      ? std::ceil(shaped.inkBounds.height())
      : std::round(shaped.inkBounds.height());
}

struct NativeGlyphBox {
  qreal width = 0.0;
  qreal height = 0.0;
};

struct GlyphInkExtents {
  qreal top = 0.0;
  qreal bottom = 0.0;
};

void includeMiddleDelimiterLayoutExtents(const MathRenderNode* node,
                                         GlyphInkExtents* extents,
                                         bool root = true) {
  if (!node || !extents) return;
  if (!node->middleDelimiter.isEmpty()) {
    const QRectF bounds = middleDelimiterLayoutBounds(
        node->middleDelimiter, mathStyleScale(node));
    if (!bounds.isEmpty()) {
      extents->top = std::max(
          extents->top, std::max<qreal>(0.0, -bounds.top()));
      extents->bottom = std::max(
          extents->bottom, std::max<qreal>(0.0, bounds.bottom()));
    }
    return;
  }
  if (!root && node->kind == MathRenderKind::LeftRight) return;
  for (const auto& child : node->children)
    includeMiddleDelimiterLayoutExtents(child.get(), extents, false);
}

GlyphInkExtents shapedTextCssExtents(const MathShapedText& shaped) {
  const qreal inkTop = std::max<qreal>(0.0, -shaped.inkBounds.top());
  const qreal inkBottom = std::max<qreal>(0.0, shaped.inkBounds.bottom());
  const qreal allocation = shapedTextCssHeight(shaped);
  if (shaped.fullEmLineBox) {
    const qreal bottom = std::max(
        inkBottom, std::min(allocation, shaped.fontPixelSize * 0.1));
    return {std::max<qreal>(0.0, allocation - bottom), bottom};
  }
  return {inkTop + std::max<qreal>(
                       0.0, allocation - inkTop - inkBottom),
          inkBottom};
}

bool hasTextModeDescender(const MathRenderNode* node) {
  if (!node) return false;
  if (const MathRenderNode* textRun = wrappedTextModeRun(node)) {
    QString text;
    if (collectTextModeRun(textRun, &text)) {
      if (const auto shaped = shapeTextModeRun(
              text, mathStyleScale(textRun))) {
        const GlyphInkExtents extents = shapedTextCssExtents(*shaped);
        return extents.bottom > shaped->fontPixelSize * 0.1 + 0.01;
      }
    }
  }
  return std::any_of(
      node->children.cbegin(), node->children.cend(),
      [](const std::unique_ptr<MathRenderNode>& child) {
        return hasTextModeDescender(child.get());
      });
}

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
  if (!node->middleDelimiter.isEmpty()) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto glyph = font.glyph(
        mathMiddleDelimiterCharacter(node->middleDelimiter));
    if (!glyph) return result;
    const QRectF ink = font.rasterGlyphBounds(
        glyph->glyphIndex, fontScale);
    result.top = std::max<qreal>(0.0, -ink.top());
    result.bottom = std::max<qreal>(0.0, ink.bottom());
    return result;
  }
  if (const MathRenderNode* textRun = wrappedTextModeRun(node)) {
    QString text;
    if (collectTextModeRun(textRun, &text)) {
      if (const auto shaped = shapeTextModeRun(
              text, mathStyleScale(textRun))) {
        return shapedTextCssExtents(*shaped);
      }
    }
  }
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
        ? indexedRadicalBox(node) : node;
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
  if (node->kind == MathRenderKind::Symbol && node->text.size() > 1 &&
      node->fontClass == QLatin1String("main")) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    if (const auto shaped = font.shapeMathMlText(
            node->text, mathStyleScale(node))) {
      result.top = std::max<qreal>(0.0, -shaped->inkBounds.top());
      result.bottom = std::max<qreal>(0.0, shaped->inkBounds.bottom());
      return result;
    }
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

qreal nativeOperatorPadding(const MathRenderNode* symbol) {
  if (!symbol) return 0.0;
  const qreal styleScale = symbol->mathStyleSize >= 3
      ? OpenTypeMathFont::instance().constants().scriptScriptPercentScaleDown
      : symbol->mathStyleSize >= 2 || symbol->tightSpacing
          ? OpenTypeMathFont::instance().constants().scriptPercentScaleDown
          : 1.0;
  if (symbol->atomClass == QLatin1String("mbin") ||
      symbol->text == QLatin1String("+"))
    return 4.0 / 18.0 * OpenTypeMathFont::instance().pixelSize() *
           styleScale;
  if (symbol->atomClass == QLatin1String("mrel"))
    return 5.0 / 18.0 * OpenTypeMathFont::instance().pixelSize() *
           styleScale;
  if (symbol->atomClass == QLatin1String("mop"))
    return 3.0 / 18.0 * OpenTypeMathFont::instance().pixelSize() *
           styleScale;
  return 0.0;
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
  width += 2.0 * nativeOperatorPadding(symbol);
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
  if (const MathRenderNode* textRun = wrappedTextModeRun(node)) {
    QString text;
    if (collectTextModeRun(textRun, &text)) {
      if (const auto shaped = shapeTextModeRun(
              text, mathStyleScale(textRun)))
        return shaped->advance;
    }
  }
  if (!node->middleDelimiter.isEmpty()) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const qreal styleScale = mathStyleScale(node);
    const auto glyph = font.glyph(
        mathMiddleDelimiterCharacter(node->middleDelimiter));
    return glyph ? (glyph->advance +
                    2.0 * font.pixelSize() * kMathMlMiddleSpaceEm) *
                       styleScale
                 : node->width * scale;
  }
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
    if (const MathRenderNode* brace = node->kind == MathRenderKind::VList
            ? firstKind(node, MathRenderKind::Accent) : nullptr;
        brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                  brace->accentKind == MathAccentKind::OverBrace))
      return cssNodeWidth(brace, scale) * mathStyleScale(node);
  }
  if (node->kind == MathRenderKind::LeftRight) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    size_t firstBody = node->leftDelimiter == QLatin1String(".") ? 0 : 1;
    size_t bodyEnd = node->children.size() -
                     (node->rightDelimiter == QLatin1String(".") ? 0 : 1);
    const MathRenderNode* body = firstBody < bodyEnd
        ? node->children[firstBody].get() : nullptr;
    const qreal targetHeight = std::max({
        (node->height + node->depth) * scale,
        cssNodeHeight(body, scale),
        font.pixelSize() * kMathTableRowHeightEm});
    qreal width = arrayDelimiterWidth(node->leftDelimiter, targetHeight) +
                  arrayDelimiterWidth(node->rightDelimiter, targetHeight);
    for (size_t i = firstBody; i < bodyEnd; ++i) {
      const MathRenderNode* bodyChild = node->children[i].get();
      width += middleDelimiterMarker(bodyChild)
          ? leftRightBodyCssWidth(bodyChild, scale)
          : cssNodeWidth(bodyChild, scale);
    }
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
    if (node->fontClass == QLatin1String("main")) {
      if (const auto shaped = OpenTypeMathFont::instance().shapeMathMlText(
              node->text, mathStyleScale(node)))
        return shaped->advance;
    }
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
  if (const MathRenderNode* textRun = wrappedTextModeRun(node)) {
    QString text;
    if (collectTextModeRun(textRun, &text)) {
      if (const auto shaped = shapeTextModeRun(
              text, mathStyleScale(textRun)))
        return shapedTextCssHeight(*shaped);
    }
  }
  if (!node->middleDelimiter.isEmpty()) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto glyph = font.glyph(
        mathMiddleDelimiterCharacter(node->middleDelimiter));
    if (glyph) {
      const QRectF ink = font.rasterGlyphBounds(
          glyph->glyphIndex, mathStyleScale(node));
      return std::floor(ink.height() + 0.001);
    }
  }
  const MathRenderNode* semantic = primarySemanticNode(node);
  if (semantic && semantic != node) {
    qreal height = cssNodeHeight(semantic, scale);
    if (semantic->semanticKind == MathSemanticKind::Fraction)
      height = std::max(height, std::ceil(rootLeftRightFenceExtent(
                                    node, semantic, height)));
    return height;
  }
  if (node->kind == MathRenderKind::Accent &&
      (node->accentKind == MathAccentKind::Under ||
       node->accentKind == MathAccentKind::Over) &&
      !node->accentCharacter.isEmpty() && node->children.size() >= 2) {
    const bool bodyIsLast = node->accentKind == MathAccentKind::Under;
    const MathRenderNode* body = bodyIsLast
        ? node->children.back().get() : node->children.front().get();
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const qreal target = cssNodeWidth(node, scale);
    if (node->accentUsesNaturalWidth) {
      if (const auto variant = font.horizontalVariant(
              node->accentCharacter, 0.0)) {
        return cssNodeHeight(body, scale) + variant->advance +
               font.constants().overbarExtraAscender;
      }
      if (const auto glyph = font.glyph(node->accentCharacter)) {
        return cssNodeHeight(body, scale) +
               std::ceil(glyph->inkBounds.height()) +
               font.constants().overbarExtraAscender;
      }
    }
    const HorizontalAccentSelection selection = selectHorizontalAccent(
        node->accentCharacter, target);
    const qreal arrowHeight = node->accentKind == MathAccentKind::Over &&
            containsTextModeRun(body)
        ? selection.rasterHeight() : selection.height();
    const MathFontConstants& constants = font.constants();
    return cssNodeHeight(body, scale) + arrowHeight +
           (node->accentKind == MathAccentKind::Under
                ? constants.underbarExtraDescender
                : constants.overbarExtraAscender);
  }
  if (node->semanticKind == MathSemanticKind::SupSub) {
    if (const MathRenderNode* brace = node->kind == MathRenderKind::VList
            ? firstKind(node, MathRenderKind::Accent) : nullptr;
        brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                  brace->accentKind == MathAccentKind::OverBrace))
      return braceAccentCssHeight(node, brace, scale);
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
        ? indexedRadicalBox(node) : node);
    const qreal required = cssNodeHeight(body, scale) -
                           embeddedBraceStyleReduction(body) +
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
    if (node->fontClass == QLatin1String("main")) {
      if (const auto shaped = OpenTypeMathFont::instance().shapeMathMlText(
              node->text, mathStyleScale(node)))
        return std::ceil(shaped->inkBounds.height());
    }
  }
  if (symbolCount(node) == 1) {
    const MathRenderNode* symbol = singleSymbol(node);
    if (const auto glyph = nativeGlyphBox(symbol)) return glyph->height;
    if (symbol && symbol->text.size() > 1 &&
        symbol->fontClass == QLatin1String("main")) {
      if (const auto shaped = OpenTypeMathFont::instance().shapeMathMlText(
              symbol->text, mathStyleScale(symbol)))
        return std::ceil(shaped->inkBounds.height());
    }
  }
  if ((node->kind == MathRenderKind::Span || node->children.size() == 1) &&
      middleDelimiterMarker(node)) {
    qreal height = 0.0;
    for (const auto& child : node->children)
      height = std::max(height, cssNodeHeight(child.get(), scale));
    return height;
  }
  if (node->semanticKind == MathSemanticKind::None &&
      (node->kind == MathRenderKind::Span ||
       node->kind == MathRenderKind::VList) &&
      containsTextModeRun(node)) {
    qreal height = 0.0;
    for (const auto& child : node->children) {
      if (!child || (symbolCount(child.get()) == 0 &&
                     child->semanticKind == MathSemanticKind::None))
        continue;
      height = std::max(height, cssNodeHeight(child.get(), scale));
    }
    return height;
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
  // Browser fraction rows allocate the full <mo> line box, not only its ink.
  includeMiddleDelimiterLayoutExtents(numerator, &numeratorInk);
  includeMiddleDelimiterLayoutExtents(denominator, &denominatorInk);
  const auto includeMathMlRowBox = [cssFontSize, renderScale, styleScale, &constants](
                                        const MathRenderNode* child,
                                        GlyphInkExtents* ink,
                                        bool includeOperatorLeading) {
    if (!child || !ink) return;
    if (const MathRenderNode* textRun = wrappedTextModeRun(child)) {
      QString text;
      if (collectTextModeRun(textRun, &text)) {
        if (const auto shaped = shapeTextModeRun(
                text, mathStyleScale(textRun))) {
          const qreal allocation = shapedTextCssHeight(*shaped);
          ink->top += std::max<qreal>(
              0.0, allocation - ink->top - ink->bottom);
        }
      }
      return;
    }
    if (symbolCount(child) <= 1) return;
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
    if (includeOperatorLeading && hasMathMlOperatorOverflow(child) &&
        !hasMathNumberToken(child))
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
  const auto isScriptRow = [](const MathRenderNode* row) {
    const MathRenderNode* semantic = primarySemanticNode(row);
    return semantic && semantic->semanticKind == MathSemanticKind::SupSub;
  };
  const bool hasScriptRow = isScriptRow(numerator) || isScriptRow(denominator);
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

bool mathMlOperatorIsStretchy(const QString& character) {
  return character != QString(QChar(0x2223)) &&
         character != QString(QChar(0x2225));
}

qreal rootLeftRightFenceExtent(const MathRenderNode* node,
                               const MathRenderNode* rootSemantic,
                               qreal minimumExtent) {
  if (!node) return 0.0;
  if (node->kind == MathRenderKind::LeftRight &&
      primarySemanticNode(node) == rootSemantic) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    qreal extent = 0.0;
    for (const QString& delimiter : {node->leftDelimiter, node->rightDelimiter}) {
      if (delimiter.isEmpty() || delimiter == QLatin1String(".")) continue;
      const QString character = mathDelimiterCharacter(delimiter);
      if (!mathMlOperatorIsStretchy(character)) {
        if (const auto base = font.glyph(character))
          extent = std::max(extent, base->inkBounds.height());
        continue;
      }
      const MathRenderNode* fenceSemantic = primarySemanticNode(node);
      const bool middleFraction = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Fraction &&
          middleDelimiterMarker(fenceSemantic);
      if (middleFraction) {
        extent = std::max(
            extent,
            std::floor(minimumExtent) + kChromiumMathAxisOffsetPx);
        continue;
      }
      const bool middleRadical = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Radical &&
          middleDelimiterMarker(fenceSemantic);
      if (middleRadical) {
        extent = std::max(
            extent,
            std::ceil(minimumExtent) +
                2.0 * std::ceil(font.constants().fractionRuleThickness));
        continue;
      }
      const bool middleScript = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::SupSub &&
          middleDelimiterMarker(fenceSemantic);
      if (middleScript) {
        extent = std::max(
            extent,
            std::ceil(minimumExtent) +
                4.0 * std::ceil(font.constants().fractionRuleThickness) +
                std::ceil(font.constants().fractionRuleThickness / 2.0));
        continue;
      }
      const bool middleArray = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Array &&
          middleDelimiterMarker(fenceSemantic);
      if (middleArray) {
        extent = std::max(
            extent,
            std::ceil(minimumExtent) +
                std::ceil(font.constants().fractionRuleThickness));
        continue;
      }
      const bool textStyleFraction = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Fraction &&
          fenceSemantic->fractionStyleSize > 0;
      if (textStyleFraction) {
        if (const auto variant = font.verticalVariant(
                character, minimumExtent, true))
          extent = std::max(extent, variant->extent);
        continue;
      }
      const bool tallFence =
          minimumExtent > font.pixelSize() * kMathTableRowHeightEm;
      const qreal fenceLeading = 2.0 * std::ceil(
          font.constants().fractionRuleThickness);
      const auto largestFixed = font.verticalVariant(
          character, std::numeric_limits<qreal>::max());
      const bool requiresAssembly = largestFixed &&
          minimumExtent > largestFixed->extent;
      const bool fractionFence = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Fraction;
      const bool textArrayFence = fenceSemantic &&
          fenceSemantic->semanticKind == MathSemanticKind::Array &&
          containsTextModeRun(fenceSemantic);
      const bool curvedFence = character == QLatin1String("(") ||
                               character == QLatin1String(")");
      const bool ceilingTerminal =
          character == QString(QChar(0x2308)) ||
          character == QString(QChar(0x2309));
      const qreal targetExtent = ceilingTerminal
          ? minimumExtent
          : minimumExtent + (requiresAssembly
                ? fractionFence
                    ? 2.0 * font.constants().axisHeight +
                          font.constants().fractionRuleThickness / 2.0
                    : textArrayFence
                        ? curvedFence
                            ? font.constants().fractionRuleThickness
                            : font.constants().fractionRuleThickness / 2.0
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
    return extent;
  }
  qreal extent = 0.0;
  for (const auto& child : node->children)
    extent = std::max(extent, rootLeftRightFenceExtent(
                                  child.get(), rootSemantic, minimumExtent));
  return extent;
}

qreal siblingSemanticHeight(const MathRenderNode* node,
                            const MathRenderNode* primary,
                            qreal scale, qreal cssFontSize) {
  if (!node || node == primary) return 0.0;
  if (node->semanticKind == MathSemanticKind::Fraction)
    return fractionMathMlHeight(node, cssFontSize, scale);
  if (node->semanticKind != MathSemanticKind::None)
    return cssNodeHeight(node, scale);
  qreal height = 0.0;
  for (const auto& child : node->children)
    height = std::max(
        height, siblingSemanticHeight(child.get(), primary, scale,
                                      cssFontSize));
  return height;
}

bool hasSiblingAtomClass(const MathRenderNode* node,
                         const MathRenderNode* primary,
                         QLatin1StringView atomClass) {
  if (!node || node == primary) return false;
  if (node->atomClass == atomClass) return true;
  for (const auto& child : node->children)
    if (hasSiblingAtomClass(child.get(), primary, atomClass)) return true;
  return false;
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
  if (body->kind == MathRenderKind::Accent)
    return cssNodeHeight(body, scale);
  if (const MathRenderNode* nested = firstKind(
          body, MathRenderKind::Accent);
      nested && (nested->accentKind == MathAccentKind::Under ||
                 nested->accentKind == MathAccentKind::Over) &&
      !nested->accentCharacter.isEmpty())
    return cssNodeHeight(nested, scale);
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const MathRenderNode* semantic = primarySemanticNode(body);
  if (!semantic) {
    if (const auto glyph = nativeGlyphBox(singleSymbol(body)))
      return glyph->height;
    const GlyphInkExtents ink = glyphInkExtents(body, 1.0);
    qreal height = ink.top + ink.bottom;
    if (hasMathMlOperatorOverflow(body))
      height += std::floor(constants.fractionRuleThickness);
    return std::ceil(height);
  }

  qreal height = cssNodeHeight(body, scale);
  const bool directSemanticBody =
      symbolCount(body) == symbolCount(semantic);
  if (directSemanticBody &&
      semantic->semanticKind == MathSemanticKind::Fraction &&
      semantic->fractionHasBarLine) {
    height -= constants.fractionRuleThickness *
              semantic->fractionSizeMultiplier;
  } else if (directSemanticBody &&
             semantic->semanticKind == MathSemanticKind::Radical) {
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
  qreal braceHeight = !over && containsTextModeRun(body)
      ? std::ceil(2.0 * rule)
      : std::round(gap + 2.0 * rule);
  braceHeight -= embeddedBraceStyleReduction(body);
  const qreal extra = over ? constants.overbarExtraAscender
                           : constants.underbarExtraDescender;
  qreal height = bodyHeight + gap + braceHeight + extra;
  const MathRenderNode* annotation = nullptr;
  for (const auto& child : container->children) {
    if (child && !containsNode(child.get(), accent)) {
      annotation = child.get();
      break;
    }
  }
  const MathRenderNode* annotationSemantic =
      primarySemanticNode(annotation);
  const bool textModeAnnotation = containsTextModeRun(annotation);
  qreal annotationHeight = textModeAnnotation
      ? cssNodeHeight(annotation, scale)
      : annotationSemantic
      ? cssNodeHeight(annotation, scale)
      : heightOutsideAccent(annotation, accent, scale);
  if (annotationSemantic &&
      annotationSemantic->semanticKind == MathSemanticKind::Fraction &&
      annotationSemantic->fractionHasBarLine)
    annotationHeight -= constants.fractionRuleThickness;
  if (!annotationSemantic && !textModeAnnotation) {
    annotationHeight = std::min(
        annotationHeight, std::ceil(constants.axisHeight));
    if (annotationHeight > 0.0)
      annotationHeight += std::floor(constants.fractionRuleThickness);
  }
  if (annotationHeight > 0.0)
    height += gap + annotationHeight + extra;
  const qreal styleScale = mathStyleScale(container);
  if (styleScale < 1.0) {
    const auto roundedComponent = [styleScale](qreal component) {
      const qreal scaled = component * styleScale;
      return std::ceil(scaled) - scaled;
    };
    height = height * styleScale + roundedComponent(bodyHeight) +
             roundedComponent(braceHeight) +
             roundedComponent(annotationHeight);
    height = snapLayoutUnit(height);
  }
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
          ? indexedRadicalBox(semantic) : semantic;
      const MathRenderNode* degree = semantic->radicalIndex
          ? indexedRadicalDegree(semantic, radicalBox) : nullptr;
      const qreal degreeAdvance = degree
          ? std::max<qreal>(0.0, constants.radicalKernBeforeDegree +
                                    radicalDegreeAdvanceWidth(degree, scale) +
                                    constants.radicalKernAfterDegree)
          : 0.0;
      const MathRenderNode* body = radicalBody(radicalBox);
      const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
      const qreal radicalPadding = constants.radicalVerticalGap +
                                   constants.radicalRuleThickness;
      const qreal recursiveNaturalHeight =
          hasNestedSemantic(body, MathSemanticKind::SupSub) ||
                  hasNestedSemantic(body, MathSemanticKind::Radical)
              ? embeddedBraceStyleReduction(body) > 0.0
                    ? 0.0 : scaledHeight
              : 0.0;
      const qreal requiredExtent = std::max({
          bodyInk.top + bodyInk.bottom + radicalPadding,
          cssNodeHeight(body, scale) + radicalPadding,
          root.height, recursiveNaturalHeight}) +
          (middleDelimiterMarker(body)
               ? kChromiumMathAxisOffsetPx : 0.0);
      const auto heightVariant = mathFont.verticalVariant(QString(QChar(0x221A)), requiredExtent);
      root.width = radicalCssWidth(semantic, scale);
      root.advance = heightVariant
          ? cssNodeWidth(body, scale) + heightVariant->advance - constants.spaceAfterScript +
                (degree ? constants.radicalKernBeforeDegree + degreeAdvance
                        : 0.0)
          : root.width;
      if (heightVariant) {
        qreal extra = (!primarySemanticNode(body) &&
                       !middleDelimiterMarker(body)) ||
                              semantic->radicalIndex ||
                              heightVariant->extent > 30.0 ||
                              containsTextModeRun(body)
            ? constants.radicalExtraAscender
            : constants.radicalRuleThickness / 2.0;
        if (nestedSemanticNode(semantic, MathSemanticKind::Array))
          extra += constants.radicalExtraAscender -
                   constants.radicalRuleThickness / 2.0;
        root.height = std::max(root.height, heightVariant->extent + extra);
      }
      if (root.height == 0.0) root.height = std::ceil(scaledHeight);
      if (degree && heightVariant) {
        const qreal degreeScale = constants.scriptScriptPercentScaleDown;
        qreal degreeHeight = radicalDegreePaintHeight(degree, scale);
        if (!primarySemanticNode(degree))
          degreeHeight = std::round(degreeHeight * degreeScale);
        const qreal degreeBottom = root.height -
            constants.radicalDegreeBottomRaisePercent *
                heightVariant->extent;
        const qreal topOverhang = std::max<qreal>(
            0.0, degreeHeight - degreeBottom);
        root.height = snapLayoutUnit(
            root.height + std::max<qreal>(
                0.0, 2.0 * topOverhang -
                         constants.radicalRuleThickness / 2.0));
      }
      if (enclosingKind(layout.root.get(), semantic,
                        MathRenderKind::LeftRight)) {
        root.width = cssNodeWidth(layout.root.get(), scale);
        root.advance = root.width;
      }
      break;
      }
    case MathSemanticKind::SupSub:
      if (const MathRenderNode* brace = semantic->kind == MathRenderKind::VList
              ? firstKind(semantic, MathRenderKind::Accent) : nullptr;
          brace && (brace->accentKind == MathAccentKind::UnderBrace ||
                    brace->accentKind == MathAccentKind::OverBrace)) {
        root.width = cssNodeWidth(brace, scale);
        for (const auto& child : semantic->children)
          if (child && !containsNode(child.get(), brace))
            root.width = std::max(root.width,
                                  cssNodeWidth(child.get(), scale));
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
      root.advance = root.width;
      {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        if (semantic->children.size() >= 3 &&
            hasAtomClass(semantic, QLatin1StringView("mop"))) {
          if (containsTextModeRun(semantic)) {
            if (const auto operation = buildScriptOperation(
                    semantic, layout.root.get(),
                    QRectF(0.0, 0.0, root.width, root.height), scale)) {
              root.width = operation->container.width() +
                           2.0 * operation->container.left();
              root.advance = root.width;
              root.height = operation->lineAscent + operation->lineDescent;
              break;
            }
          }
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
          root.height = maxLargeOperatorExtent(semantic);
          if (root.scriptKind != MathScriptKind::SubSup)
            root.height = std::ceil(root.height);
          if (root.scriptKind == MathScriptKind::Superscript ||
              root.scriptKind == MathScriptKind::SubSup)
            root.height += constants.superscriptBaselineDropMax;
          if (root.scriptKind == MathScriptKind::Subscript ||
              root.scriptKind == MathScriptKind::SubSup)
            root.height += constants.subscriptBaselineDropMin;
          if (semantic->operatorKind == MathOperatorKind::Limits) {
            const qreal siblingHeight = siblingSemanticHeight(
                layout.root.get(), semantic, scale, cssRootFontPixelSize);
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
      if (semantic && containsTextModeRun(semantic) &&
          semantic->operatorKind != MathOperatorKind::Limits &&
          !firstKind(semantic, MathRenderKind::Accent)) {
        if (const auto operation = buildScriptOperation(
                semantic, layout.root.get(),
                QRectF(0.0, 0.0, root.width, root.height), scale))
          root.height = operation->lineAscent + operation->lineDescent;
      }
      if (semantic && semantic->operatorKind == MathOperatorKind::Limits) {
        const qreal siblingHeight = siblingSemanticHeight(
            layout.root.get(), semantic, scale, cssRootFontPixelSize);
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
      if (semantic && embeddedBraceStyleReduction(semantic) > 0.0) {
        if (const auto operation = buildScriptOperation(
                semantic, layout.root.get(),
                QRectF(0.0, 0.0, root.width, root.height), scale)) {
          QRectF extent = operation->base;
          for (const QRectF component : {operation->superscript,
                                         operation->subscript})
            if (!component.isEmpty()) extent = extent.united(component);
          root.height = std::max(root.height, extent.height());
          if (semantic->scriptKind == MathScriptKind::Superscript &&
              semantic->children.size() == 2) {
            const MathFontConstants& constants =
                OpenTypeMathFont::instance().constants();
            root.height = std::max(
                root.height,
                cssNodeHeight(semantic->children.front().get(), scale) +
                    cssNodeHeight(semantic->children.back().get(), scale) -
                    constants.superscriptBottomMin -
                    constants.overbarRuleThickness / 4.0);
          }
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
      if (wrappedTextModeRun(layout.root.get())) {
        root.width = cssNodeWidth(layout.root.get(), scale);
        root.height = cssNodeHeight(layout.root.get(), scale);
      } else if (symbolCount(layout.root.get()) == 1 &&
          !firstKind(layout.root.get(), MathRenderKind::Accent)) {
        const MathRenderNode* symbol = singleSymbol(layout.root.get());
        const auto glyph = nativeGlyphBox(symbol);
        if (glyph) {
          root.width = glyph->width;
          root.height = glyph->height;
        } else if (symbol && symbol->text.size() > 1 &&
                   symbol->fontClass == QLatin1String("main")) {
          root.width = cssNodeWidth(layout.root.get(), scale);
          root.height = cssNodeHeight(layout.root.get(), scale);
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
        if (hasAtomClass(layout.root.get(), QLatin1StringView("mbin")) &&
            !hasMathNumberToken(layout.root.get()))
          root.height += 1.0;
        root.height = std::max(root.height,
                               std::ceil(maxLargeOperatorExtent(layout.root.get())));
        if (const MathRenderNode* accent = firstKind(layout.root.get(), MathRenderKind::Accent);
            accent && accent->children.size() >= 2) {
          const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                                  accent->accentKind == MathAccentKind::OverBrace;
          const MathRenderNode* body = bodyIsLast ? accent->children.back().get()
                                                  : accent->children.front().get();
          const qreal bodyWidth = cssNodeWidth(body, scale);
          if ((accent->accentKind == MathAccentKind::Over ||
               accent->accentKind == MathAccentKind::Under) &&
              !accent->accentCharacter.isEmpty() &&
              symbolCount(layout.root.get()) > symbolCount(accent)) {
            const HorizontalAccentSelection widthSelection =
                selectHorizontalAccent(accent->accentCharacter, bodyWidth);
            const qreal accentWidth = widthSelection.text
                ? widthSelection.text->advance
                : widthSelection.fixed ? widthSelection.fixed->extent
                : widthSelection.assembly ? widthSelection.assembly->extent
                                          : bodyWidth;
            root.width += std::max<qreal>(0.0, accentWidth - bodyWidth);
          }
          qreal bodyHeight = std::ceil((body->height + body->depth) * scale);
          bool shapedTextBody = false;
          if (accent->accentKind == MathAccentKind::Under) {
            const GlyphInkExtents bodyInk = glyphInkExtents(body, 1.0);
            bodyHeight = bodyInk.top + bodyInk.bottom;
          }
          if (const MathRenderNode* textRun = wrappedTextModeRun(body)) {
            QString text;
            if (collectTextModeRun(textRun, &text)) {
              if (const auto shaped = shapeTextModeRun(
                      text, mathStyleScale(textRun))) {
                bodyHeight = shapedTextCssHeight(*shaped);
                root.width = shaped->advance;
                shapedTextBody = true;
              }
            }
          } else if (symbolCount(body) == 1) {
            const MathRenderNode* bodySymbol = singleSymbol(body);
            if (const auto glyph = nativeGlyphBox(bodySymbol)) {
              bodyHeight = glyph->height;
            } else if (bodySymbol && bodySymbol->text.size() > 1 &&
                       bodySymbol->fontClass == QLatin1String("main")) {
              if (const auto shaped = OpenTypeMathFont::instance()
                                          .shapeMathMlText(
                                              bodySymbol->text,
                                              mathStyleScale(bodySymbol))) {
                bodyHeight = std::ceil(shaped->inkBounds.height());
                root.width = shaped->advance;
                shapedTextBody = true;
              }
            }
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
          if ((accent->accentKind == MathAccentKind::Over ||
               accent->accentKind == MathAccentKind::Under) &&
              !accent->accentUsesNaturalWidth &&
              !accent->accentCharacter.isEmpty()) {
            const HorizontalAccentSelection selection =
                selectHorizontalAccent(accent->accentCharacter, root.width);
            accentHeight = accent->accentKind == MathAccentKind::Over &&
                    shapedTextBody
                ? selection.rasterHeight() : selection.height();
            if (selection.text)
              root.width = std::max(root.width, selection.text->advance);
          }
          root.height = bodyHeight + accentHeight +
                        OpenTypeMathFont::instance().constants().fractionRuleThickness;
          if (accent->accentKind == MathAccentKind::Under &&
              symbolCount(layout.root.get()) > symbolCount(accent)) {
            root.height += std::round(
                2.0 * OpenTypeMathFont::instance().constants()
                          .underbarRuleThickness);
          }
          if (shapedTextBody) {
            root.height = cssNodeHeight(accent, scale);
          }
        }
      }
      root.advance = root.width;
      break;
  }
  if (const MathRenderNode* accent = firstKind(
          layout.root.get(), MathRenderKind::Accent);
      semantic && accent && containsNode(accent, semantic) &&
      (accent->accentKind == MathAccentKind::Over ||
       accent->accentKind == MathAccentKind::Under) &&
      !accent->accentCharacter.isEmpty() && accent->children.size() >= 2) {
    const bool bodyIsLast = accent->accentKind == MathAccentKind::Under;
    const MathRenderNode* body = bodyIsLast
        ? accent->children.back().get() : accent->children.front().get();
    root.width = snapEighth(cssNodeWidth(layout.root.get(), scale));
    root.advance = root.width;
    const HorizontalAccentSelection selection = selectHorizontalAccent(
        accent->accentCharacter, root.width);
    const qreal operatorHeight = accent->accentKind == MathAccentKind::Over &&
            containsTextModeRun(body)
        ? selection.rasterHeight() : selection.height();
    const MathFontConstants& constants =
        OpenTypeMathFont::instance().constants();
    root.height = accentBodyCssHeight(body, scale) + operatorHeight +
        (accent->accentKind == MathAccentKind::Under
             ? constants.underbarExtraDescender
             : constants.overbarExtraAscender);
  }
  const bool hasRootSiblingContent = semantic &&
      symbolCount(layout.root.get()) > symbolCount(semantic);
  if (hasRootSiblingContent) {
    root.width = cssNodeWidth(layout.root.get(), scale);
    const bool accentOwner =
        firstKind(semantic, MathRenderKind::Accent) != nullptr;
    if (root.semanticKind == MathSemanticKind::SupSub &&
        hasAtomClass(semantic, QLatin1StringView("mop")) &&
        semantic->operatorKind != MathOperatorKind::Limits &&
        cssNodeOffset(layout.root.get(), semantic, scale).value_or(0.0) <=
            0.001 &&
        !enclosingKind(layout.root.get(), semantic,
                       MathRenderKind::LeftRight))
      root.width -= OpenTypeMathFont::instance().constants().spaceAfterScript;
    root.advance = root.width;
    const qreal siblingHeight = siblingSemanticHeight(
        layout.root.get(), semantic, scale, cssRootFontPixelSize);
    const bool primaryOwnsHeight = root.height >= siblingHeight;
    root.height = std::max(root.height, siblingHeight);
    if (root.semanticKind == MathSemanticKind::SupSub &&
        !accentOwner &&
        semantic->operatorKind != MathOperatorKind::Limits &&
        !hasAtomClass(semantic, QLatin1StringView("mop")) &&
        primaryOwnsHeight &&
        hasSiblingAtomClass(layout.root.get(), semantic,
                            QLatin1StringView("mbin")))
      root.height += std::ceil(
          OpenTypeMathFont::instance().constants().fractionRuleThickness /
          2.0);
    const MathRenderNode* rowAccent = accentOwner
        ? firstKind(semantic, MathRenderKind::Accent) : nullptr;
    if (rowAccent && rowAccent->accentKind == MathAccentKind::OverBrace) {
      root.height = std::max(
          root.height,
          scaledHeight - OpenTypeMathFont::instance().constants()
                             .overbarRuleThickness / 2.0);
    }
  }
  root.height = std::max(root.height, rootLeftRightFenceExtent(
      layout.root.get(), semantic, root.height));
  if (root.semanticKind == MathSemanticKind::Fraction &&
      !middleDelimiterMarker(semantic) &&
      (!semantic->leftDelimiter.isEmpty() ||
       !semantic->rightDelimiter.isEmpty())) {
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
  if (root.semanticKind == MathSemanticKind::SupSub && semantic) {
    if (const MathRenderNode* accent = firstKind(
            semantic, MathRenderKind::Accent);
        accent && accent->children.size() >= 2) {
      const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                              accent->accentKind == MathAccentKind::OverBrace;
      const MathRenderNode* body = bodyIsLast
          ? accent->children.back().get() : accent->children.front().get();
      if (firstKind(body, MathRenderKind::Accent))
        root.baseline = std::ceil(root.baseline * 4.0) / 4.0;
    }
  }
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
  const MathRenderNode* accent =
      firstKind(layout.root.get(), MathRenderKind::Accent);
  if (!accent || accent->children.size() < 2)
    return std::nullopt;

  const bool brace = accent->accentKind == MathAccentKind::UnderBrace ||
                     accent->accentKind == MathAccentKind::OverBrace;
  const bool arrow = (accent->accentKind == MathAccentKind::Under ||
                      accent->accentKind == MathAccentKind::Over) &&
                     !accent->accentCharacter.isEmpty();
  const QString basicCharacter = basicAccentCharacter(accent);
  const bool basic = !basicCharacter.isEmpty();
  if (!brace && !arrow && !basic) return std::nullopt;
  const bool over = accent->accentKind == MathAccentKind::OverBrace ||
                    accent->accentKind == MathAccentKind::Over ||
                    accent->accentKind == MathAccentKind::Overline;
  const bool bodyIsLast = accent->accentKind == MathAccentKind::Under ||
                          accent->accentKind == MathAccentKind::OverBrace;
  const MathRenderNode* body = bodyIsLast ? accent->children.back().get()
                                         : accent->children.front().get();
  const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
  const qreal bodyHeight = accentBodyCssHeight(
      body, cssRootFontPixelSize / renderFontPixelSize);
  const qreal gap = over ? constants.overbarVerticalGap
                         : constants.underbarVerticalGap;
  const qreal rule = over ? constants.overbarRuleThickness
                          : constants.underbarRuleThickness;
  qreal accentHeight = !over && containsTextModeRun(body)
      ? std::ceil(2.0 * rule)
      : std::round(gap + 2.0 * rule);
  accentHeight -= embeddedBraceStyleReduction(body);
  const qreal extra = over ? constants.overbarExtraAscender
                           : constants.underbarExtraDescender;
  const MathCssBox root = layoutMathMlCssBox(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  const qreal rootHeight = snapEighth(root.height);

  if (basic) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto glyph = font.glyph(basicCharacter);
    if (!glyph || glyph->glyphIndex == 0) return std::nullopt;
    const QRectF ink = font.rasterGlyphBounds(glyph->glyphIndex);
    if (ink.isEmpty()) return std::nullopt;
    const qreal glyphHeight = std::max<qreal>(
        1.0, std::round(rootHeight - bodyHeight - extra));
    const qreal bodyWidth = cssNodeWidth(
        body, cssRootFontPixelSize / renderFontPixelSize);
    MathCssAccentBox result;
    result.over = over;
    result.character = basicCharacter;
    result.body = QRectF((root.width - bodyWidth) / 2.0,
                         over ? rootHeight - bodyHeight : 0.0,
                         bodyWidth, bodyHeight);
    result.accent = QRectF((root.width - glyph->advance) / 2.0,
                           over ? result.body.top() - glyphHeight
                                : result.body.bottom(),
                           glyph->advance, glyphHeight);
    return result;
  }

  if (arrow) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    if (accent->accentUsesNaturalWidth) {
      const auto variant = font.horizontalVariant(
          accent->accentCharacter, 0.0);
      const auto base = font.glyph(accent->accentCharacter);
      if (!variant && !base) return std::nullopt;
      const qreal glyphWidth = variant ? variant->extent : base->advance;
      const qreal glyphHeight = variant
          ? variant->advance : std::ceil(base->inkBounds.height());
      MathCssAccentBox result;
      result.over = over;
      result.character = accent->accentCharacter;
      result.body = QRectF(0.0, rootHeight - bodyHeight,
                           root.width, bodyHeight);
      result.accent = QRectF(
          (root.width - glyphWidth) / 2.0,
          result.body.top() - glyphHeight,
          glyphWidth, glyphHeight);
      return result;
    }
    const HorizontalAccentSelection selection = selectHorizontalAccent(
        accent->accentCharacter, root.width);
    if (!selection.fixed && !selection.assembly && !selection.text)
      return std::nullopt;
    const qreal arrowHeight = over && containsTextModeRun(body)
        ? selection.rasterHeight() : selection.height();
    const qreal accentWidth = selection.text
        ? selection.text->advance
        : selection.fixed ? selection.fixed->extent
        : selection.assembly ? selection.assembly->extent : root.width;
    const qreal bodyWidth = cssNodeWidth(
        body, cssRootFontPixelSize / renderFontPixelSize);
    MathCssAccentBox result;
    result.over = over;
    result.character = accent->accentCharacter;
    result.body = QRectF((root.width - bodyWidth) / 2.0,
                         over ? rootHeight - bodyHeight : 0.0,
                         bodyWidth, bodyHeight);
    result.accent = QRectF((root.width - accentWidth) / 2.0,
                           over ? result.body.top() - arrowHeight
                                : result.body.bottom(),
                           accentWidth, arrowHeight);
    return result;
  }

  const qreal annotationHeight = std::max<qreal>(
      0.0, rootHeight - bodyHeight - accentHeight - 2.0 * gap - extra);

  MathCssAccentBox result;
  result.over = over;
  result.character = QString(QChar(over ? 0x23DE : 0x23DF));
  result.fontScale = constants.scriptPercentScaleDown;
  const qreal bodyWidth = cssNodeWidth(
      body, cssRootFontPixelSize / renderFontPixelSize);
  const qreal bodyLeft = (root.width - bodyWidth) / 2.0;
  if (over) {
    result.annotation = QRectF(0.0, 0.0, root.width, annotationHeight);
    result.body = QRectF(bodyLeft, rootHeight - bodyHeight,
                         bodyWidth, bodyHeight);
    result.accent = QRectF(bodyLeft,
                           result.body.top() - gap - accentHeight,
                           bodyWidth, accentHeight);
  } else {
    result.body = QRectF(bodyLeft, 0.0, bodyWidth, bodyHeight);
    result.accent = QRectF(bodyLeft, result.body.bottom() + gap,
                           bodyWidth, accentHeight);
    result.annotation = QRectF(0.0, rootHeight - annotationHeight,
                               root.width, annotationHeight);
  }
  return result;
}

namespace {

QString basicAccentCharacter(const MathRenderNode* accent) {
  if (!accent || accent->kind != MathRenderKind::Accent ||
      accent->children.size() < 2)
    return {};
  if (accent->accentKind == MathAccentKind::Overline ||
      accent->accentKind == MathAccentKind::Underline)
    return QString(QChar(0x203E));
  if (accent->accentKind != MathAccentKind::Over ||
      !accent->accentCharacter.isEmpty())
    return {};
  const MathRenderNode* accentPart = accent->children.back().get();
  if (const MathRenderNode* symbol = singleSymbol(accentPart))
    return symbol->text;
  if (const MathRenderNode* stretchy =
          firstKind(accentPart, MathRenderKind::Stretchy);
      stretchy && stretchy->pathName == QLatin1String("vec"))
    return QString(QChar(0x20D7));
  return {};
}

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
  return brace || arrow || !basicAccentCharacter(node).isEmpty();
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
  return node && ((!node->middleDelimiter.isEmpty()) ||
                  (node->kind == MathRenderKind::LeftRight &&
                   primarySemanticNode(node) == nullptr) ||
                  hasPaintOperation(node->semanticKind)) &&
         !ownsAccentPaintOperation(node);
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

bool findCssPaintX(const MathRenderNode* node,
                   const MathRenderNode* target, qreal origin,
                   qreal* result) {
  if (!node || !target || !result) return false;
  if (node == target) {
    *result = origin;
    return true;
  }
  if (node->kind == MathRenderKind::Span) {
    qreal x = origin;
    for (const auto& child : node->children) {
      if (findCssPaintX(child.get(), target, x + child->xOffset, result))
        return true;
      x += cssNodeWidth(child.get(), 1.0);
    }
    return false;
  }
  for (const auto& child : node->children)
    if (findCssPaintX(child.get(), target, origin + child->xOffset, result))
      return true;
  return false;
}

bool collectGlyphRunSymbols(const MathRenderNode* node,
                            QVector<const MathRenderNode*>* symbols,
                            bool skipOwnedOperations = false) {
  if (!node || !symbols || node->phantom) return true;
  if (skipOwnedOperations && ownsPaintOperation(node)) return true;
  if (!node->middleDelimiter.isEmpty()) return skipOwnedOperations;
  if (node->textModeRun) {
    symbols->push_back(node);
    return true;
  }
  if (node->kind == MathRenderKind::Symbol ||
      node->kind == MathRenderKind::Error) {
    if (node->text.isEmpty()) return true;
    symbols->push_back(node);
    return true;
  }
  if (node->semanticKind != MathSemanticKind::None)
    return skipOwnedOperations;
  const bool zeroAdvanceOverlay =
      node->kind == MathRenderKind::Accent &&
      node->accentKind == MathAccentKind::None &&
      qFuzzyIsNull(node->width);
  if (node->kind != MathRenderKind::Span &&
      node->kind != MathRenderKind::VList && !zeroAdvanceOverlay)
    return false;
  for (const auto& child : node->children)
    if (!collectGlyphRunSymbols(child.get(), symbols, skipOwnedOperations))
      return false;
  return true;
}

std::optional<QVector<MathCssGlyphRunOperation>> buildGlyphRunOperations(
    const MathRenderNode* node, QRectF target, qreal fontScale,
    bool allowPartialOwnership = false,
    std::optional<qreal> cssPositionScale = std::nullopt) {
  if (!node || target.isEmpty() || node->width <= 0.0) return std::nullopt;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  QVector<const MathRenderNode*> symbols;
  bool collected = collectGlyphRunSymbols(node, &symbols);
  if (!collected && allowPartialOwnership) {
    symbols.clear();
    collected = collectGlyphRunSymbols(node, &symbols, true);
  }
  if (!collected || symbols.isEmpty())
    return std::nullopt;

  QVector<MathCssGlyphRunOperation> runs;
  runs.reserve(symbols.size());
  const qreal horizontalScale = target.width() / node->width;
  const bool hasTextModeSource = std::any_of(
      symbols.cbegin(), symbols.cend(), [](const MathRenderNode* symbol) {
        return wrappedTextModeRun(symbol) != nullptr;
      });
  qreal inkTop = std::numeric_limits<qreal>::max();
  qreal inkBottom = std::numeric_limits<qreal>::lowest();
  for (const MathRenderNode* symbol : symbols) {
    QPointF sourceOrigin;
    if (!findPaintOrigin(node, symbol, QPointF(0.0, node->height),
                         &sourceOrigin)) {
      return std::nullopt;
    }
    if (hasTextModeSource) {
      qreal cssX = 0.0;
      if (!findCssPaintX(node, symbol, 0.0, &cssX)) return std::nullopt;
      sourceOrigin.setX(cssX);
    }
    if (cssPositionScale) {
      const auto cssX = cssNodeOffset(node, symbol, *cssPositionScale);
      if (!cssX) return std::nullopt;
      sourceOrigin.setX(*cssX + nativeOperatorPadding(symbol));
    }
    if (singleSymbol(node) == symbol) sourceOrigin.setX(0.0);
    if (const MathRenderNode* textRun = wrappedTextModeRun(symbol)) {
      QString text;
      const MathRenderNode* firstSymbol = nullptr;
      if (!collectTextModeRun(textRun, &text, &firstSymbol) ||
          text.isEmpty() || !firstSymbol)
        return std::nullopt;
      const auto shaped = shapeTextModeRun(text, fontScale);
      if (!shaped) return std::nullopt;
      for (const MathShapedTextRun& shapedRun : shaped->runs) {
        MathCssGlyphRunOperation run;
        run.text = shapedRun.text;
        run.fontFamily = shapedRun.familyName;
        run.fontClass = firstSymbol->fontClass;
        run.atomClass = textRun->atomClass;
        run.rawFont = shapedRun.rawFont;
        run.glyphIndexes = shapedRun.glyphIndexes;
        run.positions = shapedRun.positions;
        run.advance = shapedRun.advance;
        run.fontScale = fontScale;
        run.baselineOrigin.setX(
            target.left() + sourceOrigin.x() *
                (hasTextModeSource || cssPositionScale ? 1.0
                                                       : horizontalScale));
        run.inkBounds = shapedRun.inkBounds;
        inkTop = std::min(inkTop, run.inkBounds.top());
        inkBottom = std::max(inkBottom, run.inkBounds.bottom());
        run.clip = target;
        runs.push_back(std::move(run));
      }
      continue;
    }
    MathCssGlyphRunOperation run;
    run.text = symbol->text;
    run.fontClass = symbol->fontClass;
    run.atomClass = symbol->atomClass;
    run.fontScale = fontScale;
    run.baselineOrigin = QPointF(
        target.left() + sourceOrigin.x() *
            (hasTextModeSource || cssPositionScale ? 1.0
                                                   : horizontalScale),
        0.0);
    if (symbol->text.size() == 1) {
      const QChar character = symbol->text.front();
      const bool italic = character.isLetter() &&
          (symbol->fontClass == QLatin1String("mathnormal") ||
           symbol->fontClass == QLatin1String("mathit"));
      const auto glyph = italic ? font.mathItalicGlyph(character)
                                : font.glyph(symbol->text);
      if (glyph) {
        run.glyphIndexes.push_back(glyph->glyphIndex);
        run.positions.push_back(QPointF());
        run.advance = glyph->advance * run.fontScale;
        run.inkBounds = font.rasterGlyphBounds(
            glyph->glyphIndex, run.fontScale);
      } else {
        QRawFont rawFont = QRawFont::fromFont(symbol->font);
        rawFont.setPixelSize(font.pixelSize() * run.fontScale);
        const QList<quint32> indexes =
            rawFont.glyphIndexesForString(symbol->text);
        if (!rawFont.isValid() || indexes.size() != 1 ||
            indexes.front() == 0)
          return std::nullopt;
        run.rawFont = rawFont;
        run.fontFamily = rawFont.familyName();
        run.glyphIndexes.push_back(indexes.front());
        run.positions.push_back(QPointF());
        const QList<QPointF> advances =
            rawFont.advancesForGlyphIndexes(indexes);
        run.advance = advances.isEmpty() ? 0.0 : advances.front().x();
        run.inkBounds = rawFont.boundingRect(indexes.front());
      }
    } else {
      const auto shaped = font.shapeMathMlText(symbol->text, run.fontScale);
      if (!shaped) return std::nullopt;
      run.glyphIndexes = shaped->glyphIndexes;
      run.positions = shaped->positions;
      run.advance = shaped->advance;
      run.inkBounds = shaped->inkBounds;
    }
    inkTop = std::min(inkTop, run.inkBounds.top());
    inkBottom = std::max(inkBottom, run.inkBounds.bottom());
    run.clip = target;
    runs.push_back(run);
  }
  const qreal baseline = target.center().y() - (inkTop + inkBottom) / 2.0;
  for (MathCssGlyphRunOperation& run : runs) {
    run.baselineOrigin.setY(baseline);
    run.inkBounds.translate(run.baselineOrigin);
  }
  QVector<MathCssGlyphRunOperation> merged;
  merged.reserve(runs.size());
  for (MathCssGlyphRunOperation& run : runs) {
    if (!merged.isEmpty()) {
      MathCssGlyphRunOperation& previous = merged.back();
      const qreal offset = run.baselineOrigin.x() -
                           previous.baselineOrigin.x();
      const bool contiguous =
          qFuzzyCompare(previous.fontScale, run.fontScale) &&
          previous.fontFamily == run.fontFamily &&
          previous.fontClass == run.fontClass &&
          previous.atomClass == run.atomClass &&
          previous.clip == run.clip &&
          std::abs(offset - previous.advance) <= 1.0;
      if (contiguous) {
        previous.text += run.text;
        for (qsizetype index = 0; index < run.glyphIndexes.size(); ++index) {
          previous.glyphIndexes.push_back(run.glyphIndexes.at(index));
          previous.positions.push_back(
              run.positions.at(index) + QPointF(offset, 0.0));
        }
        previous.advance = std::max(
            previous.advance, offset + run.advance);
        previous.inkBounds = previous.inkBounds.united(run.inkBounds);
        continue;
      }
    }
    merged.push_back(std::move(run));
  }
  return merged;
}

struct MathMlStyleContext {
  int scriptLevel = 0;

  qreal fontScale() const {
    const MathFontConstants& constants =
        OpenTypeMathFont::instance().constants();
    if (scriptLevel >= 2) return constants.scriptScriptPercentScaleDown;
    if (scriptLevel == 1) return constants.scriptPercentScaleDown;
    return 1.0;
  }

  MathMlStyleContext scriptStyle() const {
    return {std::min(scriptLevel + 1, 2)};
  }
};

MathMlStyleContext fractionRowStyle(
    const MathRenderNode* fraction, MathMlStyleContext parentStyle) {
  if (!fraction) return parentStyle.scriptStyle();
  const int fractionLevel = std::clamp(fraction->fractionStyleSize, 0, 2);
  return {std::max(parentStyle.scriptLevel, fractionLevel)};
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
  if (const MathRenderNode* textRun = wrappedTextModeRun(node)) {
    QString text;
    if (collectTextModeRun(textRun, &text)) {
      if (const auto shaped = shapeTextModeRun(text, fontScale)) {
        const GlyphInkExtents outline = shapedTextCssExtents(*shaped);
        const qreal allocation = shapedTextCssHeight(*shaped);
        const qreal descent = std::min(outline.bottom, allocation);
        result.ink = {allocation - descent, descent};
        result.ascent = result.ink.top;
        result.descent = result.ink.bottom;
        return result;
      }
    }
  }
  result.ink = preciseTokenInkExtents(node, fontScale);
  if (const MathRenderNode* symbol = singleSymbol(node);
      symbol && symbol->atomClass == QLatin1String("mop")) {
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    if (const auto variant = font.verticalVariant(
            symbol->text, font.constants().displayOperatorMinHeight)) {
      const QRectF ink = font.rasterGlyphBounds(
          variant->glyphIndex, fontScale);
      result.ink = {std::max<qreal>(0.0, -ink.top()),
                    std::max<qreal>(0.0, ink.bottom())};
      const qreal height = largeOperatorAllocationHeight(
          symbol->text, *variant, fontScale);
      result.ascent = std::min(height, std::round(result.ink.top));
      result.descent = std::max<qreal>(0.0, height - result.ascent);
      return result;
    }
  }
  if (const MathRenderNode* symbol = singleSymbol(node);
      symbol && !primarySemanticNode(node)) {
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
  if (!script || script->semanticKind != MathSemanticKind::SupSub)
    return std::nullopt;
  if (script->operatorKind == MathOperatorKind::Limits &&
      script->children.size() >= 3) {
    const MathRenderNode* subscript = script->children.front().get();
    const MathRenderNode* base = script->children[1].get();
    const MathRenderNode* superscript = script->children.back().get();
    const MathRenderNode* operatorSymbol = singleSymbol(base);
    if (!operatorSymbol) return std::nullopt;

    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const MathFontConstants& constants = font.constants();
    const auto variant = font.verticalVariant(
        operatorSymbol->text, constants.displayOperatorMinHeight);
    if (!variant) return std::nullopt;

    const bool textModeLimits = containsTextModeRun(script);
    const CssTokenLineMetrics supMetrics = cssTokenLineMetrics(
        superscript, renderScale, constants.scriptPercentScaleDown);
    const CssTokenLineMetrics subMetrics = cssTokenLineMetrics(
        subscript, renderScale, constants.scriptPercentScaleDown);
    GlyphInkExtents supInk = textModeLimits
        ? supMetrics.ink
        : glyphInkExtents(superscript, constants.scriptPercentScaleDown);
    GlyphInkExtents subInk = textModeLimits
        ? subMetrics.ink
        : glyphInkExtents(subscript, constants.scriptPercentScaleDown);
    supInk.top = std::max(
        supInk.top,
        std::round(2.0 * constants.axisHeight *
                   constants.scriptPercentScaleDown));
    subInk.top = std::max(
        subInk.top, std::round(2.0 * constants.axisHeight));
    const qreal supHeight = textModeLimits
        ? supMetrics.height() : supInk.top + supInk.bottom;
    const qreal subHeight = textModeLimits
        ? subMetrics.height() : subInk.top + subInk.bottom;
    const qreal operatorHeight = largeOperatorAllocationHeight(
        operatorSymbol->text, *variant);
    const qreal upperGap = textModeLimits
        ? std::max(constants.upperLimitGapMin,
                   constants.upperLimitBaselineRiseMin - supInk.bottom)
        : std::max(constants.upperLimitGapMin,
                   constants.upperLimitBaselineRiseMin);
    const qreal lowerGap = std::max(
        constants.lowerLimitGapMin,
        constants.lowerLimitBaselineDropMin - subInk.top);
    const qreal height = supHeight + upperGap + operatorHeight +
                         lowerGap + subHeight;
    const qreal baseWidth = variant->advance;
    const qreal supWidth = cssNodeWidth(superscript, renderScale);
    const qreal subWidth = cssNodeWidth(subscript, renderScale);
    const qreal width = std::max({baseWidth, supWidth, subWidth});
    const qreal operatorSpacing =
        3.0 / 18.0 * font.pixelSize();
    const qreal left = containingRect.left() + cssNodeOffset(
        containingNode, script, renderScale).value_or(0.0) +
        operatorSpacing;
    const bool fillsContainingRow = primarySemanticNode(containingNode) == script;
    const qreal top = fillsContainingRow
        ? containingRect.top()
        : containingRect.top() + (containingRect.height() - height) / 2.0;
    MathCssScriptOperation result;
    result.kind = script->scriptKind;
    result.limits = true;
    result.lineAscent = supHeight + upperGap + operatorHeight / 2.0 +
                        constants.axisHeight;
    result.lineDescent = height - result.lineAscent;
    result.container = QRectF(left, top, width, height);
    result.baseNode = base;
    result.superscriptNode = superscript;
    result.subscriptNode = subscript;
    result.base = QRectF(left + (width - baseWidth) / 2.0,
                         top + supHeight + upperGap,
                         baseWidth, operatorHeight);
    result.superscript = QRectF(left + (width - supWidth) / 2.0,
                                top, supWidth, supHeight);
    result.subscript = QRectF(left + (width - subWidth) / 2.0,
                              result.base.bottom() + lowerGap,
                              subWidth, subHeight);
    result.container = snapVerticalLayoutRect(result.container);
    result.base = snapVerticalLayoutRect(result.base);
    result.superscript = snapVerticalLayoutRect(result.superscript);
    result.subscript = snapVerticalLayoutRect(result.subscript);
    result.largeOperatorGlyph = buildVerticalGlyphOperation(
        operatorSymbol->text, result.base, false);
    return result;
  }
  if (script->children.size() != 2) return std::nullopt;
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
  const MathRenderNode* operatorSymbol = singleSymbol(base);
  const bool largeOperator = operatorSymbol &&
      operatorSymbol->atomClass == QLatin1String("mop") &&
      OpenTypeMathFont::instance().verticalVariant(
          operatorSymbol->text, constants.displayOperatorMinHeight);
  const CssTokenLineMetrics baseMetrics = cssTokenLineMetrics(
      base, renderScale, 1.0);
  const CssTokenLineMetrics supMetrics = cssTokenLineMetrics(
      superscript, renderScale, constants.scriptPercentScaleDown);
  const CssTokenLineMetrics subMetrics = cssTokenLineMetrics(
      subscript, renderScale, constants.scriptPercentScaleDown);
  GlyphInkExtents baseInk = baseMetrics.ink;
  if (largeOperator)
    baseInk = {baseMetrics.ascent, baseMetrics.descent};
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
  const qreal height = lineAscent + lineDescent;
  qreal baseWidth = cssNodeWidth(base, renderScale);
  if (largeOperator) {
    if (const auto variant = OpenTypeMathFont::instance().verticalVariant(
            operatorSymbol->text, constants.displayOperatorMinHeight))
      baseWidth = variant->advance;
  }
  const qreal supOffset = 0.0;
  qreal subOffset = 0.0;
  if (largeOperator && subscript) {
    if (const auto variant = OpenTypeMathFont::instance().verticalVariant(
            operatorSymbol->text, constants.displayOperatorMinHeight))
      subOffset = -variant->italicCorrection;
  }
  qreal width = cssNodeWidth(script, renderScale);
  if (largeOperator) {
    width = baseWidth + std::max(cssNodeWidth(superscript, renderScale),
                                 cssNodeWidth(subscript, renderScale)) +
            constants.spaceAfterScript;
  }
  qreal left = containingRect.left() + cssNodeOffset(
      containingNode, script, renderScale).value_or(0.0);
  if (largeOperator)
    left += 3.0 / 18.0 * OpenTypeMathFont::instance().pixelSize();
  if (const MathRenderNode* leftRight = enclosingKind(
          containingNode, script, MathRenderKind::LeftRight))
    left = containingRect.left() + arrayDelimiterWidth(
        leftRight->leftDelimiter, containingRect.height());
  const bool fillsContainingRow = primarySemanticNode(containingNode) == script;
  const qreal top = fillsContainingRow
      ? containingRect.top()
      : containingRect.top() + (containingRect.height() - height) / 2.0;
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
        left + baseWidth + supOffset,
        top + lineAscent - supShift - supInk.top,
        cssNodeWidth(superscript, renderScale),
        supMetrics.height());
    if (middleDelimiterMarker(superscript))
      result.superscript.translate(
          0.0, snapEighth(constants.fractionRuleThickness *
                          constants.scriptPercentScaleDown));
  }
  if (subscript) {
    result.subscript = QRectF(
        left + baseWidth + subOffset,
        top + lineAscent + subShift - subInk.top,
        cssNodeWidth(subscript, renderScale),
        subMetrics.height());
  }
  if (largeOperator) {
    if (superscript) {
      result.superscript.setY(result.container.top());
      result.superscript.setHeight(
          std::ceil(supMetrics.height()));
    }
    if (subscript) {
      qreal subHeight = std::ceil(subMetrics.height());
      if (const MathRenderNode* symbol = singleSymbol(subscript);
          symbol && symbol->text.size() == 1 &&
          symbol->text.front().isLetter()) {
        subHeight = std::max(
            subHeight,
            std::ceil(subMetrics.ink.top + subMetrics.ink.bottom));
      }
      result.subscript.setY(result.container.bottom() - subHeight);
      result.subscript.setHeight(subHeight);
    }
  }
  result.container = snapVerticalLayoutRect(result.container);
  result.base = snapVerticalLayoutRect(result.base);
  result.superscript = snapVerticalLayoutRect(result.superscript);
  result.subscript = snapVerticalLayoutRect(result.subscript);
  if (largeOperator) {
    result.largeOperatorGlyph = buildVerticalGlyphOperation(
        operatorSymbol->text, result.base, false);
  }
  return result;
}

std::optional<MathCssVerticalGlyphOperation> buildVerticalGlyphOperation(
    const QString& delimiter, QRectF target,
    bool normalizeDelimiter = true) {
  if (delimiter.isEmpty() || delimiter == QLatin1String(".") ||
      target.isEmpty())
    return std::nullopt;

  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QString character = normalizeDelimiter
      ? mathDelimiterCharacter(delimiter) : delimiter;
  if (character == QLatin1String("|") ||
      character == QString(QChar(0x2223)) ||
      character == QString(QChar(0x2225))) {
    const auto base = font.glyph(character);
    if (!base || base->glyphIndex == 0) return std::nullopt;
    MathCssVerticalGlyphOperation result;
    result.clipToBlockExtent = normalizeDelimiter;
    result.target = target;
    result.character = character;
    result.selectionTarget = target.height();
    result.kind = MathCssVerticalGlyphKind::FixedVariant;
    result.fixedGlyphIndex = base->glyphIndex;
    result.realizedExtent = base->inkBounds.height();
    result.advance = base->advance;
    result.inkBounds = font.rasterGlyphBounds(base->glyphIndex);
    if (result.inkBounds.isEmpty()) return std::nullopt;
    result.baselineOrigin = target.center() - result.inkBounds.center();
    return result;
  }
  const auto largestFixed = font.verticalVariant(
      character, std::numeric_limits<qreal>::max());
  const auto assembly = largestFixed &&
          target.height() > std::ceil(largestFixed->extent + 0.001)
      ? font.verticalAssemblyParts(character, target.height())
      : std::optional<MathGlyphAssembly>{};

  MathCssVerticalGlyphOperation result;
  result.target = target;
  result.character = character;
  result.selectionTarget = target.height();
  if (assembly && !assembly->parts.isEmpty()) {
    result.kind = MathCssVerticalGlyphKind::Assembly;
    result.realizedExtent = assembly->extent;
    result.advance = assembly->advance;
    result.italicCorrection = assembly->italicCorrection;
    result.baselineOrigin.setX(target.center().x());
    result.parts.reserve(assembly->parts.size());
    for (const MathGlyphAssemblyPart& part : assembly->parts) {
      const QRectF partInk = font.rasterGlyphBounds(part.glyphIndex);
      if (partInk.isEmpty()) return std::nullopt;
      const QList<QPointF> advances = font.rasterFont().advancesForGlyphIndexes(
          {part.glyphIndex});
      if (advances.isEmpty()) return std::nullopt;
      const QPointF position(-advances.front().x() / 2.0,
                             part.offset - partInk.top());
      const QRectF positionedInk = partInk.translated(position);
      result.parts.push_back({part.glyphIndex, position, positionedInk,
                              part.offset,
                              part.fullAdvance, part.connectorOverlap,
                              part.extender});
      result.inkBounds = result.inkBounds.isNull()
          ? positionedInk : result.inkBounds.united(positionedInk);
    }
    result.baselineOrigin.setY(target.top() - result.inkBounds.top());
    return result;
  }

  const auto fixed = font.verticalVariant(character, target.height());
  if (!fixed) return std::nullopt;
  result.kind = MathCssVerticalGlyphKind::FixedVariant;
  if (normalizeDelimiter) {
    result.scalePolicy = MathCssVerticalScalePolicy::FitSelectedExtent;
    result.clipToBlockExtent = true;
  }
  result.fixedGlyphIndex = fixed->glyphIndex;
  result.realizedExtent = fixed->extent;
  result.advance = fixed->advance;
  result.inkBounds = font.rasterGlyphBounds(fixed->glyphIndex);
  if (result.inkBounds.isEmpty()) return std::nullopt;
  result.baselineOrigin = target.center() - result.inkBounds.center();
  return result;
}

std::optional<MathCssVerticalGlyphOperation> buildInlineFenceGlyphOperation(
    const QString& delimiter, QRectF target) {
  if (delimiter.isEmpty() || delimiter == QLatin1String(".") ||
      target.isEmpty())
    return std::nullopt;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QString character = mathDelimiterCharacter(delimiter);
  const auto glyph = font.glyph(character);
  if (!glyph || glyph->glyphIndex == 0) return std::nullopt;
  const QRectF ink = font.rasterGlyphBounds(glyph->glyphIndex);
  if (ink.isEmpty()) return std::nullopt;

  MathCssVerticalGlyphOperation result;
  result.kind = MathCssVerticalGlyphKind::FixedVariant;
  result.scalePolicy = MathCssVerticalScalePolicy::FitTargetExtent;
  result.clipToBlockExtent = true;
  result.target = target;
  result.character = character;
  result.selectionTarget = target.height();
  result.fixedGlyphIndex = glyph->glyphIndex;
  result.realizedExtent = ink.height();
  result.advance = glyph->advance;
  result.inkBounds = ink;
  const qreal inkTop = target.top() + kChromiumMathAxisOffsetPx -
                       font.constants().axisHeight;
  result.baselineOrigin = QPointF(
      target.center().x() - ink.center().x(), inkTop - ink.top());
  return result;
}

std::optional<MathCssRadicalOperation> buildRadicalOperation(
    const MathRenderNode* radical, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, bool topLevel = false) {
  if (!radical || radical->semanticKind != MathSemanticKind::Radical)
    return std::nullopt;
  const MathRenderNode* radicalBox = radical->radicalIndex
      ? indexedRadicalBox(radical) : radical;
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
  else if (!primarySemanticNode(body) && symbolCount(body) > 1 &&
           !middleDelimiterMarker(body))
    bodyHeight += 1.0;
  qreal target = std::max(bodyInk.top + bodyInk.bottom, bodyHeight) +
                 radicalGap + radicalRule;
  if (middleDelimiterMarker(body))
    target += kChromiumMathAxisOffsetPx;
  const MathRenderNode* enclosingLeftRight = topLevel
      ? enclosingKind(containingNode, radical, MathRenderKind::LeftRight)
      : nullptr;
  if (topLevel && !enclosingLeftRight && !radical->radicalIndex)
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
  qreal left = containingRect.left() + cssNodeOffset(
      containingNode, radical, renderScale).value_or(0.0);
  if (enclosingLeftRight)
    left = containingRect.left() + arrayDelimiterWidth(
        enclosingLeftRight->leftDelimiter, containingRect.height());
  const qreal degreeBaseLeft = left;
  const MathRenderNode* degreeNode = radical->radicalIndex
      ? indexedRadicalDegree(radical, radicalBox) : nullptr;
  const qreal degreeAdvance = degreeNode
      ? std::max<qreal>(0.0, constants.radicalKernBeforeDegree +
                                radicalDegreeAdvanceWidth(degreeNode,
                                                          renderScale) +
                                constants.radicalKernAfterDegree)
      : 0.0;
  left += degreeNode
      ? constants.radicalKernBeforeDegree + degreeAdvance
      : 0.0;
  const bool fillsContainingRow = primarySemanticNode(containingNode) == radical;
  const qreal top = fillsContainingRow
      ? containingRect.top()
      : containingRect.top() + (containingRect.height() - height) / 2.0;
  const qreal intrinsicRadicalHeight =
      variant->extent * styleScale + radicalExtra;
  const qreal indexedExpansion = radical->radicalIndex && fillsContainingRow
      ? std::max<qreal>(0.0,
                        containingRect.height() - intrinsicRadicalHeight)
      : 0.0;
  const qreal decorationShift = indexedExpansion > 0.0
      ? indexedExpansion / 2.0 + radicalRule / 2.0 : 0.0;
  const qreal indexedBodyTop = top + radicalExtra + radicalRule +
      2.0 * radicalGap +
      (indexedExpansion > 0.0
           ? indexedExpansion / 2.0 + radicalRule / 4.0 : 0.0);

  MathCssRadicalOperation result;
  result.lineAscent = lineAscent;
  result.lineDescent = lineDescent;
  result.container = fillsContainingRow
      ? QRectF(degreeBaseLeft, containingRect.top(),
               radicalCssWidth(radical, renderScale),
               containingRect.height())
      : QRectF(degreeBaseLeft, top, radicalCssWidth(radical, renderScale),
               height);
  result.body = QRectF(left + variant->advance * styleScale,
                       radical->radicalIndex && fillsContainingRow
                           ? indexedBodyTop
                           : top + lineAscent - bodyAscent,
                       bodyWidth, bodyHeight);
  result.radicalRule.target = QRectF(result.body.left(),
                                     top + radicalExtra + decorationShift,
                                     bodyWidth, radicalRule);
  result.radicalGlyph.character = QString(QChar(0x221A));
  result.radicalGlyph.glyphIndex = variant->glyphIndex;
  result.radicalGlyph.target = QRectF(left,
                                      top + radicalExtra + decorationShift,
                                      variant->advance * styleScale,
                                      variant->extent * styleScale);
  result.bodyNode = body;
  result.degreeNode = degreeNode;
  if (result.degreeNode) {
      const qreal degreeScale = constants.scriptScriptPercentScaleDown;
      const qreal degreeWidth = primarySemanticNode(result.degreeNode)
          ? radicalDegreePaintWidth(result.degreeNode, renderScale)
          : cssNodeWidth(result.degreeNode, renderScale);
      const MathRenderNode* degreeSymbol =
          primarySemanticNode(result.degreeNode)
          ? nullptr : singleSymbol(result.degreeNode);
      const auto degreeGlyphBox = nativeGlyphBox(degreeSymbol);
      const qreal degreeHeight = degreeGlyphBox
          ? std::round(degreeGlyphBox->height * degreeScale)
          : radicalDegreePaintHeight(result.degreeNode, renderScale);
      const qreal degreeBottom = result.radicalGlyph.target.bottom() -
          constants.radicalDegreeBottomRaisePercent *
              result.radicalGlyph.target.height() +
          (fillsContainingRow
               ? std::max<qreal>(0.0, containingRect.height() - height) -
                     decorationShift
               : 0.0);
      result.degree = QRectF(
          degreeBaseLeft + constants.radicalKernBeforeDegree,
          degreeBottom - degreeHeight, degreeWidth, degreeHeight);
  }
  if (enclosingLeftRight) {
    MathCssFencePair fences;
    const qreal leftWidth = arrayDelimiterWidth(
        enclosingLeftRight->leftDelimiter, containingRect.height());
    const qreal rightWidth = arrayDelimiterWidth(
        enclosingLeftRight->rightDelimiter, containingRect.height());
    fences.left = QRectF(containingRect.left(), containingRect.top(),
                         leftWidth, containingRect.height());
    fences.right = QRectF(containingRect.right() - rightWidth,
                          containingRect.top(), rightWidth,
                          containingRect.height());
    fences.leftCharacter = mathDelimiterCharacter(
        enclosingLeftRight->leftDelimiter);
    fences.rightCharacter = mathDelimiterCharacter(
        enclosingLeftRight->rightDelimiter);
    fences.leftGlyph = buildInlineFenceGlyphOperation(
        fences.leftCharacter, fences.left);
    fences.rightGlyph = buildInlineFenceGlyphOperation(
        fences.rightCharacter, fences.right);
    result.fences = std::move(fences);
    result.container = containingRect;
  }
  result.container = snapVerticalLayoutRect(result.container);
  result.body = snapVerticalLayoutRect(result.body);
  result.degree = snapVerticalLayoutRect(result.degree);
  result.radicalRule.target = snapVerticalLayoutRect(
      result.radicalRule.target);
  result.radicalGlyph.target = snapVerticalLayoutRect(
      result.radicalGlyph.target);
  result.radicalGlyph.fontScale = styleScale;
  result.radicalGlyph.inkBounds = font.rasterGlyphBounds(
      variant->glyphIndex, styleScale);
  result.radicalGlyph.clip = result.radicalGlyph.target.intersected(
      result.container);
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
    MathMlStyleContext style = {}, bool topLevel = false,
    bool rootRowChild = false);

std::optional<MathCssPaintOperation> buildMiddlePaintOperation(
    const MathRenderNode* marker, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale) {
  if (!marker || marker->middleDelimiter.isEmpty() || !containingNode)
    return std::nullopt;
  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  const QString character = mathMiddleDelimiterCharacter(
      marker->middleDelimiter);
  const auto glyph = font.glyph(character);
  if (!glyph || glyph->glyphIndex == 0) return std::nullopt;

  const qreal fontScale = mathStyleScale(marker);
  const QRectF layoutInk = middleDelimiterLayoutBounds(
      marker->middleDelimiter, fontScale);
  if (layoutInk.isEmpty()) return std::nullopt;
  const QRectF rasterInk = font.rasterGlyphBounds(
      glyph->glyphIndex, fontScale);
  if (rasterInk.isEmpty()) return std::nullopt;
  const qreal allocationWidth = cssNodeWidth(marker, renderScale);
  const qreal spacing = font.pixelSize() * kMathMlMiddleSpaceEm *
                        fontScale;
  const qreal left = containingRect.left() +
      cssNodeOffset(containingNode, marker, renderScale).value_or(0.0);
  const qreal height = std::floor(layoutInk.height() + 0.001);
  const QRectF box(left + spacing,
                   containingRect.top() +
                       (containingRect.height() - height) / 2.0,
                   glyph->advance * fontScale, height);

  MathCssPaintOperation operation;
  operation.payload = MathCssMiddlePaintOperation{};
  auto& result = std::get<MathCssMiddlePaintOperation>(operation.payload);
  result.character = character;
  result.container = box;
  result.allocation = QRectF(left, box.top(), allocationWidth, height);
  result.lineAscent = std::max<qreal>(
      0.0, containingRect.height() -
               (symbolCount(containingNode) > 1 ? 1.0 : 0.0));
  result.glyphRun.text = character;
  result.glyphRun.glyphIndexes = {glyph->glyphIndex};
  result.glyphRun.positions = {QPointF()};
  result.glyphRun.fontScale = fontScale;
  result.glyphRun.advance = glyph->advance * fontScale;
  result.glyphRun.inkBounds = rasterInk.translated(
      box.topLeft() - rasterInk.topLeft());
  result.glyphRun.baselineOrigin = box.topLeft() - rasterInk.topLeft();
  result.glyphRun.clip = box;
  result.glyph = buildInlineFenceGlyphOperation(character, box);
  if (result.glyph) {
    result.glyph->character = character;
    result.glyph->fixedGlyphIndex = glyph->glyphIndex;
    result.glyph->advance = glyph->advance;
    result.glyph->inkBounds = font.rasterGlyphBounds(glyph->glyphIndex);
    result.glyph->realizedExtent = result.glyph->inkBounds.height();
    result.glyph->clipToBlockExtent = false;
  }
  return operation;
}

const MathRenderNode* leftRightBody(const MathRenderNode* leftRight) {
  if (!leftRight || leftRight->kind != MathRenderKind::LeftRight ||
      leftRight->children.empty())
    return nullptr;
  const size_t bodyIndex = leftRight->leftDelimiter == QLatin1String(".")
      ? 0 : 1;
  return bodyIndex < leftRight->children.size()
      ? leftRight->children[bodyIndex].get() : nullptr;
}

std::optional<MathCssPaintOperation> buildLeftRightOperation(
    const MathRenderNode* leftRight, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize,
    MathMlStyleContext style) {
  const MathRenderNode* bodyNode = leftRightBody(leftRight);
  if (!bodyNode) return std::nullopt;

  const qreal width = cssNodeWidth(leftRight, renderScale);
  const bool participatesInRootRow =
      symbolCount(containingNode) > symbolCount(leftRight);
  const qreal height = participatesInRootRow
      ? std::ceil(cssNodeHeight(leftRight, renderScale))
      : containingRect.height();
  const qreal left = containingRect.left() + cssNodeOffset(
      containingNode, leftRight, renderScale).value_or(0.0);
  const qreal top = containingRect.top() +
      (containingRect.height() - height) / 2.0;
  const qreal leftWidth = arrayDelimiterWidth(
      leftRight->leftDelimiter, height);
  const qreal rightWidth = arrayDelimiterWidth(
      leftRight->rightDelimiter, height);
  const qreal bodyWidth = middleDelimiterMarker(bodyNode)
      ? leftRightBodyCssWidth(bodyNode, renderScale)
      : cssNodeWidth(bodyNode, renderScale);
  const qreal bodyHeight = std::min(height, cssNodeHeight(bodyNode, renderScale));

  MathCssPaintOperation operation;
  operation.payload = MathCssLeftRightOperation{};
  auto& result = std::get<MathCssLeftRightOperation>(operation.payload);
  result.container = QRectF(left, top, width, height);
  result.body = QRectF(left + leftWidth,
                       top + (height - bodyHeight) / 2.0,
                       bodyWidth, bodyHeight);
  result.leftDelimiter = QRectF(left, top, leftWidth, height);
  result.rightDelimiter = QRectF(
      result.body.right(), top, rightWidth, height);
  result.leftDelimiterCharacter = mathDelimiterCharacter(
      leftRight->leftDelimiter);
  result.rightDelimiterCharacter = mathDelimiterCharacter(
      leftRight->rightDelimiter);
  result.leftDelimiterGlyph = buildVerticalGlyphOperation(
      result.leftDelimiterCharacter, result.leftDelimiter);
  result.rightDelimiterGlyph = buildVerticalGlyphOperation(
      result.rightDelimiterCharacter, result.rightDelimiter);
  result.lineAscent = height / 2.0 + kChromiumMathAxisOffsetPx;

  const auto appendRegion = [&](const MathRenderNode* node, QRectF box) {
    if (!node || box.isEmpty()) return;
    MathCssLeftRightBodyRegion region;
    region.box = box;
    region.node = node;
    if (auto runs = buildGlyphRunOperations(
            node, box, style.fontScale(), true))
      region.glyphRuns = std::move(*runs);
    result.bodyRegions.push_back(std::move(region));

    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(node, &nodes);
    for (const MathRenderNode* childNode : nodes) {
      if (auto child = buildPaintOperation(
              childNode, node, box, renderScale, cssRootFontPixelSize,
              style))
        operation.children.push_back(std::move(*child));
    }
  };

  if (bodyNode->kind == MathRenderKind::Span) {
    qreal middleLineHeight = 0.0;
    for (const auto& child : bodyNode->children) {
      const MathRenderNode* marker = middleDelimiterMarker(child.get());
      if (!marker) continue;
      const QRectF layoutBounds = middleDelimiterLayoutBounds(
          marker->middleDelimiter, mathStyleScale(marker));
      if (layoutBounds.isEmpty()) continue;
      middleLineHeight = std::max(
          middleLineHeight, layoutBounds.height());
    }
    qreal cursor = result.body.left();
    for (const auto& child : bodyNode->children) {
      if (!child) continue;
      const MathRenderNode* middleMarker =
          middleDelimiterMarker(child.get());
      const qreal childWidth = middleMarker
          ? leftRightBodyCssWidth(child.get(), renderScale)
          : cssNodeWidth(child.get(), renderScale);
      const bool nestedLeftRight =
          child->kind == MathRenderKind::LeftRight;
      const QRectF childBox(
          cursor,
          nestedLeftRight ? result.container.top() : result.body.top(),
          childWidth,
          nestedLeftRight ? result.container.height() : result.body.height());
      if (middleMarker) {
        MathCssMiddleDelimiterOperation middle;
        middle.character = mathMiddleDelimiterCharacter(
            middleMarker->middleDelimiter);
        const QRectF layoutBounds = middleDelimiterLayoutBounds(
            middleMarker->middleDelimiter, mathStyleScale(middleMarker));
        const qreal middleHeight = !layoutBounds.isEmpty()
            ? layoutBounds.height() : result.container.height();
        const qreal middleSpace =
            OpenTypeMathFont::instance().pixelSize() *
            kMathMlMiddleSpaceEm;
        middle.box = QRectF(
            cursor + middleSpace,
            result.container.top() +
                (result.container.height() - middleLineHeight) / 2.0,
            std::max<qreal>(0.0, childWidth - 2.0 * middleSpace),
            middleHeight);
        middle.glyph = buildVerticalGlyphOperation(
            middle.character, middle.box, false);
        result.middleDelimiters.push_back(std::move(middle));
      } else {
        appendRegion(child.get(), childBox);
      }
      cursor += childWidth;
    }
  } else {
    appendRegion(bodyNode, result.body);
  }
  return operation;
}

std::optional<MathCssPaintOperation> buildAccentOperation(
    const MathRenderNode* owner, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize,
    MathMlStyleContext style, bool topLevel = false,
    bool rootRowChild = false) {
  const MathRenderNode* accent = ownedHorizontalAccent(owner);
  if (!accent || accent->children.size() < 2) return std::nullopt;

  MathLayoutResult probe;
  probe.root = cloneNode(*owner);
  probe.naturalSize = QSizeF(owner->width, owner->height + owner->depth);
  probe.size = probe.naturalSize;
  probe.baseline = owner->height;
  const qreal renderFontPixelSize = cssRootFontPixelSize / renderScale;
  auto localBox = layoutMathMlAccentBox(
      probe, renderFontPixelSize, cssRootFontPixelSize);
  if (!localBox) return std::nullopt;
  MathCssBox localRoot = layoutMathMlCssBox(
      probe, renderFontPixelSize, cssRootFontPixelSize);

  QRectF localBounds;
  for (const QRectF& component : {localBox->body, localBox->accent,
                                  localBox->annotation}) {
    if (component.isEmpty()) continue;
    localBounds = localBounds.isNull() ? component
                                       : localBounds.united(component);
  }
  if (!localBounds.isNull()) {
    const QPointF normalize(-localBounds.left(), 0.0);
    localBox->body.translate(normalize);
    localBox->accent.translate(normalize);
    localBox->annotation.translate(normalize);
    localRoot.width = localBounds.width();
  }

  const bool brace = accent->accentKind == MathAccentKind::UnderBrace ||
                     accent->accentKind == MathAccentKind::OverBrace;
  if (!topLevel && brace) {
    const qreal nestedAccentHeight = std::floor(
        localBox->accent.height() *
        OpenTypeMathFont::instance().constants().scriptPercentScaleDown);
    const qreal reduction = localBox->accent.height() - nestedAccentHeight;
    localBox->accent.setHeight(nestedAccentHeight);
    if (localBox->over) {
      localBox->body.translate(0.0, -reduction);
    } else {
      localBox->annotation.translate(0.0, -reduction);
    }
    localRoot.height -= reduction;
  }
  if (rootRowChild &&
      accent->accentKind == MathAccentKind::OverBrace) {
    const MathFontConstants& constants =
        OpenTypeMathFont::instance().constants();
    const qreal allocationHeight = std::round(
        constants.overbarVerticalGap +
        2.0 * constants.overbarRuleThickness);
    const qreal operatorHeight = std::round(constants.overbarVerticalGap);
    const qreal rowCollapse = std::max<qreal>(
        0.0, allocationHeight - operatorHeight);
    localBox->body.translate(0.0, -rowCollapse);
    localRoot.height -= rowCollapse;
  }

  const qreal left = containingRect.left() +
      cssNodeOffset(containingNode, owner, renderScale).value_or(0.0);
  qreal localHeight = std::max({localBox->body.bottom(),
                                localBox->accent.bottom(),
                                localBox->annotation.bottom()});
  if (accent->accentKind == MathAccentKind::Under)
    localHeight += OpenTypeMathFont::instance().constants()
                       .underbarExtraDescender;
  const qreal top = topLevel ? containingRect.top()
      : containingRect.top() +
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
  result.accentKind = accent->accentKind;
  result.box = *localBox;
  result.box.body.translate(translation);
  result.box.accent.translate(translation);
  result.box.annotation.translate(translation);
  result.container = QRectF(left, top, localRoot.width, localHeight);
  result.bodyNode = body;
  result.annotationNode = annotation;
  if (auto runs = buildGlyphRunOperations(
          body, result.box.body, style.fontScale(), true))
    result.bodyGlyphRuns = std::move(*runs);
  if (annotation && !result.box.annotation.isEmpty()) {
    const qreal annotationWidth = cssNodeWidth(annotation, renderScale);
    const MathRenderNode* annotationSemantic =
        primarySemanticNode(annotation);
    const bool annotationOwnsCssLayout =
        annotationSemantic != nullptr || containsTextModeRun(annotation);
    const MathFontConstants& constants =
        OpenTypeMathFont::instance().constants();
    qreal annotationHeight = annotationOwnsCssLayout
        ? cssNodeHeight(annotation, renderScale)
        : std::round(cssNodeHeight(annotation, renderScale) *
                     constants.scriptPercentScaleDown);
    if (annotationSemantic &&
        annotationSemantic->semanticKind == MathSemanticKind::Fraction &&
        annotationSemantic->fractionHasBarLine)
      annotationHeight += constants.fractionRuleThickness;
    const qreal annotationTop = annotationOwnsCssLayout && result.box.over
        ? result.container.top() + constants.overbarExtraAscender
        : result.box.over
        ? result.box.annotation.bottom() - annotationHeight
        : result.box.annotation.bottom() -
              constants.underbarExtraDescender - annotationHeight;
    result.annotationContent = QRectF(
        result.box.annotation.center().x() - annotationWidth / 2.0,
        annotationTop,
        annotationWidth, annotationHeight);
    if (auto runs = buildGlyphRunOperations(
            annotation, result.annotationContent,
            style.scriptStyle().fontScale(), true))
      result.annotationGlyphRuns = std::move(*runs);
  }
  result.lineAscent = result.container.height() / 2.0 +
                      kChromiumMathAxisOffsetPx;

  const auto appendRegion = [&](const MathRenderNode* regionNode,
                                QRectF regionRect,
                                MathMlStyleContext regionStyle) {
    if (!regionNode || regionRect.isEmpty()) return;
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(regionNode, &nodes);
    for (const MathRenderNode* nested : nodes) {
      if (auto child = buildPaintOperation(
              nested, regionNode, regionRect, renderScale,
              cssRootFontPixelSize, regionStyle))
        operation.children.push_back(std::move(*child));
    }
  };
  appendRegion(body, result.box.body, style);
  appendRegion(annotation, result.annotationContent, style.scriptStyle());

  const OpenTypeMathFont& font = OpenTypeMathFont::instance();
  MathCssHorizontalGlyphOperation& glyph = result.glyph;
  glyph.target = result.box.accent;
  glyph.character = result.box.character;
  const bool basicAccent = !basicAccentCharacter(accent).isEmpty();
  const bool cappedAssembly =
      result.box.character == QString(QChar(0x21D2));
  const bool operatorAssembly =
      result.box.character == QString(QChar(0x2190)) ||
      cappedAssembly;
  glyph.selectionTarget = accent->accentUsesNaturalWidth || basicAccent
      ? result.box.accent.width()
      : result.box.body.width();
  glyph.fontScale = result.box.fontScale * style.fontScale();
  const MathRenderNode* bodySemantic = primarySemanticNode(body);
  const bool scaleVariantSelection =
      accent->accentKind == MathAccentKind::UnderBrace && !bodySemantic;
  qreal stretchTarget = glyph.selectionTarget *
      (scaleVariantSelection ? glyph.fontScale : 1.0);
  if (cappedAssembly)
    stretchTarget = std::min(
        stretchTarget, std::floor(3.0 * result.box.accent.width()));
  glyph.placementExtent = cappedAssembly
      ? stretchTarget : glyph.selectionTarget;
  if (rootRowChild && accent->accentKind == MathAccentKind::Under) {
    glyph.paintOffset.setY(std::round(
        2.0 * font.constants().underbarRuleThickness));
  }
  glyph.scalePolicy = accent->accentUsesNaturalWidth ||
          !result.bodyGlyphRuns.isEmpty() || bodySemantic
      ? MathCssHorizontalScalePolicy::PreserveVariantScale
      : MathCssHorizontalScalePolicy::StretchToTarget;
  if (result.box.character == QString(QChar(0x23E0)) ||
      result.box.character == QString(QChar(0x23E1)))
    glyph.scalePolicy =
        MathCssHorizontalScalePolicy::StretchInkToPlacementExtent;
  const auto natural = accent->accentUsesNaturalWidth || basicAccent
      ? font.glyph(result.box.character)
      : std::optional<MathGlyphMetrics>{};
  const HorizontalAccentSelection selection = natural
      ? HorizontalAccentSelection{}
      : selectHorizontalAccent(result.box.character, stretchTarget,
                               brace || operatorAssembly);
  if (natural) {
    glyph.kind = MathCssHorizontalGlyphKind::FixedVariant;
    glyph.fixedGlyphIndex = natural->glyphIndex;
    glyph.realizedExtent = natural->advance;
    glyph.inkBounds = font.rasterGlyphBounds(natural->glyphIndex,
                                             glyph.fontScale);
  } else if (selection.text) {
    glyph.kind = MathCssHorizontalGlyphKind::ShapedText;
    glyph.text = result.box.character;
    glyph.textGlyphIndexes = selection.text->glyphIndexes;
    glyph.textGlyphPositions = selection.text->positions;
    glyph.realizedExtent = selection.text->advance;
    glyph.inkBounds = selection.text->inkBounds;
  } else if (selection.fixed) {
    glyph.kind = MathCssHorizontalGlyphKind::FixedVariant;
    glyph.fixedGlyphIndex = selection.fixed->glyphIndex;
    glyph.realizedExtent = selection.fixed->extent;
    glyph.inkBounds = font.rasterGlyphBounds(selection.fixed->glyphIndex,
                                             glyph.fontScale);
  } else if (selection.assembly) {
    glyph.kind = MathCssHorizontalGlyphKind::Assembly;
    glyph.realizedExtent = selection.assembly->extent * glyph.fontScale;
    glyph.italicCorrection =
        selection.assembly->italicCorrection * glyph.fontScale;
    glyph.parts.reserve(selection.assembly->parts.size());
    for (const MathGlyphAssemblyPart& part : selection.assembly->parts) {
      QRectF partInk = font.rasterGlyphBounds(part.glyphIndex,
                                              glyph.fontScale);
      glyph.parts.push_back({part.glyphIndex,
                             partInk,
                             part.offset * glyph.fontScale,
                             part.fullAdvance * glyph.fontScale,
                             part.connectorOverlap * glyph.fontScale,
                             part.extender});
      partInk.translate(part.offset * glyph.fontScale, 0.0);
      glyph.inkBounds = glyph.inkBounds.isNull()
          ? partInk : glyph.inkBounds.united(partInk);
    }
  } else {
    return std::nullopt;
  }
  return operation;
}

std::optional<MathCssPaintOperation> buildArrayOperation(
    const MathRenderNode* array, const MathRenderNode* containingNode,
    QRectF containingRect, qreal renderScale, qreal cssRootFontPixelSize,
    MathMlStyleContext style) {
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
      OpenTypeMathFont::instance().pixelSize() *
      kMathTableCellHorizontalPaddingEm;
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
  const bool wrappedByLeftRight =
      enclosingKind(containingNode, array, MathRenderKind::LeftRight);
  const bool wrappedMiddleArray =
      wrappedByLeftRight && middleDelimiterMarker(array);
  const qreal middleLeading = wrappedMiddleArray
      ? 2.0 * std::floor(OpenTypeMathFont::instance()
                             .constants().fractionRuleThickness)
      : 0.0;
  const bool fillsContainingRegion =
      primarySemanticNode(containingNode) == array &&
      (symbolCount(containingNode) == symbolCount(array) ||
       wrappedByLeftRight);
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
      ? containingRect.height() + middleLeading
      : std::max(tableHeight, std::ceil(delimiterHeight));
  const qreal top = fillsContainingRegion
      ? containingRect.top() - middleLeading / 2.0
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
  result.leftDelimiterGlyph = buildVerticalGlyphOperation(
      result.leftDelimiterCharacter, result.leftDelimiter);
  result.rightDelimiterGlyph = buildVerticalGlyphOperation(
      result.rightDelimiterCharacter, result.rightDelimiter);
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
      if (semantic && semantic->semanticKind == MathSemanticKind::Radical &&
          hasNestedSemantic(semantic, MathSemanticKind::SupSub)) {
        const MathFontConstants& constants =
            OpenTypeMathFont::instance().constants();
        cell.height -= constants.radicalExtraAscender -
                       constants.radicalRuleThickness / 2.0;
      }
      cell.height -= embeddedBraceStyleReduction(cell.node);
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
        cell.height = std::max(cell.height, std::ceil(
            rootLeftRightFenceExtent(cell.node, semantic, cell.height)));
      }
      if (semantic && ownsGenericPaintOperation(semantic)) {
        const QRectF probeRect(0.0, 0.0, cell.width, cell.height);
        if (const auto probe = buildPaintOperation(
                semantic, cell.node, probeRect, renderScale,
                cssRootFontPixelSize, style)) {
          cell.baseline = semantic->semanticKind == MathSemanticKind::SupSub &&
                                  semantic->scriptKind == MathScriptKind::Superscript
              ? semantic->height
              : semantic->semanticKind == MathSemanticKind::SupSub &&
                        semantic->scriptKind == MathScriptKind::Subscript
                  ? std::round(semantic->height)
              : semantic->semanticKind == MathSemanticKind::SupSub ||
                        (semantic->semanticKind == MathSemanticKind::Radical &&
                         !hasNestedSemantic(
                             semantic, MathSemanticKind::SupSub))
                  ? probe->lineAscent()
              : semantic->semanticKind == MathSemanticKind::Radical
                  ? probe->alignmentBaseline() -
                        OpenTypeMathFont::instance().constants()
                            .radicalExtraAscender
                  : probe->alignmentBaseline();
        }
      }
      if (semantic && semantic->semanticKind == MathSemanticKind::Fraction &&
          firstKind(cell.node, MathRenderKind::LeftRight)) {
        cell.baseline = cell.height / 2.0 +
                        OpenTypeMathFont::instance().constants().axisHeight;
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
      cell.baseline = measured.baseline;
      cell.contentNode = measured.node;
      if (auto runs = buildGlyphRunOperations(
              cell.contentNode, cell.content, style.fontScale(), true))
        cell.glyphRuns = std::move(*runs);
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
              cssRootFontPixelSize, style))
        operation.children.push_back(std::move(*child));
    }
  }
  return operation;
}

std::optional<MathCssPaintOperation> buildFractionOperation(
    const MathRenderNode* fraction, const MathRenderNode* containingNode,
    qreal fractionOffset, qreal semanticTop, qreal semanticHeight,
    qreal renderScale, qreal cssRootFontPixelSize, bool nested,
    MathMlStyleContext style) {
  const auto metrics = fractionMetrics(
      fraction, cssRootFontPixelSize, renderScale);
  if (!metrics) return std::nullopt;
  const MathRenderNode* stack = directChild(fraction, MathRenderKind::VList);
  if (!stack) return std::nullopt;
  const MathMlStyleContext rowStyle = fractionRowStyle(fraction, style);
  const qreal contentWidth = cssNodeWidth(stack, renderScale) + 2.0;
  const qreal targetHeight = (fraction->height + fraction->depth) * renderScale;
  QString leftDelimiter = fraction->leftDelimiter;
  QString rightDelimiter = fraction->rightDelimiter;
  bool inheritedLeftDelimiter = false;
  if (const MathRenderNode* leftRight = enclosingKind(
          containingNode, fraction, MathRenderKind::LeftRight)) {
    if (leftDelimiter.isEmpty()) {
      leftDelimiter = leftRight->leftDelimiter;
      inheritedLeftDelimiter = true;
    }
    if (rightDelimiter.isEmpty()) rightDelimiter = leftRight->rightDelimiter;
  }
  const qreal leftWidth = arrayDelimiterWidth(
      leftDelimiter, targetHeight);
  const qreal rightWidth = arrayDelimiterWidth(
      rightDelimiter, targetHeight);
  if (inheritedLeftDelimiter) fractionOffset -= leftWidth;
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
      ? middleDelimiterMarker(fraction)
            ? semanticTop + alignmentRule / 2.0
            : semanticTop + (semanticHeight - fractionHeight) / 2.0 -
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
  // Row paint boxes must contain the extents used to size the fraction.
  numeratorHeight = std::max(
      numeratorHeight, metrics->numeratorInk.top + metrics->numeratorInk.bottom);
  denominatorHeight = std::max(
      denominatorHeight,
      metrics->denominatorInk.top + metrics->denominatorInk.bottom);
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
                               child->radicalGlyph.target.bottom(),
                               child->radicalRule.target.bottom(),
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
  result.leftDelimiterCharacter = mathDelimiterCharacter(leftDelimiter);
  result.rightDelimiterCharacter = mathDelimiterCharacter(rightDelimiter);
  fractionPaint.leftDelimiterGlyph = buildVerticalGlyphOperation(
      result.leftDelimiterCharacter, result.leftDelimiter);
  fractionPaint.rightDelimiterGlyph = buildVerticalGlyphOperation(
      result.rightDelimiterCharacter, result.rightDelimiter);
  if (auto runs = buildGlyphRunOperations(
          metrics->numerator, result.numerator,
          rowStyle.fontScale(), true))
    fractionPaint.numeratorGlyphRuns = std::move(*runs);
  if (auto runs = buildGlyphRunOperations(
          metrics->denominator, result.denominator,
          rowStyle.fontScale(), true))
    fractionPaint.denominatorGlyphRuns = std::move(*runs);

  const auto appendChildren = [&](const MathRenderNode* row, QRectF rowRect) {
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(row, &nodes);
    for (const MathRenderNode* node : nodes) {
      if (auto child = buildPaintOperation(
              node, row, rowRect, renderScale, cssRootFontPixelSize,
              rowStyle))
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
    MathMlStyleContext style, bool topLevel, bool rootRowChild) {
  if (!operationNode || !containingNode) return std::nullopt;
  if (!operationNode->middleDelimiter.isEmpty())
    return buildMiddlePaintOperation(
        operationNode, containingNode, containingRect, renderScale);
  if (operationNode->kind == MathRenderKind::LeftRight)
    return buildLeftRightOperation(
        operationNode, containingNode, containingRect, renderScale,
        cssRootFontPixelSize, style);
  if (ownsAccentPaintOperation(operationNode))
    return buildAccentOperation(
        operationNode, containingNode, containingRect, renderScale,
        cssRootFontPixelSize, style, topLevel, rootRowChild);
  if (operationNode->semanticKind == MathSemanticKind::Fraction) {
    const bool ownsRegion = primarySemanticNode(containingNode) == operationNode;
    const qreal left = containingRect.left() + cssNodeOffset(
        containingNode, operationNode, renderScale).value_or(0.0);
    const qreal fractionHeight = topLevel
        ? fractionMathMlHeight(
              operationNode, cssRootFontPixelSize, renderScale)
        : nestedFractionCssHeight(
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
        operationNode, containingNode, left, top, height, renderScale,
        cssRootFontPixelSize, true, style);
  }
  if (operationNode->semanticKind == MathSemanticKind::Array)
    return buildArrayOperation(
        operationNode, containingNode, containingRect, renderScale,
        cssRootFontPixelSize, style);

  MathCssPaintOperation result;
  if (operationNode->semanticKind == MathSemanticKind::SupSub) {
    auto script = buildScriptOperation(
        operationNode, containingNode, containingRect, renderScale);
    if (!script) return std::nullopt;
    result.payload = std::move(*script);
    if (const MathRenderNode* leftRight = enclosingKind(
            containingNode, operationNode, MathRenderKind::LeftRight)) {
      auto& scriptResult = std::get<MathCssScriptOperation>(result.payload);
      MathCssFencePair fences;
      const qreal leftWidth = arrayDelimiterWidth(
          leftRight->leftDelimiter, containingRect.height());
      const qreal rightWidth = arrayDelimiterWidth(
          leftRight->rightDelimiter, containingRect.height());
      fences.left = QRectF(containingRect.left(), containingRect.top(),
                           leftWidth, containingRect.height());
      fences.right = QRectF(containingRect.right() - rightWidth,
                            containingRect.top(), rightWidth,
                            containingRect.height());
      fences.leftCharacter = mathDelimiterCharacter(
          leftRight->leftDelimiter);
      fences.rightCharacter = mathDelimiterCharacter(
          leftRight->rightDelimiter);
      fences.leftGlyph = buildInlineFenceGlyphOperation(
          fences.leftCharacter, fences.left);
      fences.rightGlyph = buildInlineFenceGlyphOperation(
          fences.rightCharacter, fences.right);
      scriptResult.fences = std::move(fences);
      scriptResult.container = containingRect;
    }
  } else if (operationNode->semanticKind == MathSemanticKind::Radical) {
    auto radical = buildRadicalOperation(
        operationNode, containingNode, containingRect, renderScale, topLevel);
    if (!radical) return std::nullopt;
    result.payload = std::move(*radical);
  } else {
    return std::nullopt;
  }

  const auto appendRegion = [&](const MathRenderNode* regionNode,
                                QRectF regionRect,
                                MathMlStyleContext regionStyle) {
    if (!regionNode || regionRect.isEmpty()) return;
    QVector<const MathRenderNode*> nodes;
    collectImmediatePaintNodes(regionNode, &nodes);
    for (const MathRenderNode* node : nodes) {
      if (auto child = buildPaintOperation(
              node, regionNode, regionRect, renderScale,
              cssRootFontPixelSize, regionStyle))
        result.children.push_back(std::move(*child));
    }
  };
  if (auto* radical = std::get_if<MathCssRadicalOperation>(&result.payload);
      radical && topLevel && !operationNode->radicalIndex) {
    QVector<const MathRenderNode*> bodyOperations;
    collectImmediatePaintNodes(radical->bodyNode, &bodyOperations);
    qreal bodyBaseline = middleDelimiterMarker(radical->bodyNode)
        ? mathMlInlineAscent(style.fontScale())
        : cssRootFontPixelSize;
    if (bodyOperations.size() == 1 &&
        primarySemanticNode(radical->bodyNode) == bodyOperations.front()) {
      if (const auto probe = buildPaintOperation(
              bodyOperations.front(), radical->bodyNode, radical->body,
              renderScale, cssRootFontPixelSize, style)) {
        bodyBaseline = probe->alignmentBaseline();
        radical->body.moveTop(snapLayoutUnit(
            radical->container.top() + result.alignmentBaseline() -
            bodyBaseline));
        if (auto child = buildPaintOperation(
                bodyOperations.front(), radical->bodyNode, radical->body,
                renderScale, cssRootFontPixelSize, style))
          result.children.push_back(std::move(*child));
        return result;
      }
    }
    if (rootRowChild) {
      const auto rowRuns = buildGlyphRunOperations(
          containingNode, containingRect, style.fontScale(), true);
      const auto bodyRuns = buildGlyphRunOperations(
          radical->bodyNode, radical->body, style.fontScale(), true);
      if (rowRuns && !rowRuns->isEmpty() && bodyRuns &&
          !bodyRuns->isEmpty()) {
        radical->body.translate(
            0.0, rowRuns->front().baselineOrigin.y() -
                     bodyRuns->front().baselineOrigin.y());
      }
    } else {
      radical->body.moveTop(snapLayoutUnit(
          radical->container.top() + result.alignmentBaseline() -
          bodyBaseline));
    }
  }
  if (auto* radical = std::get_if<MathCssRadicalOperation>(&result.payload)) {
    if (auto runs = buildGlyphRunOperations(
            radical->bodyNode, radical->body, style.fontScale(), true))
      radical->bodyGlyphRuns = std::move(*runs);
    const MathRenderNode* degreeSymbol = singleSymbol(radical->degreeNode);
    const MathRenderNode* degreePaintNode =
        primarySemanticNode(radical->degreeNode)
        ? nullptr : degreeSymbol ? degreeSymbol : radical->degreeNode;
    if (auto runs = buildGlyphRunOperations(
            degreePaintNode, radical->degree,
            OpenTypeMathFont::instance().constants()
                .scriptScriptPercentScaleDown,
            true))
      radical->degreeGlyphRuns = std::move(*runs);
  }
  if (auto* script = std::get_if<MathCssScriptOperation>(&result.payload)) {
    const std::optional<qreal> cssPositionScale =
        script->largeOperatorGlyph ? std::optional<qreal>(renderScale)
                                   : std::nullopt;
    if (!script->largeOperatorGlyph) {
      if (auto runs = buildGlyphRunOperations(
              script->baseNode, script->base, style.fontScale(), true))
        script->baseGlyphRuns = std::move(*runs);
    }
    if (auto runs = buildGlyphRunOperations(
            script->superscriptNode, script->superscript,
            style.scriptStyle().fontScale(), true, cssPositionScale))
      script->superscriptGlyphRuns = std::move(*runs);
    if (auto runs = buildGlyphRunOperations(
            script->subscriptNode, script->subscript,
            style.scriptStyle().fontScale(), true, cssPositionScale))
      script->subscriptGlyphRuns = std::move(*runs);
  }
  if (const auto* script = std::get_if<MathCssScriptOperation>(&result.payload)) {
    appendRegion(script->baseNode, script->base, style);
    appendRegion(script->superscriptNode, script->superscript,
                 style.scriptStyle());
    appendRegion(script->subscriptNode, script->subscript,
                 style.scriptStyle());
  } else if (const auto* radical =
                 std::get_if<MathCssRadicalOperation>(&result.payload)) {
    appendRegion(radical->bodyNode, radical->body, style);
    appendRegion(radical->degreeNode, radical->degree,
                 MathMlStyleContext{2});
  }
  return result;
}

}  // namespace

MathCssPaintKind MathCssPaintOperation::kind() const {
  if (std::holds_alternative<MathCssGlyphRunGroupOperation>(payload))
    return MathCssPaintKind::GlyphRun;
  if (std::holds_alternative<MathCssRowOperation>(payload))
    return MathCssPaintKind::Row;
  if (std::holds_alternative<MathCssLeftRightOperation>(payload))
    return MathCssPaintKind::LeftRight;
  if (std::holds_alternative<MathCssMiddlePaintOperation>(payload))
    return MathCssPaintKind::MiddleDelimiter;
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
  if (const auto* glyphRun =
          std::get_if<MathCssGlyphRunGroupOperation>(&payload))
    return glyphRun->container;
  if (const auto* row = std::get_if<MathCssRowOperation>(&payload))
    return row->container;
  if (const auto* leftRight =
          std::get_if<MathCssLeftRightOperation>(&payload))
    return leftRight->container;
  if (const auto* middle =
          std::get_if<MathCssMiddlePaintOperation>(&payload))
    return middle->container;
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
  if (const auto* glyphRun =
          std::get_if<MathCssGlyphRunGroupOperation>(&payload))
    return glyphRun->lineAscent;
  if (const auto* row = std::get_if<MathCssRowOperation>(&payload))
    return row->lineAscent;
  if (const auto* leftRight =
          std::get_if<MathCssLeftRightOperation>(&payload))
    return leftRight->lineAscent;
  if (const auto* middle =
          std::get_if<MathCssMiddlePaintOperation>(&payload))
    return middle->lineAscent;
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

namespace {

qreal jsonNumber(qreal value) {
  return std::round(value * 1000000.0) / 1000000.0;
}

QJsonObject rectJson(QRectF rect) {
  return {{QStringLiteral("x"), jsonNumber(rect.x())},
          {QStringLiteral("y"), jsonNumber(rect.y())},
          {QStringLiteral("width"), jsonNumber(rect.width())},
          {QStringLiteral("height"), jsonNumber(rect.height())}};
}

QJsonObject pointJson(QPointF point) {
  return {{QStringLiteral("x"), jsonNumber(point.x())},
          {QStringLiteral("y"), jsonNumber(point.y())}};
}

QJsonArray glyphRunsJson(const QVector<MathCssGlyphRunOperation>& runs) {
  QJsonArray result;
  for (const MathCssGlyphRunOperation& run : runs) {
    QJsonArray glyphIndexes;
    for (quint32 glyphIndex : run.glyphIndexes)
      glyphIndexes.push_back(static_cast<qint64>(glyphIndex));
    QJsonArray positions;
    for (QPointF position : run.positions)
      positions.push_back(pointJson(position));
    result.push_back(QJsonObject{
        {QStringLiteral("text"), run.text},
        {QStringLiteral("fontFamily"), run.fontFamily},
        {QStringLiteral("fontClass"), run.fontClass},
        {QStringLiteral("atomClass"), run.atomClass},
        {QStringLiteral("glyphIndexes"), glyphIndexes},
        {QStringLiteral("positions"), positions},
        {QStringLiteral("baselineOrigin"), pointJson(run.baselineOrigin)},
        {QStringLiteral("advance"), jsonNumber(run.advance)},
        {QStringLiteral("fontScale"), jsonNumber(run.fontScale)},
        {QStringLiteral("inkBounds"), rectJson(run.inkBounds)},
        {QStringLiteral("clip"), rectJson(run.clip)}});
  }
  return result;
}

QJsonObject verticalGlyphJson(const MathCssVerticalGlyphOperation& glyph) {
  QJsonArray parts;
  for (const MathCssVerticalGlyphPart& part : glyph.parts) {
    parts.push_back(QJsonObject{
        {QStringLiteral("glyphIndex"), static_cast<qint64>(part.glyphIndex)},
        {QStringLiteral("position"), pointJson(part.position)},
        {QStringLiteral("inkBounds"), rectJson(part.inkBounds)},
        {QStringLiteral("offset"), jsonNumber(part.offset)},
        {QStringLiteral("fullAdvance"), jsonNumber(part.fullAdvance)},
        {QStringLiteral("connectorOverlap"),
         jsonNumber(part.connectorOverlap)},
        {QStringLiteral("extender"), part.extender}});
  }
  return {
      {QStringLiteral("kind"),
       glyph.kind == MathCssVerticalGlyphKind::Assembly
           ? QStringLiteral("assembly") : QStringLiteral("fixed")},
      {QStringLiteral("scalePolicy"),
       glyph.scalePolicy == MathCssVerticalScalePolicy::FitTargetExtent
           ? QStringLiteral("fitTargetExtent")
           : glyph.scalePolicy ==
                     MathCssVerticalScalePolicy::FitSelectedExtent
               ? QStringLiteral("fitSelectedExtent")
               : QStringLiteral("preserveVariantScale")},
      {QStringLiteral("clipToBlockExtent"), glyph.clipToBlockExtent},
      {QStringLiteral("target"), rectJson(glyph.target)},
      {QStringLiteral("character"), glyph.character},
      {QStringLiteral("baselineOrigin"), pointJson(glyph.baselineOrigin)},
      {QStringLiteral("selectionTarget"), jsonNumber(glyph.selectionTarget)},
      {QStringLiteral("realizedExtent"), jsonNumber(glyph.realizedExtent)},
      {QStringLiteral("advance"), jsonNumber(glyph.advance)},
      {QStringLiteral("italicCorrection"),
       jsonNumber(glyph.italicCorrection)},
      {QStringLiteral("inkBounds"), rectJson(glyph.inkBounds)},
      {QStringLiteral("fixedGlyphIndex"),
       static_cast<qint64>(glyph.fixedGlyphIndex)},
      {QStringLiteral("parts"), parts}};
}

QJsonObject fencePairJson(const MathCssFencePair& fences) {
  QJsonObject result{{QStringLiteral("left"), rectJson(fences.left)},
                     {QStringLiteral("right"), rectJson(fences.right)},
                     {QStringLiteral("leftCharacter"),
                      fences.leftCharacter},
                     {QStringLiteral("rightCharacter"),
                      fences.rightCharacter}};
  if (fences.leftGlyph)
    result.insert(QStringLiteral("leftGlyph"),
                  verticalGlyphJson(*fences.leftGlyph));
  if (fences.rightGlyph)
    result.insert(QStringLiteral("rightGlyph"),
                  verticalGlyphJson(*fences.rightGlyph));
  return result;
}

QString paintKindJsonName(MathCssPaintKind kind) {
  switch (kind) {
    case MathCssPaintKind::GlyphRun: return QStringLiteral("glyph-run");
    case MathCssPaintKind::Row: return QStringLiteral("row");
    case MathCssPaintKind::LeftRight: return QStringLiteral("left-right");
    case MathCssPaintKind::MiddleDelimiter:
      return QStringLiteral("middle-delimiter");
    case MathCssPaintKind::Fraction: return QStringLiteral("fraction");
    case MathCssPaintKind::Radical: return QStringLiteral("radical");
    case MathCssPaintKind::SupSub: return QStringLiteral("supsub");
    case MathCssPaintKind::Array: return QStringLiteral("array");
    case MathCssPaintKind::Accent: return QStringLiteral("accent");
  }
  return {};
}

}  // namespace

QJsonObject MathCssPaintOperation::toJson() const {
  QJsonObject result{
      {QStringLiteral("kind"), paintKindJsonName(kind())},
      {QStringLiteral("container"), rectJson(container())},
      {QStringLiteral("lineAscent"), jsonNumber(lineAscent())}};
  if (const auto* glyphRun =
          std::get_if<MathCssGlyphRunGroupOperation>(&payload)) {
    result.insert(QStringLiteral("runs"), glyphRunsJson(glyphRun->runs));
  } else if (const auto* row =
                 std::get_if<MathCssRowOperation>(&payload)) {
    result.insert(QStringLiteral("glyphRuns"), glyphRunsJson(row->glyphRuns));
  } else if (const auto* leftRight =
                 std::get_if<MathCssLeftRightOperation>(&payload)) {
    result.insert(QStringLiteral("body"), rectJson(leftRight->body));
    result.insert(QStringLiteral("leftDelimiter"),
                  rectJson(leftRight->leftDelimiter));
    result.insert(QStringLiteral("rightDelimiter"),
                  rectJson(leftRight->rightDelimiter));
    if (leftRight->leftDelimiterGlyph)
      result.insert(QStringLiteral("leftDelimiterGlyph"),
                    verticalGlyphJson(*leftRight->leftDelimiterGlyph));
    if (leftRight->rightDelimiterGlyph)
      result.insert(QStringLiteral("rightDelimiterGlyph"),
                    verticalGlyphJson(*leftRight->rightDelimiterGlyph));
    QJsonArray regions;
    for (const MathCssLeftRightBodyRegion& region : leftRight->bodyRegions) {
      regions.push_back(QJsonObject{
          {QStringLiteral("box"), rectJson(region.box)},
          {QStringLiteral("glyphRuns"), glyphRunsJson(region.glyphRuns)}});
    }
    result.insert(QStringLiteral("bodyRegions"), regions);
    QJsonArray middles;
    for (const MathCssMiddleDelimiterOperation& middle :
         leftRight->middleDelimiters) {
      QJsonObject value{{QStringLiteral("character"), middle.character},
                        {QStringLiteral("box"), rectJson(middle.box)}};
      if (middle.glyph)
        value.insert(QStringLiteral("glyph"),
                     verticalGlyphJson(*middle.glyph));
      middles.push_back(value);
    }
    result.insert(QStringLiteral("middleDelimiters"), middles);
  } else if (const auto* middle =
                 std::get_if<MathCssMiddlePaintOperation>(&payload)) {
    result.insert(QStringLiteral("character"), middle->character);
    result.insert(QStringLiteral("allocation"),
                  rectJson(middle->allocation));
    result.insert(QStringLiteral("glyphRun"),
                  glyphRunsJson({middle->glyphRun}).at(0));
    if (middle->glyph)
      result.insert(QStringLiteral("glyph"),
                    verticalGlyphJson(*middle->glyph));
  } else if (const auto* fraction = std::get_if<MathCssFractionPaint>(&payload)) {
    result.insert(QStringLiteral("fraction"), rectJson(fraction->box.fraction));
    result.insert(QStringLiteral("numerator"), rectJson(fraction->box.numerator));
    result.insert(QStringLiteral("denominator"), rectJson(fraction->box.denominator));
    result.insert(QStringLiteral("rule"), rectJson(fraction->box.rule));
    result.insert(QStringLiteral("hasRule"), fraction->box.hasRule);
    result.insert(QStringLiteral("numeratorGlyphRuns"),
                  glyphRunsJson(fraction->numeratorGlyphRuns));
    result.insert(QStringLiteral("denominatorGlyphRuns"),
                  glyphRunsJson(fraction->denominatorGlyphRuns));
    if (fraction->leftDelimiterGlyph)
      result.insert(QStringLiteral("leftDelimiterGlyph"),
                    verticalGlyphJson(*fraction->leftDelimiterGlyph));
    if (fraction->rightDelimiterGlyph)
      result.insert(QStringLiteral("rightDelimiterGlyph"),
                    verticalGlyphJson(*fraction->rightDelimiterGlyph));
  } else if (const auto* script =
                 std::get_if<MathCssScriptOperation>(&payload)) {
    result.insert(QStringLiteral("scriptKind"),
                  static_cast<int>(script->kind));
    if (script->limits) result.insert(QStringLiteral("limits"), true);
    result.insert(QStringLiteral("base"), rectJson(script->base));
    result.insert(QStringLiteral("superscript"), rectJson(script->superscript));
    result.insert(QStringLiteral("subscript"), rectJson(script->subscript));
    result.insert(QStringLiteral("baseGlyphRuns"),
                  glyphRunsJson(script->baseGlyphRuns));
    result.insert(QStringLiteral("superscriptGlyphRuns"),
                  glyphRunsJson(script->superscriptGlyphRuns));
    result.insert(QStringLiteral("subscriptGlyphRuns"),
                  glyphRunsJson(script->subscriptGlyphRuns));
    if (script->largeOperatorGlyph)
      result.insert(QStringLiteral("largeOperatorGlyph"),
                    verticalGlyphJson(*script->largeOperatorGlyph));
    if (script->fences)
      result.insert(QStringLiteral("fences"),
                    fencePairJson(*script->fences));
  } else if (const auto* radical =
                 std::get_if<MathCssRadicalOperation>(&payload)) {
    result.insert(QStringLiteral("body"), rectJson(radical->body));
    result.insert(QStringLiteral("degree"), rectJson(radical->degree));
    result.insert(QStringLiteral("glyph"),
                  rectJson(radical->radicalGlyph.target));
    result.insert(QStringLiteral("rule"),
                  rectJson(radical->radicalRule.target));
    result.insert(QStringLiteral("radicalGlyph"), QJsonObject{
        {QStringLiteral("character"), radical->radicalGlyph.character},
        {QStringLiteral("glyphIndex"),
         static_cast<qint64>(radical->radicalGlyph.glyphIndex)},
        {QStringLiteral("target"), rectJson(radical->radicalGlyph.target)},
        {QStringLiteral("inkBounds"),
         rectJson(radical->radicalGlyph.inkBounds)},
        {QStringLiteral("clip"), rectJson(radical->radicalGlyph.clip)},
        {QStringLiteral("fontScale"),
         jsonNumber(radical->radicalGlyph.fontScale)}});
    result.insert(QStringLiteral("bodyGlyphRuns"),
                  glyphRunsJson(radical->bodyGlyphRuns));
    result.insert(QStringLiteral("degreeGlyphRuns"),
                  glyphRunsJson(radical->degreeGlyphRuns));
    if (radical->fences)
      result.insert(QStringLiteral("fences"),
                    fencePairJson(*radical->fences));
  } else if (const auto* array =
                 std::get_if<MathCssArrayOperation>(&payload)) {
    result.insert(QStringLiteral("table"), rectJson(array->table));
    QJsonArray rows;
    for (QRectF rowRect : array->rows) rows.push_back(rectJson(rowRect));
    result.insert(QStringLiteral("rows"), rows);
    QJsonArray cells;
    for (const MathCssArrayCell& cell : array->cells) {
      cells.push_back(QJsonObject{
          {QStringLiteral("row"), cell.row},
          {QStringLiteral("column"), cell.column},
          {QStringLiteral("box"), rectJson(cell.box)},
          {QStringLiteral("content"), rectJson(cell.content)},
          {QStringLiteral("baseline"), jsonNumber(cell.baseline)},
          {QStringLiteral("glyphRuns"), glyphRunsJson(cell.glyphRuns)}});
    }
    result.insert(QStringLiteral("cells"), cells);
    result.insert(QStringLiteral("leftDelimiter"),
                  array->leftDelimiterCharacter);
    result.insert(QStringLiteral("rightDelimiter"),
                  array->rightDelimiterCharacter);
    if (array->leftDelimiterGlyph)
      result.insert(QStringLiteral("leftDelimiterGlyph"),
                    verticalGlyphJson(*array->leftDelimiterGlyph));
    if (array->rightDelimiterGlyph)
      result.insert(QStringLiteral("rightDelimiterGlyph"),
                    verticalGlyphJson(*array->rightDelimiterGlyph));
  } else if (const auto* accent =
                 std::get_if<MathCssAccentOperation>(&payload)) {
    result.insert(QStringLiteral("accentKind"),
                  static_cast<int>(accent->accentKind));
    result.insert(QStringLiteral("body"), rectJson(accent->box.body));
    result.insert(QStringLiteral("accent"), rectJson(accent->box.accent));
    result.insert(QStringLiteral("annotation"),
                  rectJson(accent->box.annotation));
    result.insert(QStringLiteral("annotationContent"),
                  rectJson(accent->annotationContent));
    result.insert(QStringLiteral("character"), accent->box.character);
    result.insert(QStringLiteral("over"), accent->box.over);
    result.insert(QStringLiteral("bodyGlyphRuns"),
                  glyphRunsJson(accent->bodyGlyphRuns));
    result.insert(QStringLiteral("annotationGlyphRuns"),
                  glyphRunsJson(accent->annotationGlyphRuns));
    const MathCssHorizontalGlyphOperation& glyph = accent->glyph;
    QJsonObject glyphJson{
        {QStringLiteral("kind"),
         glyph.kind == MathCssHorizontalGlyphKind::Assembly
             ? QStringLiteral("assembly")
             : glyph.kind == MathCssHorizontalGlyphKind::ShapedText
                 ? QStringLiteral("shaped-text")
                 : QStringLiteral("fixed")},
        {QStringLiteral("scalePolicy"),
         glyph.scalePolicy ==
                 MathCssHorizontalScalePolicy::PreserveVariantScale
             ? QStringLiteral("preserve-variant")
             : glyph.scalePolicy ==
                       MathCssHorizontalScalePolicy::StretchInkToPlacementExtent
                   ? QStringLiteral("stretch-ink-to-placement")
                   : QStringLiteral("stretch-to-target")},
        {QStringLiteral("target"), rectJson(glyph.target)},
        {QStringLiteral("character"), glyph.character},
        {QStringLiteral("selectionTarget"),
         jsonNumber(glyph.selectionTarget)},
        {QStringLiteral("fontScale"), jsonNumber(glyph.fontScale)},
        {QStringLiteral("realizedExtent"),
         jsonNumber(glyph.realizedExtent)},
        {QStringLiteral("italicCorrection"),
         jsonNumber(glyph.italicCorrection)},
        {QStringLiteral("paintOffset"), pointJson(glyph.paintOffset)},
        {QStringLiteral("inkBounds"), rectJson(glyph.inkBounds)},
        {QStringLiteral("fixedGlyphIndex"),
         static_cast<qint64>(glyph.fixedGlyphIndex)}};
    if (glyph.kind == MathCssHorizontalGlyphKind::ShapedText) {
      glyphJson.insert(QStringLiteral("text"), glyph.text);
      QJsonArray indexes;
      for (quint32 index : glyph.textGlyphIndexes)
        indexes.push_back(static_cast<qint64>(index));
      glyphJson.insert(QStringLiteral("glyphIndexes"), indexes);
      QJsonArray positions;
      for (QPointF position : glyph.textGlyphPositions)
        positions.push_back(pointJson(position));
      glyphJson.insert(QStringLiteral("glyphPositions"), positions);
    }
    QJsonArray parts;
    for (const MathCssHorizontalGlyphPart& part : glyph.parts) {
      parts.push_back(QJsonObject{
          {QStringLiteral("glyphIndex"),
           static_cast<qint64>(part.glyphIndex)},
          {QStringLiteral("inkBounds"), rectJson(part.inkBounds)},
          {QStringLiteral("offset"), jsonNumber(part.offset)},
          {QStringLiteral("fullAdvance"), jsonNumber(part.fullAdvance)},
          {QStringLiteral("connectorOverlap"),
           jsonNumber(part.connectorOverlap)},
          {QStringLiteral("extender"), part.extender}});
    }
    glyphJson.insert(QStringLiteral("parts"), parts);
    result.insert(QStringLiteral("glyph"), glyphJson);
  }
  QJsonArray childrenJson;
  for (const MathCssPaintOperation& child : children)
    childrenJson.push_back(child.toJson());
  result.insert(QStringLiteral("children"), childrenJson);
  return result;
}

QString mathMlPaintFailureCodeName(MathMlPaintFailureCode code) {
  switch (code) {
    case MathMlPaintFailureCode::InvalidLayout:
      return QStringLiteral("invalid-layout");
    case MathMlPaintFailureCode::InvalidFontSize:
      return QStringLiteral("invalid-font-size");
    case MathMlPaintFailureCode::RootGlyphRunsUnavailable:
      return QStringLiteral("root-glyph-runs-unavailable");
    case MathMlPaintFailureCode::ChildOperationUnavailable:
      return QStringLiteral("child-operation-unavailable");
    case MathMlPaintFailureCode::AlignedChildOperationUnavailable:
      return QStringLiteral("aligned-child-operation-unavailable");
    case MathMlPaintFailureCode::AccentOperationUnavailable:
      return QStringLiteral("accent-operation-unavailable");
    case MathMlPaintFailureCode::UnsupportedRootOperation:
      return QStringLiteral("unsupported-root-operation");
    case MathMlPaintFailureCode::FractionOperationUnavailable:
      return QStringLiteral("fraction-operation-unavailable");
    case MathMlPaintFailureCode::RootOperationUnavailable:
      return QStringLiteral("root-operation-unavailable");
  }
  return QStringLiteral("unknown");
}

QString formatMathMlPaintFailure(const MathMlPaintFailure& failure) {
  QString result = QStringLiteral("MathML operation build failed [%1]")
                       .arg(mathMlPaintFailureCodeName(failure.code));
  if (!failure.nodePath.isEmpty())
    result += QStringLiteral(" at %1").arg(failure.nodePath);
  if (!failure.expectedMathMlTag.isEmpty())
    result += QStringLiteral(" (expected <%1>)")
                  .arg(failure.expectedMathMlTag);
  return result;
}

MathMlPaintError::MathMlPaintError(MathMlPaintFailure failure)
    : std::runtime_error(
          formatMathMlPaintFailure(failure).toUtf8().constData()),
      failure_(std::move(failure)) {}

QJsonObject MathMlPaintFailure::toJson() const {
  const auto renderKindName = [](MathRenderKind value) {
    switch (value) {
      case MathRenderKind::Span: return "span";
      case MathRenderKind::Symbol: return "symbol";
      case MathRenderKind::Rule: return "rule";
      case MathRenderKind::Rect: return "rect";
      case MathRenderKind::Sqrt: return "sqrt";
      case MathRenderKind::SupSub: return "sup-sub";
      case MathRenderKind::Fraction: return "fraction";
      case MathRenderKind::Accent: return "accent";
      case MathRenderKind::Phantom: return "phantom";
      case MathRenderKind::Stretchy: return "stretchy";
      case MathRenderKind::LeftRight: return "left-right";
      case MathRenderKind::Array: return "array";
      case MathRenderKind::VList: return "vlist";
      case MathRenderKind::Error: return "error";
    }
    return "unknown";
  };
  const auto semanticKindName = [](MathSemanticKind value) {
    switch (value) {
      case MathSemanticKind::None: return "none";
      case MathSemanticKind::Fraction: return "fraction";
      case MathSemanticKind::Radical: return "radical";
      case MathSemanticKind::SupSub: return "sup-sub";
      case MathSemanticKind::Array: return "array";
    }
    return "unknown";
  };
  QJsonObject result{
      {QStringLiteral("code"), mathMlPaintFailureCodeName(code)},
      {QStringLiteral("renderKind"),
       QString::fromLatin1(renderKindName(renderKind))},
      {QStringLiteral("semanticKind"),
       QString::fromLatin1(semanticKindName(semanticKind))},
      {QStringLiteral("nodePath"), nodePath}};
  if (!expectedMathMlTag.isEmpty())
    result.insert(QStringLiteral("expectedMathMlTag"), expectedMathMlTag);
  return result;
}

MathMlPaintOperationBuildResult buildMathMlPaintOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  const auto fail = [&](MathMlPaintFailureCode code,
                        const MathRenderNode* node) {
    const MathRenderNode* failureNode = node ? node : layout.root.get();
    return MathMlPaintOperationBuildResult{
        std::nullopt,
        MathMlPaintFailure{code,
                           failureNode ? failureNode->kind
                                       : MathRenderKind::Error,
                           failureNode ? failureNode->semanticKind
                                       : MathSemanticKind::None,
                           nodePath(layout.root.get(), failureNode),
                           expectedMathMlTag(failureNode)}};
  };
  const auto succeed = [](MathCssPaintOperation operation) {
    return MathMlPaintOperationBuildResult{std::move(operation),
                                           std::nullopt};
  };
  if (!layout.valid())
    return fail(MathMlPaintFailureCode::InvalidLayout, layout.root.get());
  if (renderFontPixelSize <= 0.0 || cssRootFontPixelSize <= 0.0)
    return fail(MathMlPaintFailureCode::InvalidFontSize, layout.root.get());
  const qreal renderScale = cssRootFontPixelSize / renderFontPixelSize;
  const MathCssBox root = layoutMathMlCssBox(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  QVector<const MathRenderNode*> rootOperations;
  collectImmediatePaintNodes(layout.root.get(), &rootOperations);
  if (rootOperations.isEmpty()) {
    const QRectF rootRect(0.0, 0.0, root.width, root.height);
    auto runs = buildGlyphRunOperations(layout.root.get(), rootRect, 1.0);
    if (!runs) {
      if (const MathRenderNode* accent =
              firstKind(layout.root.get(), MathRenderKind::Accent))
        return fail(MathMlPaintFailureCode::AccentOperationUnavailable,
                    accent);
      return fail(MathMlPaintFailureCode::RootGlyphRunsUnavailable,
                  layout.root.get());
    }
    MathCssPaintOperation operation;
    operation.payload = MathCssGlyphRunGroupOperation{
        rootRect, std::move(*runs), root.baseline};
    return succeed(std::move(operation));
  }

  const QRectF rootRect(0.0, 0.0, root.width, root.height);
  auto rootGlyphRuns = buildGlyphRunOperations(
      layout.root.get(), rootRect, 1.0, true);
  if (rootOperations.size() > 1 || rootGlyphRuns) {
    MathCssPaintOperation row;
    row.payload = MathCssRowOperation{
        rootRect, layout.root.get(),
        rootGlyphRuns ? std::move(*rootGlyphRuns)
                      : QVector<MathCssGlyphRunOperation>{},
        root.baseline};
    row.children.reserve(rootOperations.size());
    for (const MathRenderNode* childNode : rootOperations) {
      auto child = buildPaintOperation(
          childNode, layout.root.get(), rootRect, renderScale,
          cssRootFontPixelSize, {}, true, true);
      if (!child)
        return fail(MathMlPaintFailureCode::ChildOperationUnavailable,
                    childNode);
      row.children.push_back(std::move(*child));
    }
    auto& rowPayload = std::get<MathCssRowOperation>(row.payload);
    const bool containsLargeOperator = std::any_of(
        row.children.cbegin(), row.children.cend(),
        [](const MathCssPaintOperation& child) {
          const auto* script = std::get_if<MathCssScriptOperation>(
              &child.payload);
          return script && script->largeOperatorGlyph.has_value();
        });
    if (containsLargeOperator) {
      auto cssPositionedRuns = buildGlyphRunOperations(
          layout.root.get(), rootRect, 1.0, true, renderScale);
      if (!cssPositionedRuns)
        return fail(MathMlPaintFailureCode::RootGlyphRunsUnavailable,
                    layout.root.get());
      rowPayload.glyphRuns = std::move(*cssPositionedRuns);
    }
    const auto operationContentBaseline =
        [](const MathCssPaintOperation& operation) -> std::optional<qreal> {
      if (const auto* accent =
              std::get_if<MathCssAccentOperation>(&operation.payload);
          accent && !accent->bodyGlyphRuns.isEmpty())
        return accent->bodyGlyphRuns.front().baselineOrigin.y();
      if (const auto* radical =
              std::get_if<MathCssRadicalOperation>(&operation.payload);
          radical && !radical->bodyGlyphRuns.isEmpty())
        return radical->bodyGlyphRuns.front().baselineOrigin.y();
      if (const auto* leftRight =
              std::get_if<MathCssLeftRightOperation>(&operation.payload)) {
        for (const MathCssLeftRightBodyRegion& region :
             leftRight->bodyRegions)
          if (!region.glyphRuns.isEmpty())
            return region.glyphRuns.front().baselineOrigin.y();
      }
      if (const auto* script =
              std::get_if<MathCssScriptOperation>(&operation.payload);
          script && !script->baseGlyphRuns.isEmpty())
        return script->baseGlyphRuns.front().baselineOrigin.y();
      if (const auto* array =
              std::get_if<MathCssArrayOperation>(&operation.payload))
        return array->table.center().y() +
               OpenTypeMathFont::instance().constants().axisHeight;
      if (const auto* fraction =
              std::get_if<MathCssFractionPaint>(&operation.payload))
        return fraction->box.container.center().y() +
               OpenTypeMathFont::instance().constants().axisHeight;
      return std::nullopt;
    };
    const auto* firstAccent = std::get_if<MathCssAccentOperation>(
        &row.children.front().payload);
    const bool rootOwnsBaseline = !rowPayload.glyphRuns.isEmpty() &&
        firstAccent && firstAccent->accentKind == MathAccentKind::Under;
    const qreal contentBaseline = snapLayoutUnit(rootOwnsBaseline
        ? rowPayload.glyphRuns.front().baselineOrigin.y()
        : operationContentBaseline(row.children.front()).value_or(
              rootRect.center().y() +
              OpenTypeMathFont::instance().constants().axisHeight));
    if (!rowPayload.glyphRuns.isEmpty()) {
      const qreal translation =
          contentBaseline - rowPayload.glyphRuns.front().baselineOrigin.y();
      for (MathCssGlyphRunOperation& run : rowPayload.glyphRuns) {
        run.baselineOrigin.ry() += translation;
        run.inkBounds.translate(0.0, translation);
      }
    }
    for (qsizetype index = 0; index < row.children.size(); ++index) {
      if (const auto childBaseline =
              operationContentBaseline(row.children.at(index))) {
        const qreal height = row.children.at(index).container().height();
        const qreal intrinsicBaseline =
            *childBaseline - row.children.at(index).container().top();
        QRectF alignedRect = rootRect;
        alignedRect.setTop(std::clamp(
            contentBaseline - intrinsicBaseline,
            rootRect.top(),
            std::max(rootRect.top(), rootRect.bottom() - height)));
        alignedRect.setHeight(height);
        auto aligned = buildPaintOperation(
            rootOperations.at(index), layout.root.get(), alignedRect,
            renderScale, cssRootFontPixelSize, {}, true, true);
        if (!aligned)
          return fail(
              MathMlPaintFailureCode::AlignedChildOperationUnavailable,
              rootOperations.at(index));
        row.children[index] = std::move(*aligned);
      }
    }
    return succeed(std::move(row));
  }
  const MathRenderNode* owner = rootOperations.front();
  if (ownsAccentPaintOperation(owner)) {
    auto operation = buildAccentOperation(
        owner, layout.root.get(),
        QRectF(0.0, 0.0, root.width, root.height), renderScale,
        cssRootFontPixelSize, {}, true);
    if (!operation)
      return fail(MathMlPaintFailureCode::AccentOperationUnavailable,
                  owner);
    return succeed(std::move(*operation));
  }
  if (!ownsGenericPaintOperation(owner))
    return fail(MathMlPaintFailureCode::UnsupportedRootOperation, owner);
  if (owner->semanticKind == MathSemanticKind::Fraction) {
    qreal offset = cssNodeOffset(
        layout.root.get(), owner, renderScale).value_or(0.0);
    if (const MathRenderNode* leftRight = enclosingKind(
            layout.root.get(), owner, MathRenderKind::LeftRight))
      offset = arrayDelimiterWidth(leftRight->leftDelimiter, root.height);
    auto operation = buildFractionOperation(
        owner, layout.root.get(), offset, 0.0, root.height, renderScale,
        cssRootFontPixelSize, false, {});
    if (!operation)
      return fail(MathMlPaintFailureCode::FractionOperationUnavailable,
                  owner);
    return succeed(std::move(*operation));
  }
  auto operation = buildPaintOperation(
      owner, layout.root.get(), QRectF(0.0, 0.0, root.width, root.height),
      renderScale, cssRootFontPixelSize, {}, true);
  if (!operation)
    return fail(MathMlPaintFailureCode::RootOperationUnavailable, owner);
  return succeed(std::move(*operation));
}

std::optional<MathCssFractionBox> layoutMathMlFractionBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize) {
  const auto operation = buildMathMlPaintOperations(
      layout, renderFontPixelSize, cssRootFontPixelSize).operation;
  if (!operation) return std::nullopt;
  const auto* fraction = std::get_if<MathCssFractionPaint>(
      &operation->payload);
  return fraction ? std::optional<MathCssFractionBox>{fraction->box}
                  : std::nullopt;
}

}  // namespace muffin::math
