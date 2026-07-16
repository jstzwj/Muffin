#include "math/MathCssBox.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace muffin::math {
namespace {

constexpr qreal kChromiumMathAxisOffsetPx = 6.0;
constexpr qreal kNativeScriptInlineScale = 0.92;
constexpr qreal kNativeSubscriptInlineScale = 0.74;
constexpr qreal kNestedFractionInlineScale = 0.873;
constexpr qreal kRadicalOperatorWidthPx = 12.0;
constexpr qreal kRadicalRightOverhangPx = 0.657;

qreal snapEighth(qreal value) {
  return std::round(value * 8.0) / 8.0;
}

const MathRenderNode* semanticNode(const MathRenderNode* node,
                                   MathSemanticKind kind) {
  if (node == nullptr) return nullptr;
  if (node->semanticKind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* result = semanticNode(child.get(), kind)) return result;
  return nullptr;
}

const MathRenderNode* primarySemanticNode(const MathRenderNode* root) {
  for (const MathSemanticKind kind : {MathSemanticKind::Array,
                                      MathSemanticKind::Radical,
                                      MathSemanticKind::Fraction,
                                      MathSemanticKind::SupSub})
    if (const auto* result = semanticNode(root, kind)) return result;
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

qreal fractionCssWidth(const MathRenderNode* fraction, qreal scale) {
  const MathRenderNode* body = directChild(fraction, MathRenderKind::VList);
  const qreal contentWidth = (body ? body->width
                                   : widestDescendant(fraction, MathRenderKind::VList)) * scale;
  if (hasNestedSemantic(fraction, MathSemanticKind::Fraction))
    return snapEighth(contentWidth * kNestedFractionInlineScale + 2.0);
  return std::max<qreal>(11.0, std::round(contentWidth) + 2.0);
}

int symbolCount(const MathRenderNode* node) {
  if (node == nullptr) return 0;
  int count = node->kind == MathRenderKind::Symbol ? 1 : 0;
  for (const auto& child : node->children) count += symbolCount(child.get());
  return count;
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

std::optional<NativeGlyphBox> nativeGlyphBox(const MathRenderNode* symbol) {
  if (symbol == nullptr || symbol->text.size() != 1) return std::nullopt;
  switch (symbol->text.front().unicode()) {
    case 'a': case 'x': return NativeGlyphBox{9.0, 7.0};
    case 'b': case 'd': case 'f': return NativeGlyphBox{9.0, 11.0};
    case 'c': return NativeGlyphBox{7.0, 7.0};
    case 'g': return NativeGlyphBox{10.0, 10.0};
    case 'i': return NativeGlyphBox{5.0, 10.0};
    case 'j': return NativeGlyphBox{6.0, 13.0};
    case 'l': return NativeGlyphBox{5.0, 11.0};
    case 'm': return NativeGlyphBox{13.0, 7.0};
    case 'w': return NativeGlyphBox{12.0, 7.0};
    case 'y': return NativeGlyphBox{9.0, 10.0};
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return NativeGlyphBox{9.0, 10.0};
    case '+': return NativeGlyphBox{12.0, 11.0};
    default: return std::nullopt;
  }
}

qreal cssNodeWidth(const MathRenderNode* node, qreal scale) {
  if (node == nullptr) return 0.0;
  if (node->semanticKind == MathSemanticKind::Fraction)
    return fractionCssWidth(node, scale);
  if (node->semanticKind == MathSemanticKind::SupSub && node->children.size() == 2) {
    const qreal base = cssNodeWidth(node->children.front().get(), scale);
    const qreal scriptScale = node->scriptKind == MathScriptKind::Subscript
        ? kNativeSubscriptInlineScale : kNativeScriptInlineScale;
    qreal width = snapEighth(base + node->children.back()->width * scriptScale);
    if (node->scriptKind != MathScriptKind::Subscript) width = std::max<qreal>(16.125, width);
    return width;
  }
  if (node->kind == MathRenderKind::Symbol) {
    if (const auto glyph = nativeGlyphBox(node)) return glyph->width;
    return std::round(node->width * scale);
  }
  if (node->children.empty()) return node->width * scale;
  if (node->kind == MathRenderKind::VList) {
    qreal width = 0.0;
    for (const auto& child : node->children)
      width = std::max(width, cssNodeWidth(child.get(), scale));
    return width;
  }
  qreal width = 0.0;
  for (const auto& child : node->children) width += cssNodeWidth(child.get(), scale);
  return width;
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
      const MathRenderNode* fractionBody = directChild(semantic, MathRenderKind::VList);
      const qreal contentWidth = (fractionBody ? fractionBody->width
                                               : widestDescendant(semantic, MathRenderKind::VList)) * scale;
      if (hasNestedSemantic(semantic, MathSemanticKind::Fraction)) {
        root.advance = contentWidth * kNestedFractionInlineScale;
        root.width = fractionCssWidth(semantic, scale);
        root.height = semantic->height + semantic->depth + 0.25;
      } else {
        root.advance = std::round(contentWidth);
        root.width = fractionCssWidth(semantic, scale);
        root.height = std::ceil(layout.naturalSize.height() * (32.8125 / 38.864));
        if (hasNestedSemantic(semantic, MathSemanticKind::SupSub))
          root.height += cssRootFontPixelSize * 0.15;
      }
      root.advance = hasNestedSemantic(semantic, MathSemanticKind::Fraction)
          ? root.width - 1.0
          : std::round((fractionBody ? fractionBody->width
                                     : widestDescendant(semantic, MathRenderKind::VList)));
      break;
    }
    case MathSemanticKind::Radical:
      {
      const MathRenderNode* radicalBox = semantic->radicalIndex
          ? nestedSemanticNode(semantic, MathSemanticKind::Radical) : semantic;
      root.width = kRadicalOperatorWidthPx + cssNodeWidth(radicalBody(radicalBox), scale);
      if (const auto* fraction = nestedSemanticNode(semantic, MathSemanticKind::Fraction)) {
        root.height = std::max<qreal>(37.0,
            std::ceil((fraction->height + fraction->depth) * (32.8125 / 38.864)) + 7.0);
      }
      if (semantic->radicalIndex && !semantic->children.empty())
        root.width += cssNodeWidth(semantic->children.front().get(), scale) * 0.15;
      root.advance = root.width - kRadicalRightOverhangPx;
      if (root.height == 0.0)
        root.height = semantic->radicalIndex
            ? (semantic->height + semantic->depth) * scale + 0.625
            : std::ceil(scaledHeight) + 7.0;
      break;
      }
    case MathSemanticKind::SupSub:
      root.width = scaledWidth;
      if (semantic && semantic->children.size() == 2) {
        const qreal base = semantic->children.front()->width * scale;
        const qreal scriptScale = semantic->scriptKind == MathScriptKind::Subscript
            ? kNativeSubscriptInlineScale : kNativeScriptInlineScale;
        const qreal scripts = semantic->children.back()->width * scriptScale;
        root.width = snapEighth(base + scripts);
      }
      if (root.scriptKind != MathScriptKind::Subscript)
        root.width = std::max<qreal>(16.125, root.width);
      root.advance = root.width;
      root.height = (semantic->height + semantic->depth) * scale;
      if (semantic->scriptKind == MathScriptKind::Subscript) root.height += 1.0;
      if (semantic->scriptKind == MathScriptKind::SubSup) root.height += 0.5625;
      break;
    case MathSemanticKind::Array:
      root.width = 0.0;
      for (qreal columnWidth : semantic->arrayColumnWidths)
        root.width += std::ceil(columnWidth * scale) + cssRootFontPixelSize * 0.8;
      root.width -= std::max(0, semantic->columns - 2) * 1.0;
      root.width = std::floor(root.width * 16.0) / 16.0;
      root.advance = root.width - cssRootFontPixelSize * 0.4;
      root.height = 0.0;
      for (size_t row = 0; row < semantic->arrayRowHeights.size(); ++row) {
        const bool inkDescender = row < semantic->arrayRowInkDescenders.size() &&
                                  semantic->arrayRowInkDescenders[row];
        root.height += inkDescender ? 21.0 : 18.0;
      }
      break;
    case MathSemanticKind::None:
      if (symbolCount(layout.root.get()) == 1) {
        const auto glyph = nativeGlyphBox(singleSymbol(layout.root.get()));
        if (glyph) {
          root.width = glyph->width;
          root.height = glyph->height;
          if (singleSymbol(layout.root.get())->text == QLatin1String("+"))
            root.width = 19.094;
        } else {
          root.width = std::round(scaledWidth);
          root.height = std::ceil((layout.root->children.empty()
              ? layout.root->height + layout.root->depth
              : layout.root->children.front()->height + layout.root->children.front()->depth) * scale);
        }
      } else {
        root.width = snapEighth(scaledWidth);
        root.height = (layout.root->children.empty()
            ? layout.root->height + layout.root->depth
            : layout.root->children.front()->height + layout.root->children.front()->depth) * scale;
        root.height = std::ceil(root.height);
      }
      root.advance = root.width;
      break;
  }
  root.baseline = root.height / 2.0 + kChromiumMathAxisOffsetPx;
  root.inkTop = 0.0;
  root.inkBottom = root.height;
  switch (root.semanticKind) {
    case MathSemanticKind::Fraction:
      root.inkTop = -5.0;
      root.inkBottom = root.height + 4.0;
      break;
    case MathSemanticKind::Radical:
      if (hasNestedSemantic(semantic, MathSemanticKind::SupSub)) {
        root.inkTop = 3.75;
        root.inkBottom = root.baseline + 1.6;
      }
      break;
    case MathSemanticKind::Array:
      root.inkTop = 2.5;
      root.inkBottom = root.height + 0.5;
      break;
    case MathSemanticKind::SupSub:
    case MathSemanticKind::None:
      break;
  }
  return root;
}

}  // namespace muffin::math
