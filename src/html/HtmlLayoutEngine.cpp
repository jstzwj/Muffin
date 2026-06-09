#include "html/HtmlLayoutEngine.h"
#include "html/HtmlTextMeasurer.h"

#include <yoga/Yoga.h>

#include <QFontMetricsF>
#include <QImageReader>
#include <QMap>
#include <QVector>

#include <cmath>
#include <numeric>

namespace muffin::html {
namespace {

constexpr qreal kDefaultImageWidth = 100.0;
constexpr qreal kDefaultImageHeight = 80.0;

YGSize measureTextCallback(YGNodeConstRef nodeRef, float width, YGMeasureMode widthMode, float, YGMeasureMode) {
  auto* ctx = static_cast<YogaContext*>(YGNodeGetContext(nodeRef));
  if (!ctx) {
    return {0, 0};
  }

  auto* box = ctx->box;
  const qreal textWidth = box->geometry().width;
  const qreal textHeight = box->geometry().height;
  const qreal measuredWidth =
      widthMode == YGMeasureModeExactly || widthMode == YGMeasureModeAtMost ? qMin<qreal>(textWidth, width) : textWidth;

  QFontMetricsF fm(box->style().font);
  qreal minHeight = fm.height();

  return {
      static_cast<float>(std::ceil(measuredWidth)),
      static_cast<float>(std::max(textHeight, minHeight))};
}

void freeContextTree(YGNodeRef node) {
  auto* ctx = static_cast<YogaContext*>(YGNodeGetContext(node));
  delete ctx;
  YGNodeSetContext(node, nullptr);

  uint32_t childCount = YGNodeGetChildCount(node);
  for (uint32_t i = 0; i < childCount; ++i) {
    freeContextTree(YGNodeGetChild(node, i));
  }
}

qreal horizontalBoxExtent(const HtmlComputedStyle& style) {
  return style.padding.left() + style.padding.right() + style.borderWidth.left() + style.borderWidth.right();
}

qreal verticalBoxExtent(const HtmlComputedStyle& style) {
  return style.padding.top() + style.padding.bottom() + style.borderWidth.top() + style.borderWidth.bottom();
}

bool isTableCellBox(const HtmlBox& box) {
  return box.style().visible && box.style().display == HtmlDisplay::TableCell;
}

bool isTableRowBox(const HtmlBox& box) {
  return box.style().visible && box.style().display == HtmlDisplay::TableRow;
}

void collectTableRows(HtmlBox& box, QVector<HtmlBox*>& rows) {
  if (!box.style().visible || box.style().display == HtmlDisplay::None || box.tag() == HtmlTag::Caption) {
    return;
  }
  if (isTableRowBox(box)) {
    rows.push_back(&box);
    return;
  }
  for (auto& child : box.children()) {
    collectTableRows(*child, rows);
  }
}

void collectRowCells(HtmlBox& row, QVector<HtmlBox*>& cells) {
  for (auto& child : row.children()) {
    if (isTableCellBox(*child)) {
      cells.push_back(child.get());
    }
  }
}

HtmlBox* findTableCaption(HtmlBox& table) {
  for (auto& child : table.children()) {
    if (child->style().visible && child->tag() == HtmlTag::Caption) {
      return child.get();
    }
  }
  return nullptr;
}

bool isTableInternalBox(const HtmlBox& box) {
  return box.style().display == HtmlDisplay::TableRowGroup ||
         box.style().display == HtmlDisplay::TableRow ||
         box.style().display == HtmlDisplay::TableCell;
}

}  // namespace

HtmlLayoutEngine::HtmlLayoutEngine() = default;
HtmlLayoutEngine::~HtmlLayoutEngine() = default;

void HtmlLayoutEngine::layout(
    HtmlBox& root,
    qreal availableWidth,
    qreal baseFontSize,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  textLayouts.clear();

  YGNode* rootNode = createYogaNode(root, baseFontSize, availableWidth, textLayouts);

  YGNodeCalculateLayout(rootNode, static_cast<float>(availableWidth), YGUndefined, YGDirectionLTR);

  readLayoutBack(root, rootNode);

  // Free all YogaContext objects before freeing nodes
  // (YGNodeFreeRecursive doesn't call context destructors)
  freeContextTree(rootNode);

  YGNodeFreeRecursive(rootNode);
}

YGNode* HtmlLayoutEngine::createYogaNode(
    HtmlBox& box,
    qreal fontSize,
    qreal availableWidth,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  YGNode* node = YGNodeNew();

  const auto& style = box.style();

  // Skip invisible boxes
  if (!style.visible || style.display == HtmlDisplay::None) {
    YGNodeStyleSetDisplay(node, YGDisplayNone);
    box.style().visible = false;
    return node;
  }

  // Apply box model styles
  applyBoxStyle(node, style);
  if ((style.display == HtmlDisplay::TableRowGroup ||
       style.display == HtmlDisplay::TableRow) &&
      style.width < 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(qMax<qreal>(1.0, availableWidth)));
  }

  if (style.display == HtmlDisplay::Table) {
    layoutTableBox(box, availableWidth, textLayouts);
    YGNodeStyleSetWidth(node, static_cast<float>(box.geometry().width));
    YGNodeStyleSetHeight(node, static_cast<float>(box.geometry().height));
    return node;
  }

  // Determine if this box contains only inline children (text content)
  bool hasBlockChildren = false;
  bool hasInlineContent = false;
  bool hasReplacedInlineContent = false;
  for (const auto& child : box.children()) {
    if (child->style().display == HtmlDisplay::None || !child->style().visible) {
      continue;
    }
    // Collapsed details: only summary is visible
    if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
      continue;
    }
    if (child->style().display == HtmlDisplay::Block ||
        child->style().display == HtmlDisplay::Flex ||
        child->style().display == HtmlDisplay::Table ||
        child->style().display == HtmlDisplay::ListItem) {
      hasBlockChildren = true;
    }
    if (child->isInlineLevel() || child->isTextRun()) {
      hasInlineContent = true;
    }
    if (child->tag() == HtmlTag::Image) {
      hasReplacedInlineContent = true;
    }
    // inline-block acts as replaced inline content in inline context
    if (child->style().display == HtmlDisplay::InlineBlock) {
      hasReplacedInlineContent = true;
    }
  }

  if (box.tag() == HtmlTag::Image) {
    QSize naturalSize;
    if (!box.src().isEmpty()) {
      QImageReader reader(box.src());
      naturalSize = reader.size();
    }
    qreal imageWidth = style.width >= 0 ? style.width : (naturalSize.width() > 0 ? naturalSize.width() : kDefaultImageWidth);
    qreal imageHeight = style.height >= 0 ? style.height : (naturalSize.height() > 0 ? naturalSize.height() : kDefaultImageHeight);
    if (style.width >= 0 && style.height < 0 && naturalSize.width() > 0 && naturalSize.height() > 0) {
      imageHeight = imageWidth * naturalSize.height() / naturalSize.width();
    } else if (style.height >= 0 && style.width < 0 && naturalSize.width() > 0 && naturalSize.height() > 0) {
      imageWidth = imageHeight * naturalSize.width() / naturalSize.height();
    }
    const qreal maxWidth = qMax<qreal>(1.0, availableWidth - style.margin.left() - style.margin.right());
    if (imageWidth > maxWidth) {
      const qreal scale = maxWidth / imageWidth;
      imageWidth *= scale;
      imageHeight *= scale;
    }
    YGNodeStyleSetWidth(node, static_cast<float>(imageWidth));
    YGNodeStyleSetHeight(node, static_cast<float>(imageHeight));
    box.geometry().width = imageWidth;
    box.geometry().height = imageHeight;
  } else if (box.tag() == HtmlTag::Pre && hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildPreLayout(
        box, style.fontSize, qMax<qreal>(1.0, availableWidth - horizontalBoxExtent(style)));
    qreal textHeight = textLayout->height;
    qreal textWidth = textLayout->width;

    auto* ctx = new YogaContext{&box, style.fontSize};
    YGNodeSetContext(node, ctx);

    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
    box.geometry().width = textWidth;
    box.geometry().height = textHeight;

    YGNodeSetMeasureFunc(node, measureTextCallback);
  } else if (hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    // This is an inline formatting context — measure text as one unit
    auto textLayout = measurer_.buildInlineLayout(
        box, style.fontSize, qMax<qreal>(1.0, availableWidth - style.padding.left() - style.padding.right()),
        style.textAlign);
    qreal textHeight = textLayout->height;
    qreal textWidth = textLayout->width;

    // Set measurement callback data
    auto* ctx = new YogaContext{&box, style.fontSize};
    YGNodeSetContext(node, ctx);

    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
    box.geometry().width = textWidth;
    box.geometry().height = textHeight;

    // Use Yoga measurement callback for text
    YGNodeSetMeasureFunc(node, measureTextCallback);
  } else {
    // Block-level container — recurse into children
    if (style.display != HtmlDisplay::Flex && style.display != HtmlDisplay::TableRow) {
      YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
    }
    int childIndex = 0;
    int tableCellCount = 0;
    if (style.display == HtmlDisplay::TableRow) {
      for (const auto& child : box.children()) {
        if (child->style().visible && child->style().display == HtmlDisplay::TableCell) {
          ++tableCellCount;
        }
      }
    }
    for (auto& child : box.children()) {
      if (child->style().display == HtmlDisplay::None || !child->style().visible) {
        continue;
      }
      // Collapsed <details>: hide non-summary children
      if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
        continue;
      }
      qreal childAvailableWidth = availableWidth;
      if (style.display == HtmlDisplay::TableRow && child->style().display == HtmlDisplay::TableCell && tableCellCount > 0) {
        childAvailableWidth = qMax<qreal>(1.0, availableWidth / tableCellCount);
      }
      YGNode* childNode = createYogaNode(*child, style.fontSize, childAvailableWidth, textLayouts);
      YGNodeInsertChild(node, childNode, childIndex);
      ++childIndex;
    }
  }

  return node;
}

void HtmlLayoutEngine::applyBoxStyle(YGNode* node, const HtmlComputedStyle& style) {
  // Margin (use percent API when a percentage was specified, pixel API otherwise)
  if (style.marginPercent.top() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeTop, static_cast<float>(style.marginPercent.top()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeTop, static_cast<float>(style.margin.top()));
  }
  if (style.marginPercent.bottom() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeBottom, static_cast<float>(style.marginPercent.bottom()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeBottom, static_cast<float>(style.margin.bottom()));
  }
  if (style.marginPercent.left() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeLeft, static_cast<float>(style.marginPercent.left()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeLeft, static_cast<float>(style.margin.left()));
  }
  if (style.marginPercent.right() >= 0) {
    YGNodeStyleSetMarginPercent(node, YGEdgeRight, static_cast<float>(style.marginPercent.right()));
  } else {
    YGNodeStyleSetMargin(node, YGEdgeRight, static_cast<float>(style.margin.right()));
  }

  // Padding (use percent API when a percentage was specified, pixel API otherwise)
  if (style.paddingPercent.top() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeTop, static_cast<float>(style.paddingPercent.top()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeTop, static_cast<float>(style.padding.top()));
  }
  if (style.paddingPercent.bottom() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeBottom, static_cast<float>(style.paddingPercent.bottom()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeBottom, static_cast<float>(style.padding.bottom()));
  }
  if (style.paddingPercent.left() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeLeft, static_cast<float>(style.paddingPercent.left()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeLeft, static_cast<float>(style.padding.left()));
  }
  if (style.paddingPercent.right() >= 0) {
    YGNodeStyleSetPaddingPercent(node, YGEdgeRight, static_cast<float>(style.paddingPercent.right()));
  } else {
    YGNodeStyleSetPadding(node, YGEdgeRight, static_cast<float>(style.padding.right()));
  }

  // Width/Height
  if (style.widthPercent >= 0) {
    YGNodeStyleSetWidthPercent(node, static_cast<float>(style.widthPercent));
  } else if (style.width >= 0) {
    YGNodeStyleSetWidth(node, static_cast<float>(style.width));
  }
  if (style.height >= 0) {
    YGNodeStyleSetHeight(node, static_cast<float>(style.height));
  }

  // Flex direction for flex containers
  if (style.display == HtmlDisplay::Flex) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
    YGNodeStyleSetFlexWrap(node, YGWrapWrap);
  } else if (style.display == HtmlDisplay::Table ||
             style.display == HtmlDisplay::TableRowGroup) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
  } else if (style.display == HtmlDisplay::TableRow) {
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
  } else if (style.display == HtmlDisplay::TableCell) {
    YGNodeStyleSetFlexGrow(node, 1);
    YGNodeStyleSetFlexShrink(node, 1);
  }

  // Default flex behavior: grow to fill available space in column layout
  if (style.width < 0 && style.display != HtmlDisplay::TableCell) {
    YGNodeStyleSetFlexGrow(node, 0);
    YGNodeStyleSetFlexShrink(node, 0);
  }
}

void HtmlLayoutEngine::layoutTableBox(
    HtmlBox& table,
    qreal availableWidth,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  QVector<HtmlBox*> rows;
  collectTableRows(table, rows);

  // Build a grid accounting for colspan and rowspan
  struct CellEntry {
    HtmlBox* box = nullptr;
    int colSpan = 1;
    int rowSpan = 1;
  };

  int gridCols = 0;
  QVector<QVector<CellEntry>> grid;
  grid.reserve(rows.size());
  for (HtmlBox* row : rows) {
    QVector<HtmlBox*> cells;
    collectRowCells(*row, cells);
    // Determine effective column count accounting for spans
    int rowCols = 0;
    QVector<CellEntry> entries;
    entries.reserve(cells.size());
    for (HtmlBox* cell : cells) {
      CellEntry entry;
      entry.box = cell;
      entry.colSpan = qMax(1, cell->colSpan());
      entry.rowSpan = qMax(1, cell->rowSpan());
      rowCols += entry.colSpan;
      entries.push_back(entry);
    }
    gridCols = qMax(gridCols, rowCols);
    grid.push_back(std::move(entries));
  }

  const qreal availableInnerWidth = qMax<qreal>(
      1.0,
      availableWidth - table.style().margin.left() - table.style().margin.right() -
          horizontalBoxExtent(table.style()));

  // Compute per-column widths from non-spanning cells
  QVector<qreal> columnWidths(qMax(0, gridCols), 0.0);
  for (int rowIdx = 0; rowIdx < grid.size(); ++rowIdx) {
    int col = 0;
    // Skip columns occupied by previous rowspans
    // (simplified: we only account for colspan for width calculation)
    for (const auto& entry : grid[rowIdx]) {
      if (entry.colSpan == 1) {
        if (col < columnWidths.size()) {
          columnWidths[col] = qMax(columnWidths[col], intrinsicOuterWidth(*entry.box, availableInnerWidth));
        }
      }
      col += entry.colSpan;
    }
  }

  qreal naturalTableContentWidth = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0);
  qreal tableContentWidth = table.style().width >= 0 ? table.style().width : naturalTableContentWidth;
  tableContentWidth = qBound<qreal>(1.0, tableContentWidth, availableInnerWidth);

  if (gridCols > 0 && naturalTableContentWidth > 0) {
    if (!qFuzzyCompare(tableContentWidth, naturalTableContentWidth)) {
      const qreal scale = tableContentWidth / naturalTableContentWidth;
      for (qreal& width : columnWidths) {
        width = qMax<qreal>(1.0, width * scale);
      }
    }
  } else {
    tableContentWidth = 0;
  }

  const qreal actualColumnSum = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0);
  if (actualColumnSum > 0) {
    tableContentWidth = actualColumnSum;
  }

  qreal y = 0;
  if (HtmlBox* caption = findTableCaption(table)) {
    const qreal captionHeight = layoutFixedWidthBox(*caption, qMax<qreal>(1.0, tableContentWidth), textLayouts);
    caption->geometry() = HtmlLayoutGeometry{0, y, qMax<qreal>(1.0, tableContentWidth), captionHeight};
    y += captionHeight;
  }

  // Rowspan tracking: for each row, which columns are occupied by a cell from above
  // and the remaining height to extend. Maps col → {remainingRows, height}
  struct RowSpanSlot {
    int remainingRows = 0;
    qreal height = 0;
  };
  QVector<QMap<int, RowSpanSlot>> rowSpanGrid(rows.size());

  QVector<qreal> rowTops(rows.size(), 0.0);
  QVector<qreal> rowHeights(rows.size(), 0.0);
  for (int rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
    rowTops[rowIdx] = y;
    qreal rowHeight = 0;

    // First pass: compute cell heights, place cells accounting for spans
    int col = 0;
    for (auto& entry : grid[rowIdx]) {
      // Skip columns occupied by rowspans from above
      while (col < gridCols && rowSpanGrid[rowIdx].contains(col)) {
        auto& slot = rowSpanGrid[rowIdx][col];
        rowHeight = qMax(rowHeight, slot.height);
        col += 1;  // rowspan occupies 1 column-width per slot (simplified)
      }

      if (col >= gridCols) break;

      // Compute width for this cell (sum of spanned columns)
      qreal cellWidth = 0;
      for (int c = col; c < col + entry.colSpan && c < gridCols; ++c) {
        cellWidth += (c < columnWidths.size()) ? columnWidths[c] : 1.0;
      }
      cellWidth = qMax<qreal>(1.0, cellWidth);

      const qreal cellHeight = layoutFixedWidthBox(*entry.box, cellWidth, textLayouts);
      entry.box->geometry() = HtmlLayoutGeometry{
          col < columnWidths.size() ? std::accumulate(columnWidths.begin(), columnWidths.begin() + col, 0.0) : 0.0,
          0, cellWidth, cellHeight};
      rowHeight = qMax(rowHeight, cellHeight);

      // Propagate rowspan to subsequent rows
      if (entry.rowSpan > 1) {
        for (int r = 1; r < entry.rowSpan && rowIdx + r < rows.size(); ++r) {
          for (int c = col; c < col + entry.colSpan && c < gridCols; ++c) {
            rowSpanGrid[rowIdx + r][c] = {entry.rowSpan - r, cellHeight};
          }
        }
      }

      col += entry.colSpan;
    }

    // Account for any remaining rowspan slots in this row
    for (auto it = rowSpanGrid[rowIdx].constBegin(); it != rowSpanGrid[rowIdx].constEnd(); ++it) {
      rowHeight = qMax(rowHeight, it.value().height);
    }

    // Equalize heights for all cells in this row
    for (auto& entry : grid[rowIdx]) {
      entry.box->geometry().height = rowHeight;
    }

    rows[rowIdx]->geometry() = HtmlLayoutGeometry{0, y, tableContentWidth, rowHeight};
    rowHeights[rowIdx] = rowHeight;
    y += rowHeight;
  }

  for (auto& child : table.children()) {
    if (!child->style().visible || child->style().display == HtmlDisplay::None || child->tag() == HtmlTag::Caption) {
      continue;
    }
    if (child->style().display != HtmlDisplay::TableRowGroup) {
      continue;
    }
    qreal groupTop = 0;
    qreal groupBottom = 0;
    bool sawRow = false;
    for (auto& rowChild : child->children()) {
      if (!isTableRowBox(*rowChild)) {
        continue;
      }
      for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        if (rows[rowIndex] == rowChild.get()) {
          if (!sawRow) {
            groupTop = rowTops[rowIndex];
            groupBottom = rowTops[rowIndex] + rowHeights[rowIndex];
            sawRow = true;
          } else {
            groupTop = qMin(groupTop, rowTops[rowIndex]);
            groupBottom = qMax(groupBottom, rowTops[rowIndex] + rowHeights[rowIndex]);
          }
          rowChild->geometry().top -= groupTop;
          break;
        }
      }
    }
    if (sawRow) {
      child->geometry() = HtmlLayoutGeometry{0, groupTop, tableContentWidth, groupBottom - groupTop};
    }
  }

  table.geometry().width = qMax<qreal>(tableContentWidth, 1.0) + horizontalBoxExtent(table.style());
  table.geometry().height = y + verticalBoxExtent(table.style());
}

qreal HtmlLayoutEngine::layoutFixedWidthBox(
    HtmlBox& box,
    qreal width,
    std::vector<std::unique_ptr<HtmlTextLayout>>& textLayouts) {
  const auto& style = box.style();
  const qreal contentWidth = qMax<qreal>(1.0, width - horizontalBoxExtent(style));

  bool hasBlockChildren = false;
  bool hasInlineContent = false;
  bool hasReplacedInlineContent = false;
  for (const auto& child : box.children()) {
    if (!child->style().visible || child->style().display == HtmlDisplay::None) {
      continue;
    }
    if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
      continue;
    }
    if (child->style().display == HtmlDisplay::Block ||
        child->style().display == HtmlDisplay::Flex ||
        child->style().display == HtmlDisplay::Table ||
        child->style().display == HtmlDisplay::ListItem) {
      hasBlockChildren = true;
    }
    if (child->isInlineLevel() || child->isTextRun()) {
      hasInlineContent = true;
    }
    if (child->tag() == HtmlTag::Image) {
      hasReplacedInlineContent = true;
    }
  }

  qreal contentHeight = 0;
  if (box.tag() == HtmlTag::Pre && hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildPreLayout(box, style.fontSize, contentWidth);
    contentHeight = textLayout->height;
    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
  } else if (hasInlineContent && !hasBlockChildren && !hasReplacedInlineContent) {
    auto textLayout = measurer_.buildInlineLayout(box, style.fontSize, contentWidth, style.textAlign);
    contentHeight = textLayout->height;
    const int textLayoutIndex = static_cast<int>(textLayouts.size());
    textLayouts.push_back(std::move(textLayout));
    box.setTextLayoutIndex(textLayoutIndex);
  } else {
    qreal y = 0;
    for (auto& child : box.children()) {
      if (!child->style().visible || child->style().display == HtmlDisplay::None) {
        continue;
      }
      if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) {
        continue;
      }
      if (isTableInternalBox(*child)) {
        continue;
      }
      const qreal childHeight = layoutFixedWidthBox(*child, contentWidth, textLayouts);
      child->geometry() = HtmlLayoutGeometry{0, y, contentWidth, childHeight};
      y += childHeight;
    }
    contentHeight = y;
  }

  const qreal totalHeight = qMax<qreal>(contentHeight + verticalBoxExtent(style), QFontMetricsF(style.font).height());
  box.geometry().width = width;
  box.geometry().height = totalHeight;
  return totalHeight;
}

qreal HtmlLayoutEngine::intrinsicOuterWidth(const HtmlBox& box, qreal availableWidth) const {
  const auto& style = box.style();
  if (box.tag() == HtmlTag::Pre) {
    auto textLayout = measurer_.buildPreLayout(box, style.fontSize, qMax<qreal>(1.0, availableWidth));
    return qMax<qreal>(1.0, textLayout->width + horizontalBoxExtent(style));
  }
  const qreal measuredWidth = measurer_.measureInlineContext(box, style.fontSize, qMax<qreal>(1.0, availableWidth)).width();
  return qMax<qreal>(1.0, measuredWidth + horizontalBoxExtent(style));
}

void HtmlLayoutEngine::readLayoutBack(HtmlBox& box, YGNode* node) {
  if (!box.style().visible) {
    return;
  }

  auto& geo = box.geometry();
  geo.left = YGNodeLayoutGetLeft(node);
  geo.top = YGNodeLayoutGetTop(node);
  geo.width = YGNodeLayoutGetWidth(node);
  geo.height = YGNodeLayoutGetHeight(node);

  // Read children layouts
  uint32_t childCount = YGNodeGetChildCount(node);
  uint32_t yogaIndex = 0;
  for (auto& child : box.children()) {
    if (!child->style().visible || child->style().display == HtmlDisplay::None) {
      continue;
    }
    if (yogaIndex < childCount) {
      YGNode* childNode = YGNodeGetChild(node, yogaIndex);
      readLayoutBack(*child, childNode);
      ++yogaIndex;
    }
  }

  // For inline context boxes, the text layout already has the correct height.
  // Override Yoga's height if text measurement was done.
  // (The measurement callback should have returned the correct size already.)
}

}  // namespace muffin::html
