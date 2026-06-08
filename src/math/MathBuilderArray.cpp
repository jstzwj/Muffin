#include "math/MathBuilder.h"

#include "math/MathDelimiter.h"
#include "math/MathFontMetrics.h"
#include "math/MathLayoutTree.h"

#include <QHash>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace muffin::math {
namespace {

qreal axisHeight(const MathOptions& options) {
  return options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).axisHeight;
}

qreal ruleThickness(const MathOptions& options) {
  const qreal ruleEm = qMax(MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness, options.settings().minRuleThickness);
  return options.fontPointSize() * ruleEm;
}

std::unique_ptr<MathRenderNode> makeArrayCellWrapper(std::unique_ptr<MathRenderNode> content,
                                                     qreal rowHeight,
                                                     qreal rowDepth,
                                                     qreal alignedX) {
  auto wrapper = std::make_unique<MathRenderNode>();
  wrapper->kind = MathRenderKind::Span;
  wrapper->width = content ? content->width : 0.0;
  wrapper->height = rowHeight;
  wrapper->depth = rowDepth;
  wrapper->xOffset = alignedX;
  if (content) {
    content->xOffset = 0.0;
    content->yOffset = 0.0;
    wrapper->children.push_back(std::move(content));
  }
  return wrapper;
}

}  // namespace

std::unique_ptr<MathRenderNode> MathBuilder::makeArray(const MathParseNode& node) {
  const int rowCount = node.rows.size();
  int colCount = 0;
  for (const auto& row : node.rows) {
    colCount = qMax(colCount, row.size());
  }
  if (rowCount <= 0 || colCount <= 0) {
    return makeError(QStringLiteral("empty array"), options_);
  }

  std::vector<std::vector<std::unique_ptr<MathRenderNode>>> cells;
  QVector<qreal> colWidths(colCount, 0.0);
  QVector<qreal> rowHeights(rowCount, 0.0);
  QVector<qreal> rowDepths(rowCount, 0.0);
  MathStyle cellStyle = options_.style().text();
  if (node.arrayCellStyle == QStringLiteral("script")) {
    cellStyle = MathStyle::script();
  } else if (node.arrayCellStyle == QStringLiteral("display")) {
    cellStyle = MathStyle::display();
  } else if (node.arrayCellStyle == QStringLiteral("text")) {
    cellStyle = MathStyle::textStyle();
  }

  for (int r = 0; r < rowCount; ++r) {
    std::vector<std::unique_ptr<MathRenderNode>> rowCells;
    for (int c = 0; c < colCount; ++c) {
      std::unique_ptr<MathRenderNode> cell;
      if (c < node.rows.at(r).size()) {
        QVector<MathParseNode> cellBody;
        for (const auto& item : node.rows.at(r).at(c).body) {
          if (item) {
            cellBody.push_back(*item);
          }
        }
        cell = MathBuilder(options_.havingStyle(cellStyle)).buildExpression(cellBody);
      } else {
        cell = std::make_unique<MathRenderNode>();
      }
      colWidths[c] = qMax(colWidths[c], cell->width);
      rowHeights[r] = qMax(rowHeights[r], cell->height);
      rowDepths[r] = qMax(rowDepths[r], cell->depth);
      rowCells.push_back(std::move(cell));
    }
    cells.push_back(std::move(rowCells));
  }

  const GlobalFontMetrics fontMetrics = MathFontMetrics::globalMetrics(options_.style().size());
  const qreal pt = fontMetrics.ptPerEm > 0.0 ? 1.0 / fontMetrics.ptPerEm : 0.1;
  const qreal arrayColSepEm = node.colSeparationType == QStringLiteral("small")
                                  ? 0.2778 * (options_.havingStyle(MathStyle::script()).sizeMultiplier() / options_.sizeMultiplier())
                                  : 5.0 * pt;
  const qreal baselineSkipEm = 12.0 * pt;
  const qreal jotEm = 3.0 * pt;
  const qreal arrayskipEm = qMax<qreal>(0.0, node.arrayStretch) * baselineSkipEm;
  const qreal arstrutHeight = options_.fontPointSize() * 0.7 * arrayskipEm;
  const qreal arstrutDepth = options_.fontPointSize() * 0.3 * arrayskipEm;
  const qreal rule = ruleThickness(options_);
  QVector<qreal> rowExtraGaps(rowCount, 0.0);

  for (int r = 0; r < rowCount; ++r) {
    rowHeights[r] = qMax(rowHeights[r], arstrutHeight);
    rowDepths[r] = qMax(rowDepths[r], arstrutDepth);
    if (r < node.rowGaps.size() && !qFuzzyIsNull(node.rowGaps.at(r))) {
      const qreal rowGap = options_.fontPointSize() * node.rowGaps.at(r);
      if (rowGap > 0.0) {
        rowDepths[r] = qMax(rowDepths[r], arstrutDepth + rowGap);
      } else {
        rowExtraGaps[r] = rowGap;
      }
    }
    if (node.addJot && r + 1 < rowCount) {
      rowDepths[r] += options_.fontPointSize() * jotEm;
    }
  }

  struct PlacedColumn {
    int column = -1;
    qreal x = 0.0;
    qreal width = 0.0;
    QChar align = QLatin1Char('c');
  };
  struct PlacedSeparator {
    qreal x = 0.0;
    bool dashed = false;
  };

  QVector<PlacedColumn> placedColumns;
  QVector<PlacedSeparator> verticalSeparators;
  qreal bodyWidth = 0.0;
  int alignIndex = 0;
  const bool useExplicitSpec = !node.columns.isEmpty();
  const qreal verticalSeparatorWidth = qMax<qreal>(1.0, rule);
  const auto addGap = [&](qreal em) {
    if (em > 0.0) {
      bodyWidth += em * options_.fontPointSize();
    }
  };

  if (useExplicitSpec) {
    bool sawAlign = false;
    bool previousWasSeparator = false;
    for (const MathArrayColumn& spec : node.columns) {
      if (spec.type == MathArrayColumn::Type::Separator) {
        if (previousWasSeparator) {
          addGap(fontMetrics.doubleRuleSep);
        }
        verticalSeparators.push_back({bodyWidth, spec.separator == QLatin1Char(':')});
        bodyWidth += verticalSeparatorWidth;
        previousWasSeparator = true;
        continue;
      }
      if (alignIndex >= colCount) {
        continue;
      }
      previousWasSeparator = false;
      const qreal pregap = spec.pregap >= 0.0 ? spec.pregap : (sawAlign || node.hskipBeforeAndAfter ? arrayColSepEm : 0.0);
      addGap(pregap);
      placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], spec.align});
      bodyWidth += colWidths[alignIndex];
      const qreal postgap = spec.postgap >= 0.0 ? spec.postgap : ((alignIndex + 1 < colCount || node.hskipBeforeAndAfter) ? arrayColSepEm : 0.0);
      addGap(postgap);
      ++alignIndex;
      sawAlign = true;
    }
  }
  while (alignIndex < colCount) {
    const qreal pregap = alignIndex > 0 ? arrayColSepEm : 0.0;
    addGap(pregap);
    const QChar align = alignIndex < node.columnAlignments.size() ? node.columnAlignments.at(alignIndex) : QLatin1Char('c');
    placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], align});
    bodyWidth += colWidths[alignIndex];
    if (alignIndex + 1 < colCount) {
      addGap(arrayColSepEm);
    }
    ++alignIndex;
  }

  auto array = std::make_unique<MathRenderNode>();
  array->kind = MathRenderKind::Array;
  array->columns = colCount;
  array->rows = rowCount;
  qreal totalHeight = 0.0;
  QVector<qreal> rowPositions(rowCount, 0.0);
  QVector<qreal> hlinePositions(node.arrayLines.size(), 0.0);
  const auto setHLinePositions = [&](int beforeRow) {
    int hlinesInGap = 0;
    for (int i = 0; i < node.arrayLines.size(); ++i) {
      if (node.arrayLines.at(i).beforeRow != beforeRow) {
        continue;
      }
      if (hlinesInGap > 0) {
        totalHeight += options_.fontPointSize() * 0.25;
      }
      hlinePositions[i] = totalHeight;
      ++hlinesInGap;
    }
  };
  setHLinePositions(0);
  for (int r = 0; r < rowCount; ++r) {
    totalHeight += rowHeights[r];
    rowPositions[r] = totalHeight;
    totalHeight += rowDepths[r] + rowExtraGaps[r];
    setHLinePositions(r + 1);
  }
  qreal xOffset = 0.0;
  qreal leftDelimiterWidth = 0.0;
  qreal rightDelimiterWidth = 0.0;
  qreal delimiterHeight = 0.0;
  qreal delimiterDepth = 0.0;
  const qreal targetDelimHeight = totalHeight;
  const qreal nullDelimiterWidth = options_.fontPointSize() * 0.12;
  const bool hasLeftDelimiter = !node.leftDelim.isEmpty() && node.leftDelim != QStringLiteral(".");
  const bool hasRightDelimiter = !node.rightDelim.isEmpty() && node.rightDelim != QStringLiteral(".");
  if (node.leftDelim == QStringLiteral(".")) {
    xOffset = nullDelimiterWidth;
  } else if (hasLeftDelimiter) {
    auto left = makeDelimiter(node.leftDelim, targetDelimHeight, MathNodeType::Open);
    left->xOffset = 0.0;
    left->yOffset = 0.0;
    leftDelimiterWidth = left->width;
    xOffset = leftDelimiterWidth;
    delimiterHeight = qMax(delimiterHeight, left->height);
    delimiterDepth = qMax(delimiterDepth, left->depth);
    array->children.push_back(std::move(left));
  }

  const qreal axis = axisHeight(options_);
  qreal baseline = totalHeight / 2.0 + axis;
  auto tableBody = std::make_unique<MathRenderNode>();
  tableBody->kind = MathRenderKind::VList;
  tableBody->width = bodyWidth;
  tableBody->height = baseline;
  tableBody->depth = totalHeight - baseline;

  for (const PlacedColumn& placed : placedColumns) {
    std::vector<MathVListChild> columnChildren;
    const int c = placed.column;
    for (int r = 0; r < rowCount; ++r) {
      auto cell = std::move(cells[r][c]);
      qreal alignedX = 0.0;
      if (placed.align == QLatin1Char('r')) {
        alignedX = colWidths[c] - cell->width;
      } else if (placed.align == QLatin1Char('c')) {
        alignedX = (colWidths[c] - cell->width) / 2.0;
      }
      columnChildren.push_back(MathVListChild{layoutFromRenderNode(makeArrayCellWrapper(std::move(cell), rowHeights[r], rowDepths[r], alignedX)),
                                              rowPositions[r] - baseline});
    }
    auto colLayout = makeLayoutVListIndividualShift(std::move(columnChildren));
    colLayout->width = placed.width;
    auto colVList = renderNodeFromLayout(*colLayout);
    auto colSpan = std::make_unique<MathRenderNode>();
    colSpan->kind = MathRenderKind::Span;
    colSpan->width = placed.width;
    colSpan->height = colVList->height;
    colSpan->depth = colVList->depth;
    colSpan->xOffset = placed.x;
    colSpan->children.push_back(std::move(colVList));
    tableBody->children.push_back(std::move(colSpan));
  }

  tableBody->xOffset = xOffset;

  // Wrap tableBody and hlines into a VList, matching KaTeX's approach where
  // hlines are VList children with individualShift (see array.ts:525-539).
  if (!node.arrayLines.isEmpty()) {
    std::vector<MathVListChild> wrapChildren;
    auto tableLayout = layoutFromRenderNode(std::move(tableBody));
    wrapChildren.push_back(MathVListChild{std::move(tableLayout), 0.0});
    for (int i = 0; i < node.arrayLines.size(); ++i) {
      const MathArrayLine& arrayLine = node.arrayLines.at(i);
      auto line = std::make_unique<MathRenderNode>();
      line->kind = MathRenderKind::Rule;
      line->width = bodyWidth;
      line->ruleThickness = rule;
      line->color = options_.color();
      line->xOffset = xOffset;
      line->text = arrayLine.dashed ? QStringLiteral("dashed") : QString();
      wrapChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(line)),
                                            hlinePositions.value(i) - baseline});
    }
    auto wrapLayout = makeLayoutVListIndividualShift(std::move(wrapChildren));
    auto wrapped = renderNodeFromLayout(*wrapLayout);
    wrapped->width = bodyWidth;
    array->children.push_back(std::move(wrapped));
  } else {
    array->children.push_back(std::move(tableBody));
  }

  for (const PlacedSeparator& separator : verticalSeparators) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->height = baseline;
    line->depth = totalHeight - baseline;
    line->width = verticalSeparatorWidth;
    line->ruleThickness = rule;
    line->color = options_.color();
    line->xOffset = xOffset + separator.x;
    line->yOffset = 0.0;
    line->shift = -1.0;
    line->text = separator.dashed ? QStringLiteral("dashed") : QString();
    array->children.push_back(std::move(line));
  }

  if (hasRightDelimiter) {
    auto right = makeDelimiter(node.rightDelim, targetDelimHeight, MathNodeType::Close);
    right->xOffset = xOffset + bodyWidth;
    right->yOffset = 0.0;
    rightDelimiterWidth = right->width;
    delimiterHeight = qMax(delimiterHeight, right->height);
    delimiterDepth = qMax(delimiterDepth, right->depth);
    array->children.push_back(std::move(right));
  }

  array->width = bodyWidth + xOffset + (node.rightDelim == QStringLiteral(".") ? nullDelimiterWidth : (hasRightDelimiter ? rightDelimiterWidth : 0.0));
  array->height = qMax(baseline, delimiterHeight);
  array->depth = qMax(totalHeight - baseline, delimiterDepth);
  return array;
}

}  // namespace muffin::math
