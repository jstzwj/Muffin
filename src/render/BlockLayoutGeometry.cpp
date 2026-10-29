#include "render/BlockLayout.h"

#include "blocks/code/CodeFenceScrollController.h"

namespace muffin {

QPointF BlockLayout::TableCellLayout::textOrigin() const {
  const QRectF contentRect = box.contentBox;
  qreal textX = contentRect.left();
  if (alignment == TableAlignment::Right) {
    textX = contentRect.right() - text.size().width();
  } else if (alignment == TableAlignment::Center) {
    textX = contentRect.left() + (contentRect.width() - text.size().width()) / 2.0;
  }
  return QPointF(qMax(contentRect.left(), textX), contentRect.top());
}

HitTestResult BlockLayout::hitForCursorPosition(CursorPosition position, const RenderTheme& theme) const {
  HitTestResult hit;
  hit.blockId = position.blockId;
  hit.textNodeId = position.text.nodeId.isValid() ? position.text.nodeId : position.blockId;
  hit.textOffset = position.text.textOffset;
  hit.sourceOffset = position.text.sourceOffset;
  hit.blockRect = rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (type()) {
  case BlockType::Paragraph:
  case BlockType::Heading:
  case BlockType::ListItem:
    hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
    if (const InlineLayout* inlineLayout = this->inlineLayout()) {
      const QPointF textOrigin = inlineTextOrigin();
      const qsizetype localSourceOffset = position.text.sourceOffset >= 0 && contentSourceStart() >= 0
                                              ? position.text.sourceOffset - contentSourceStart()
                                              : -1;
      hit.cursorRect = localSourceOffset >= 0
                           ? inlineLayout->cursorRectForSourceOffset(localSourceOffset).translated(textOrigin)
                           : inlineLayout->cursorRect(position.text.textOffset).translated(textOrigin);
    }
    break;
  case BlockType::FrontMatter:
  case BlockType::CodeFence:
    hit.zone = type() == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
    hit.cursorRect = literalVisualCursorRect(position.text.textOffset, theme);
    break;
  case BlockType::MathBlock:
    hit.zone = HitTestResult::Zone::Math;
    if (literalEditing()) {
      hit.cursorRect = literalVisualCursorRect(position.text.textOffset, theme);
    } else {
      const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, literal().size());
      const qreal x = offset <= literal().size() / 2 ? rect().left() : rect().right();
      hit.cursorRect = QRectF(x, rect().top(), 1.0, rect().height());
    }
    break;
  case BlockType::HtmlBlock:
    hit.zone = HitTestResult::Zone::Html;
    if (literalEditing()) {
      hit.cursorRect = literalVisualCursorRect(position.text.textOffset, theme);
    } else {
      const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, literal().size());
      const qreal x = offset <= literal().size() / 2 ? rect().left() : rect().right();
      hit.cursorRect = QRectF(x, rect().top(), 1.0,
                              htmlContainer_ ? TextFontMetrics(cssBoxGeometry_.font).height() : rect().height());
    }
    break;
  case BlockType::Table:
    hit.zone = HitTestResult::Zone::TableCell;
    for (int row = 0; row < static_cast<int>(tableRows().size()); ++row) {
      const auto& cells = tableRows()[row].cells;
      for (int column = 0; column < static_cast<int>(cells.size()); ++column) {
        const auto& cell = cells[column];
        if (cell.nodeId != hit.textNodeId)
          continue;
        hit.tableRow = row;
        hit.tableColumn = column;
        const qsizetype localSourceOffset = position.text.sourceOffset >= 0 && cell.contentSourceStart >= 0
                                                ? position.text.sourceOffset - cell.contentSourceStart
                                                : -1;
        hit.cursorRect = localSourceOffset >= 0
                             ? cell.text.cursorRectForSourceOffset(localSourceOffset).translated(cell.textOrigin())
                             : cell.text.cursorRect(position.text.textOffset).translated(cell.textOrigin());
        if (hit.cursorRect.isEmpty()) {
          hit.cursorRect =
              QRectF(cell.rect.left() + 6.0, cell.rect.top() + 4.0, 1.0, qMax<qreal>(14.0, cell.rect.height() - 8.0));
        }
        return hit;
      }
    }
    break;
  case BlockType::LinkDefinition:
  case BlockType::FootnoteDefinition:
    hit.zone = HitTestResult::Zone::Text;
    hit.cursorRect = definitionCursorRectForSourceOffset(position.text.sourceOffset, theme);
    break;
  default:
    break;
  }

  if (hit.cursorRect.isEmpty()) {
    hit.cursorRect = QRectF(hit.blockRect.left(), hit.blockRect.top(), 1.0, hit.blockRect.height());
  }
  return hit;
}

QVector<QRectF> BlockLayout::selectionRects(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelf(selection, theme);
  for (const auto& child : children_) {
    rects += child->selectionRects(selection, theme);
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsForOffsets(qsizetype startOffset, qsizetype endOffset,
                                                      const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelfForOffsets(startOffset, endOffset, theme);
  for (const auto& child : children_) {
    rects += child->selectionRectsForOffsets(startOffset, endOffset, theme);
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsSelf(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects;
  if (!selection.isSingleBlock() || selection.isCollapsed() || selection.anchor.blockId != id_) {
    return rects;
  }

  switch (type_) {
  case BlockType::Heading:
  case BlockType::Paragraph:
  case BlockType::ListItem:
    break;
  case BlockType::FrontMatter:
  case BlockType::CodeFence:
    return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
  case BlockType::HtmlBlock:
    if (literalEditing_) {
      return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    }
    if (htmlContainer_)
      return rects; // Hidden boundary source does not select its rendered descendants.
    rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
    return rects;
  case BlockType::MathBlock:
    if (literalEditing_) {
      return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    }
    rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
    return rects;
  case BlockType::Table: {
    // Cross-cell selection: classify every cell in row-major order. The anchor cell selects
    // from its endpoint's boundary to the cell edge (in document direction), the focus cell
    // conversely, and cells strictly between the endpoints are fully selected. Cells outside
    // the span stay untouched. (Historically only the FOCUS cell highlighted, using min/max of
    // two cells' incomparable offsets.)
    int anchorIndex = -1;
    int focusIndex = -1;
    int cellCount = 0;
    for (const TableRowLayout& row : tableRows_) {
      for (const TableCellLayout& cell : row.cells) {
        if (cell.nodeId == selection.anchor.text.nodeId) {
          anchorIndex = cellCount;
        }
        if (cell.nodeId == selection.focus.text.nodeId) {
          focusIndex = cellCount;
        }
        ++cellCount;
      }
    }
    if (anchorIndex < 0 || focusIndex < 0) {
      return rects; // endpoints not in this table — the multi-block whole-table path covers it
    }
    const bool anchorFirst = anchorIndex <= focusIndex;

    int cellIndex = 0;
    int rowIndex = 0;
    for (const TableRowLayout& row : tableRows_) {
      int colIndex = 0;
      for (const TableCellLayout& cell : row.cells) {
        const qsizetype cellLength = cell.text.plainText().size();
        qsizetype startOff = -1; // -1 = cell not covered
        qsizetype endOff = -1;
        const qsizetype anchorBoundary = qBound<qsizetype>(0, selection.anchor.text.textOffset, cellLength);
        const qsizetype focusBoundary = qBound<qsizetype>(0, selection.focus.text.textOffset, cellLength);
        if (anchorIndex == focusIndex && cellIndex == anchorIndex) {
          startOff = qMin(anchorBoundary, focusBoundary);
          endOff = qMax(anchorBoundary, focusBoundary);
        } else if (cellIndex == anchorIndex) {
          startOff = anchorFirst ? anchorBoundary : 0;
          endOff = anchorFirst ? cellLength : anchorBoundary;
        } else if (cellIndex == focusIndex) {
          startOff = anchorFirst ? 0 : focusBoundary;
          endOff = anchorFirst ? focusBoundary : cellLength;
        } else if (cellIndex > qMin(anchorIndex, focusIndex) && cellIndex < qMax(anchorIndex, focusIndex)) {
          startOff = 0;
          endOff = cellLength;
        }

        if (startOff >= 0) {
          QVector<QRectF> inlineRects;
          if (endOff > startOff) {
            if (anchorIndex == focusIndex && selection.anchor.text.sourceOffset >= 0 &&
                selection.focus.text.sourceOffset >= 0 && cell.contentSourceStart >= 0) {
              // Same-cell endpoints keep the historical source-offset fidelity.
              inlineRects =
                  cell.text.selectionRectsForSourceOffsets(selection.anchor.text.sourceOffset - cell.contentSourceStart,
                                                           selection.focus.text.sourceOffset - cell.contentSourceStart);
            } else {
              inlineRects = cell.text.selectionRects(startOff, endOff);
            }
          }
          if (!inlineRects.isEmpty()) {
            const QPointF origin = cell.textOrigin();
            for (QRectF rect : inlineRects) {
              rect.translate(origin);
              rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
            }
          } else if (endOff <= startOff) {
            // A covered but empty extent (empty cell, or boundary at the cell edge): emit the
            // cell band so the selection reads as continuous.
            const QRectF band = tableCellRect(rowIndex, colIndex).adjusted(2.0, 2.0, -2.0, -2.0);
            if (!band.isEmpty()) {
              rects.push_back(band);
            }
          }
        }
        ++cellIndex;
        ++colIndex;
      }
      ++rowIndex;
    }
    return rects;
  }
  case BlockType::LinkDefinition:
  case BlockType::FootnoteDefinition:
    return definitionSelectionRects(selection.startOffset(), selection.endOffset(), theme);
  default:
    return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const QPointF origin = inlineTextOrigin();
  const qsizetype localAnchorSourceOffset = selection.anchor.text.sourceOffset >= 0 && contentSourceStart_ >= 0
                                                ? selection.anchor.text.sourceOffset - contentSourceStart_
                                                : -1;
  const qsizetype localFocusSourceOffset = selection.focus.text.sourceOffset >= 0 && contentSourceStart_ >= 0
                                               ? selection.focus.text.sourceOffset - contentSourceStart_
                                               : -1;
  const QVector<QRectF> inlineRects =
      localAnchorSourceOffset >= 0 && localFocusSourceOffset >= 0
          ? inlineLayout_->selectionRectsForSourceOffsets(localAnchorSourceOffset, localFocusSourceOffset)
          : inlineLayout_->selectionRects(selection.startOffset(), selection.endOffset());
  for (QRectF rect : inlineRects) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsSelfForOffsets(qsizetype startOffset, qsizetype endOffset,
                                                          const RenderTheme& theme) const {
  QVector<QRectF> rects;

  switch (type_) {
  case BlockType::Heading:
  case BlockType::Paragraph:
  case BlockType::ListItem:
    break;
  case BlockType::FrontMatter:
  case BlockType::CodeFence:
    return literalSelectionRects(startOffset, endOffset, theme);
  case BlockType::HtmlBlock:
    if (literalEditing_) {
      return literalSelectionRects(startOffset, endOffset, theme);
    }
    if (htmlContainer_)
      return rects;
    if (startOffset != endOffset) {
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
    }
    return rects;
  case BlockType::MathBlock:
    if (literalEditing_) {
      return literalSelectionRects(startOffset, endOffset, theme);
    }
    if (startOffset != endOffset) {
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
    }
    return rects;
  case BlockType::Table:
    if (startOffset != endOffset) {
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
    }
    return rects;
  case BlockType::LinkDefinition:
  case BlockType::FootnoteDefinition:
    return definitionSelectionRects(startOffset, endOffset, theme);
  default:
    return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const QPointF origin = inlineTextOrigin();
  for (QRectF rect : inlineLayout_->selectionRects(startOffset, endOffset)) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::literalSelectionRects(qsizetype startOffset, qsizetype endOffset,
                                                   const RenderTheme& theme) const {
  const QRectF contentRect = literalContentRect(theme);
  const auto& layout = literalTextLayout(theme);
  QVector<QRectF> rects = layout.selectionRects(startOffset, endOffset);
  for (QRectF& rect : rects) {
    rect.translate(contentRect.topLeft());
    rect = rect.adjusted(-1.0, 0, 1.0, 0);
    if (layout.wraps())
      rect = rect.intersected(contentRect.adjusted(0, 0, 1, 0));
  }
  return rects;
}

HitTestResult BlockLayout::hitSelf(QPointF documentPos, const RenderTheme& theme,
                                   const CodeFenceScrollController* scroll,
                                   const QSet<QString>* openSequenceMenus) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  switch (type_) {
  case BlockType::Heading:
  case BlockType::Paragraph:
  case BlockType::ListItem:
    // A `[TOC]` preview block is non-editable: no caret is placed. A click on an
    // entry's row resolves to a `#toc:<nodeId>` href so the existing Ctrl+click
    // link path scrolls to the heading; a click elsewhere selects the block.
    if (isToc_) {
      result.zone = HitTestResult::Zone::SelectBlock;
      for (const TocEntryLayout& entry : tocEntries_) {
        if (entry.rect.contains(documentPos)) {
          result.linkHref = QStringLiteral("#toc:") + entry.target.toString();
          break;
        }
      }
      return result;
    }
    if (inlineLayout_) {
      const QPointF origin = inlineTextOrigin();
      const qreal textLeft = origin.x();
      const QRectF textRect(origin, QSizeF(qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height()));
      if (hasListMarker() && documentPos.x() < textLeft) {
        result.zone = HitTestResult::Zone::Marker;
        result.cursorRect = QRectF(textLeft, origin.y(), 1.0, rect_.height());
        // A task item's whole gutter is the checkbox affordance; widening the
        // hit rect a little makes the 13px box comfortably clickable.
        if (taskListItem_ && taskCheckboxRect(theme).adjusted(-3.0, -3.0, 3.0, 3.0).contains(documentPos)) {
          result.taskCheckboxHit = true;
        }
        return result;
      }
      result.zone = HitTestResult::Zone::Text;
      const QPointF localPos = documentPos - textRect.topLeft();
      result.textOffset = inlineLayout_->hitTestTextOffset(localPos);
      const qsizetype localSourceOffset = inlineLayout_->hitTestSourceOffset(localPos);
      result.sourceOffset = contentSourceStart_ >= 0 ? contentSourceStart_ + localSourceOffset : localSourceOffset;
      result.cursorRect = inlineLayout_->hitTestCursorRect(localPos).translated(textRect.topLeft());
      result.linkHref = inlineLayout_->linkHrefAtLocalPos(localPos);
      result.imageSrc = inlineLayout_->imageSrcAtLocalPos(localPos);
    }
    break;
  case BlockType::FrontMatter:
  case BlockType::CodeFence: {
    const QRectF contentRect = literalContentRect(theme);
    const bool wrap = literalTextLayout(theme).wraps();
    const bool scrollable = type_ == BlockType::CodeFence && !wrap && codeMaxLineWidth_ > contentRect.width() + 0.5;
    const qreal offset = (scroll != nullptr && scrollable) ? scroll->offsetFor(id_) : 0.0;
    if (type_ == BlockType::CodeFence && mermaidState_ == MermaidState::Error &&
        mermaidDiagnostic_.span.hasLocation() && mermaidDiagnosticRect(theme).contains(documentPos)) {
      result.zone = HitTestResult::Zone::Code;
      result.textOffset = qBound<qsizetype>(0, mermaidDiagnostic_.span.offset, literal_.size());
      result.cursorRect = literalVisualCursorRect(result.textOffset, theme);
      return result;
    }
    // A click on the reserved bottom scrollbar strip drives the horizontal thumb instead of
    // placing the caret.
    if (scrollable) {
      const qreal stripH = scrollBarStripHeight(theme);
      const QRectF strip(contentRect.left(), contentRect.bottom() - stripH, contentRect.width(), stripH);
      if (strip.contains(documentPos)) {
        result.zone = HitTestResult::Zone::CodeHorizontalBar;
        result.cursorRect = strip; // document-space strip; EditorView maps it to the viewport for dragging
        return result;
      }
    }
    result.zone = type_ == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
    // Rendered mermaid diagram: a click on a node carrying a safe link follows it
    // (Ctrl+click in EditorView); otherwise fall through to caret placement below.
    result.mermaidRendered = isMermaidRendered();
    if (result.mermaidRendered) {
      const MermaidInteractionHit interaction = mermaidInteractionAt(documentPos, theme, openSequenceMenus);
      result.linkHref = interaction.linkHref;
      result.toolTip = interaction.toolTip;
      result.mermaidMenuActorId = interaction.menuActorId;
      if (!result.linkHref.isEmpty() || !result.mermaidMenuActorId.isEmpty()) {
        return result;
      }
    }
    // Code fences honor the wrap setting (and subtract the horizontal scroll offset when mapping
    // a click); FrontMatter always wraps. Previously both hardcoded wrap, so click mapping was
    // wrong whenever code-block wrap was off.
    result.textOffset =
        literalTextLayout(theme).offsetAtPoint(documentPos - contentRect.topLeft() + QPointF(offset, 0));
    result.cursorRect = literalVisualCursorRect(result.textOffset, theme);
    break;
  }
  case BlockType::MathBlock:
    result.zone = HitTestResult::Zone::Math;
    {
      if (literalEditing_) {
        const QRectF contentRect = mathEditorSourceRect(theme);
        result.textOffset = literalTextLayout(theme).offsetAtPoint(documentPos - contentRect.topLeft());
        result.cursorRect = literalVisualCursorRect(result.textOffset, theme);
      } else {
        result.textOffset = documentPos.x() < rect_.center().x() ? 0 : literal_.size();
        const qreal x = result.textOffset == 0 ? rect_.left() : rect_.right();
        result.cursorRect = QRectF(x, rect_.top(), 1.0, rect_.height());
      }
    }
    break;
  case BlockType::HtmlBlock:
    result.zone = HitTestResult::Zone::Html;
    {
      if (literalEditing_) {
        const QRectF contentRect = rect_.marginsRemoved(LayoutBox::insets(cssBoxGeometry_.usedBox));
        result.textOffset = literalTextLayout(theme).offsetAtPoint(documentPos - contentRect.topLeft());
        result.cursorRect = literalVisualCursorRect(result.textOffset, theme);
      } else {
        if (htmlLayout_ && htmlLayout_->valid()) {
          const QRectF contentRect = rect_.marginsRemoved(LayoutBox::insets(cssBoxGeometry_.usedBox));
          const html::HtmlLayoutResult::HitResult htmlHit = htmlLayout_->hitTest(documentPos - contentRect.topLeft());
          result.linkHref = htmlHit.linkHref;
          result.imageSrc = htmlHit.imageSrc;
        }
        result.textOffset = documentPos.x() < rect_.center().x() ? 0 : literal_.size();
        result.cursorRect = QRectF(result.textOffset == 0 ? rect_.left() : rect_.right(), rect_.top(), 1.0,
                                   htmlContainer_ ? TextFontMetrics(cssBoxGeometry_.font).height() : rect_.height());
      }
    }
    break;
  case BlockType::LinkDefinition:
  case BlockType::FootnoteDefinition:
    return hitDefinition(documentPos, theme);
  case BlockType::ThematicBreak: {
    // A thematic break hosts no editable text, so clicking it SELECTS the whole break
    // (Typora-style: a thin outline is drawn around it and Del/Backspace/Enter remove it). Only a
    // click INSIDE the rule's rect selects it; a click in the half-blockSpacing padded margin
    // above/below returns invalid ({}), so DocumentLayout::hitTest's window loop moves on and the
    // neighbouring block claims the gap — preserving "click above → caret before the rule" and
    // "click below → caret after it / into the empty paragraph beneath".
    if (rect_.contains(documentPos)) {
      result.zone = HitTestResult::Zone::SelectBlock;
    } else {
      return {};
    }
    break;
  }
  default:
    result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
    break;
  }

  return result;
}

HitTestResult BlockLayout::hitTable(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  int rowIndex = 0;
  for (const TableRowLayout& row : tableRows_) {
    if (!row.rect.contains(documentPos)) {
      ++rowIndex;
      continue;
    }
    int columnIndex = 0;
    for (const TableCellLayout& cell : row.cells) {
      if (cell.rect.contains(documentPos)) {
        result.zone = HitTestResult::Zone::TableCell;
        result.textNodeId = cell.nodeId.isValid() ? cell.nodeId : id_;
        result.tableRow = rowIndex;
        result.tableColumn = columnIndex;
        const QPointF textOrigin = cell.textOrigin();
        const QPointF localPos = documentPos - textOrigin;
        result.textOffset = cell.text.hitTestTextOffset(localPos);
        const qsizetype localSourceOffset = cell.text.hitTestSourceOffset(localPos);
        result.sourceOffset =
            cell.contentSourceStart >= 0 ? cell.contentSourceStart + localSourceOffset : localSourceOffset;
        result.cursorRect = cell.text.cursorRectForSourceOffset(localSourceOffset).translated(textOrigin);
        result.linkHref = cell.text.linkHrefAtLocalPos(localPos);
        result.imageSrc = cell.text.imageSrcAtLocalPos(localPos);
        return result;
      }
      ++columnIndex;
    }
    ++rowIndex;
  }

  result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
  return result;
}

QRectF BlockLayout::tableCellRect(int row, int column) const {
  if (row < 0 || row >= static_cast<int>(tableRows_.size())) {
    return {};
  }
  const TableRowLayout& tableRow = tableRows_.at(static_cast<size_t>(row));
  if (column < 0 || column >= static_cast<int>(tableRow.cells.size())) {
    return {};
  }
  return tableRow.cells.at(static_cast<size_t>(column)).rect;
}

} // namespace muffin
