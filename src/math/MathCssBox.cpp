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
constexpr qreal kScriptFractionBoxHeightEm = 1.544921875;

qreal snapEighth(qreal value) {
  return std::round(value * 8.0) / 8.0;
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
        cellHeight = std::max(cellHeight,
                              cssNodeHeight(cell, scale));
        plainDescender = plainDescender || hasPlainLatinDescender(cell);
      }
    }
    height += cellHeight + OpenTypeMathFont::instance().pixelSize() *
                           (kMathTableCellVerticalPaddingEm +
                            (plainDescender ? kMathTableDescenderExpansionEm : 0.0));
  }
  return height;
}

qreal fractionCssWidth(const MathRenderNode* fraction, qreal scale) {
  const MathRenderNode* body = directChild(fraction, MathRenderKind::VList);
  const qreal contentWidth = body ? cssNodeWidth(body, scale)
                                  : widestDescendant(fraction, MathRenderKind::VList) * scale;
  return std::max<qreal>(0.0, contentWidth + 2.0);
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

qreal maxFenceVariantExtent(const MathRenderNode* node, qreal minimumExtent) {
  if (node == nullptr) return 0.0;
  qreal extent = 0.0;
  if (node->kind == MathRenderKind::Symbol && node->text.size() == 1 &&
      QStringLiteral("()[]{}|").contains(node->text.front())) {
    const auto variant = OpenTypeMathFont::instance().verticalVariant(node->text,
                                                                      minimumExtent);
    if (variant) extent = variant->extent;
  }
  for (const auto& child : node->children)
    extent = std::max(extent, maxFenceVariantExtent(child.get(), minimumExtent));
  return extent;
}

bool hasNonLatinInkBelowBaseline(const MathRenderNode* node) {
  if (node == nullptr) return false;
  if (node->kind == MathRenderKind::Symbol && node->text.size() == 1 &&
      node->text.front().unicode() > 0x7F) {
    const QChar character = node->text.front();
    const OpenTypeMathFont& font = OpenTypeMathFont::instance();
    const auto glyph = character.isLetter() ? font.mathItalicGlyph(character)
                                             : font.glyph(node->text);
    if (glyph && glyph->inkBounds.bottom() > 0.5) return true;
  }
  for (const auto& child : node->children)
    if (hasNonLatinInkBelowBaseline(child.get())) return true;
  return false;
}

const MathRenderNode* singleSymbol(const MathRenderNode* node) {
  if (node == nullptr) return nullptr;
  if (node->kind == MathRenderKind::Symbol) return node;
  const MathRenderNode* result = nullptr;
  for (const auto& child : node->children) {
    const MathRenderNode* candidate = singleSymbol(child.get());
    if (candidate == nullptr) continue;
    if (result != nullptr) return nullptr;
    result = candidate;
  }
  return result;
}

struct NativeGlyphBox {
  qreal width = 0.0;
  qreal height = 0.0;
};

struct GlyphInkExtents {
  qreal top = 0.0;
  qreal bottom = 0.0;
};

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
      const bool descender = QStringLiteral("gjpqy").contains(character, Qt::CaseInsensitive);
      result.top = fontScale < 0.999 && !descender
          ? (character.isDigit()
                 ? std::round(glyph->inkBounds.height() * fontScale)
                 : std::ceil(glyph->inkBounds.height() * fontScale))
          : pixelRound(-glyph->inkBounds.top());
      if (descender)
        result.bottom = pixelRound(glyph->inkBounds.bottom());
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
  if (symbol->atomClass == QLatin1String("mopen") ||
      symbol->atomClass == QLatin1String("mclose")) {
    const auto variant = font.verticalVariant(symbol->text,
                                              std::numeric_limits<qreal>::max());
    if (variant) width = variant->advance * styleScale;
  }
  if (QStringLiteral("()[]{}|").contains(character)) {
    const auto variant = font.verticalVariant(symbol->text,
                                              std::numeric_limits<qreal>::max());
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
  const bool inkDescender = QStringLiteral("gjpqy").contains(character, Qt::CaseInsensitive);
  qreal height = std::ceil(-glyph->inkBounds.top() +
                           (inkDescender ? glyph->inkBounds.bottom() : 0.0));
  if (binaryOperator) height += 2.0;
  return NativeGlyphBox{width, height};
}

qreal cssNodeWidth(const MathRenderNode* node, qreal scale) {
  if (node == nullptr) return 0.0;
  if (node->kind == MathRenderKind::Rule || node->text == QLatin1String("nulldelimiter"))
    return 0.0;
  if (node->atomClass == QLatin1String("mspace") && node->text == QLatin1String("glue"))
    return 0.0;
  if (node->atomClass == QLatin1String("mopen") ||
      node->atomClass == QLatin1String("mclose")) {
    if (const MathRenderNode* delimiter = singleSymbol(node)) {
      const auto variant = OpenTypeMathFont::instance().verticalVariant(
          delimiter->text, std::numeric_limits<qreal>::max());
      if (variant) return variant->advance;
    }
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
    if (node->mathStyleSize >= 1)
      return OpenTypeMathFont::instance().pixelSize() * kScriptFractionBoxHeightEm;
    return std::ceil((node->height + node->depth) * scale * (32.8125 / 38.864)) - 1.0;
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
      if (hasNestedSemantic(semantic, MathSemanticKind::Fraction)) {
        root.width = fractionCssWidth(semantic, scale);
        root.advance = root.width;
        root.height = std::ceil(semantic->height + semantic->depth) + 1.0;
      } else {
        root.width = fractionCssWidth(semantic, scale);
        root.height = std::ceil(layout.naturalSize.height() * (32.8125 / 38.864)) - 1.0;
        if (hasNestedSemantic(semantic, MathSemanticKind::SupSub))
          root.height += OpenTypeMathFont::instance().constants().fractionRuleThickness;
        if (hasNonLatinInkBelowBaseline(semantic))
          root.height += OpenTypeMathFont::instance().constants().fractionRuleThickness / 4.0;
      }
      root.advance = root.width;
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
      root.width = scaledWidth;
      if (semantic && semantic->children.size() == 2) {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        const qreal base = cssNodeWidth(semantic->children.front().get(), scale);
        const qreal scripts = cssNodeWidth(semantic->children.back().get(), scale);
        root.width = snapEighth(base + scripts + constants.spaceAfterScript);
      }
      if (semantic && hasAtomClass(semantic, QLatin1StringView("mop")))
        root.width += 2.0 * OpenTypeMathFont::instance().constants().spaceAfterScript;
      if (semantic && semantic->children.size() == 2 &&
          symbolCount(layout.root.get()) > symbolCount(semantic))
        root.width = cssNodeWidth(layout.root.get(), scale) -
                     OpenTypeMathFont::instance().constants().spaceAfterScript;
      root.advance = root.width;
      {
        const MathFontConstants& constants = OpenTypeMathFont::instance().constants();
        if (semantic->children.size() >= 3 &&
            hasAtomClass(semantic, QLatin1StringView("mop"))) {
          const GlyphInkExtents sub = glyphInkExtents(
              semantic->children.front().get(), constants.scriptPercentScaleDown);
          const GlyphInkExtents sup = glyphInkExtents(
              semantic->children.back().get(), constants.scriptPercentScaleDown);
          const qreal operatorHeight = std::ceil(
              maxLargeOperatorExtent(semantic->children[1].get()));
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
      break;
    case MathSemanticKind::Array:
      root.width = arrayCssWidth(semantic, scale);
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
        root.height = std::max(root.height, std::ceil(
            maxFenceVariantExtent(layout.root.get(), root.height)));
        if (const MathRenderNode* accent = firstKind(layout.root.get(), MathRenderKind::Accent);
            accent && accent->children.size() >= 2) {
          const MathRenderNode* body = accent->children.front().get();
          qreal bodyHeight = std::ceil((body->height + body->depth) * scale);
          if (symbolCount(body) == 1) {
            if (const auto glyph = nativeGlyphBox(singleSymbol(body))) bodyHeight = glyph->height;
          } else if (hasAtomClass(body, QLatin1StringView("mbin"))) {
            bodyHeight += 1.0;
          }
          const MathRenderNode* accentPart = accent->children.back().get();
          qreal accentHeight = 0.0;
          if (const MathRenderNode* symbol = singleSymbol(accentPart)) {
            if (const auto glyph = OpenTypeMathFont::instance().glyph(symbol->text))
              accentHeight = std::ceil(glyph->inkBounds.height());
          } else if (const MathRenderNode* stretchy = firstKind(accentPart, MathRenderKind::Stretchy);
                     stretchy && stretchy->pathName == QLatin1String("vec")) {
            if (const auto glyph = OpenTypeMathFont::instance().glyph(QString(QChar(0x20D7))))
              accentHeight = std::ceil(glyph->inkBounds.height());
          }
          root.height = bodyHeight + accentHeight +
                        OpenTypeMathFont::instance().constants().fractionRuleThickness;
        }
      }
      root.advance = root.width;
      break;
  }
  root.baseline = root.height / 2.0 + kChromiumMathAxisOffsetPx;
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

}  // namespace muffin::math
