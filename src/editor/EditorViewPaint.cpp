#include "editor/EditorView.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "editor/EditorViewGeometry.h"
#include "editor/HtmlBlockHoverController.h"
#include "render/BlockLayout.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "render/LayoutBox.h"
#include "document/MarkdownDocument.h"

#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include "render/TextLayout.h"
#include <QLoggingCategory>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace muffin {

// Pull the stateless geometry helpers (blockComesBefore, blocksBetween, selectableLength, …)
// into scope so the selection-paint call sites read as before.
using namespace editor_geometry;

namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

// The selection overlay color from the theme, capped to translucent: the overlay paints ON TOP of
// rendered text, so an opaque theme selection (GitHub's #d7e8ff) must stay see-through enough to
// read the glyphs under it. Themes that design their own translucency keep it up to the cap.
QColor selectionOverlayColor(const RenderTheme& theme) {
  QColor color = theme.selectionColor();
  if (color.alphaF() > 0.5) {
    color.setAlphaF(0.5);
  }
  return color;
}

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, viewPerf()) {}
};

} // namespace

void EditorView::paintLoadingOverlay(QPainter& painter) const {
  const QFont font = theme_.headingFont(1);
  const TextFontMetrics metrics(font);
  const QString text = QCoreApplication::translate("muffin::EditorView", "Loading…");

  const QRectF rect = viewport()->rect();
  const qreal textHeight = metrics.height();
  const qreal ringRadius = textHeight * 0.8;
  const qreal dotRadius = qMax(2.0, ringRadius * 0.13);
  const qreal gap = textHeight * 0.45;
  const qreal ringExtent = 2.0 * (ringRadius + dotRadius); // top dot top → bottom dot bottom
  const qreal totalHeight = ringExtent + gap + textHeight;

  const qreal centerX = rect.left() + rect.width() / 2.0;
  const qreal blockTop = rect.top() + (rect.height() - totalHeight) / 2.0;
  const qreal ringCenterY = blockTop + ringExtent / 2.0;

  painter.setRenderHint(QPainter::Antialiasing, true);

  // Pulsing-dot spinner: 12 dots around a ring. A bright wave whose head sits at loadingPhase_
  // sweeps clockwise; each dot's brightness falls off with its distance behind the head, with a
  // faint floor so the full ring stays legible. Dot radius subtly scales with brightness.
  const QColor dotBase = theme_.textColorForElement(QStringLiteral("p"), nullptr);
  constexpr int kDotCount = 12;
  painter.setPen(Qt::NoPen);
  for (int i = 0; i < kDotCount; ++i) {
    // Distance of this dot behind the advancing head, in [0,1); 0 == head (brightest). The +1
    // and single fold keep it positive without fmod: loadingPhase_ - i/N ∈ (-1,1) ⇒ ∈ [0,1) here.
    qreal behind = loadingPhase_ - qreal(i) / qreal(kDotCount) + 1.0;
    if (behind >= 1.0)
      behind -= 1.0;
    const qreal brightness = (1.0 - behind) * (1.0 - behind);
    QColor c = dotBase;
    c.setAlphaF(0.12 + 0.88 * brightness);
    painter.setBrush(c);
    const qreal r = dotRadius * (0.6 + 0.4 * brightness);
    painter.save();
    painter.translate(centerX, ringCenterY);
    painter.rotate(i * (360.0 / kDotCount)); // dot 0 at 12 o'clock, advancing clockwise
    painter.drawEllipse(QPointF(0.0, -ringRadius), r, r);
    painter.restore();
  }

  painter.setFont(font);
  painter.setPen(theme_.textColorForElement(QStringLiteral("h1"), nullptr));
  const QRectF textRect(rect.left(), blockTop + ringExtent + gap, rect.width(), textHeight);
  painter.drawText(textRect, Qt::AlignCenter, text);
}

void EditorView::paintEvent(QPaintEvent* event) {
  // Standalone views/source-mode switches may have no tab activation callback.
  // Resolve deferred geometry before the first frame can expose stale metrics.
  refreshPendingResources();
  PerfTimer perf("view.paint");
  const QRegion dirtyRegion = event ? event->region() : QRegion(viewport()->rect());
  if (dirtyRegion.isEmpty()) {
    return;
  }

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setClipRegion(dirtyRegion);
  for (const QRect& rect : dirtyRegion) {
    painter.fillRect(rect, theme_.viewportBackgroundColor());
  }

  if (loading_) {
    // Async open parse is in flight (no document yet, or a stale one): show a centered spinner +
    // label instead of the stale page so the user sees feedback while the worker parses.
    paintLoadingOverlay(painter);
    return;
  }

  if (!layout_) {
    return;
  }

  const QRectF page = layout_->pageRect(theme_, viewport()->height()).translated(0, -scrollY());
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto* pageStyle = document_ ? theme_.elementStyleForNode(document_->root(), QStringLiteral("#write")) : nullptr;
  if (pageStyle) {
    auto background = *pageStyle;
    background.paint.backgroundColor = theme_.pageBackgroundColor();
    const auto used = theme_.usedBoxForStyle(background, page.width());
    paintLayoutBox(
        painter, LayoutBox::place(QStringLiteral("#write"), std::move(background), used, page, theme_.paragraphFont()));
  } else {
    DecorationPainter::paintBoxShadow(painter, page, theme_.pageBorderRadius(), theme_.pageShadowColor(),
                                      theme_.pageShadowOffsetX(), theme_.pageShadowOffsetY(), theme_.pageShadowBlur(),
                                      theme_.pageShadowSpread());
    painter.setBrush(theme_.pageBackgroundColor());
    if (theme_.pageBorderColor().isValid() && theme_.pageBorderWidth() > 0.0) {
      painter.setPen(QPen(theme_.pageBorderColor(), theme_.pageBorderWidth()));
    } else {
      painter.setPen(Qt::NoPen);
    }
    const qreal radius = theme_.pageBorderRadius();
    painter.drawRoundedRect(page, radius, radius);
  }
  // CSS #write::before full-page texture overlay (e.g. phycat's faint dot grid):
  // painted over the card, clipped to it, under the text.
  painter.save();
  painter.setClipRect(page);
  DecorationPainter::paintWriteTexture(painter, theme_, page,
                                       layout_->documentPseudo(theme_, QStringLiteral("before")));
  painter.restore();
  painter.restore();

  ensureVisibleBuilt();

  const QRectF visible = documentViewportRect();
  const QRect dirtyBounds = dirtyRegion.boundingRect().intersected(viewport()->rect());
  const QRectF dirtyDocument = QRectF(dirtyBounds).translated(0, scrollY());
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(
      (cssPaintTimer_->isActive() ? visible : dirtyDocument.intersected(visible)).adjusted(0, -80, 0, 80), theme_);
  const NodeId activeTopLevel =
      (focusMode_ && cursorPosition_.isValid()) ? layout_->topLevelBlockIdFor(cursorPosition_.blockId) : NodeId();

  cssPaintContext_.timeMs = cssPaintClock_.elapsed();
  cssPaintContext_.needsFrame = false;
  cssPaintContext_.focusedAncestors.clear();
  if (document_ && cursorPosition_.isValid()) {
    const auto focused = cursorPosition_.text.nodeId.isValid() ? cursorPosition_.text.nodeId : cursorPosition_.blockId;
    for (auto* node = document_->node(focused); node; node = node->parent())
      cssPaintContext_.focusedAncestors.insert(node->id());
  }

  const QVector<const BlockLayout*> animationBlocks = layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80), theme_);
  const bool hasVisibleMermaidAnimation =
      std::any_of(animationBlocks.cbegin(), animationBlocks.cend(),
                  [](const BlockLayout* block) { return block && block->hasAnimatedMermaid(); });
  updateMermaidAnimationDriver(hasVisibleMermaidAnimation);
  const qreal mermaidAnimationTime =
      hasVisibleMermaidAnimation && mermaidAnimationClock_.isValid() ? mermaidAnimationClock_.elapsed() / 1000.0 : -1.0;

  for (const BlockLayout* block : blocks) {
    const auto openMenuIt = openSequenceMenus_.constFind(block->nodeId());
    const QSet<QString>* openMenus = openMenuIt == openSequenceMenus_.cend() ? nullptr : &openMenuIt.value();
    BlockLayout::BlockPaintState blockState;
    blockState.css = &cssPaintContext_;
    blockState.focusedNode =
        cursorPosition_.text.nodeId.isValid() ? cursorPosition_.text.nodeId : cursorPosition_.blockId;
    blockState.focusSourceOffset = cursorPosition_.text.sourceOffset;
    blockState.mermaidAnimationTimeSeconds = block->hasAnimatedMermaid() ? mermaidAnimationTime : -1;
    blockState.openSequenceMenus = openMenus;
    if (focusMode_ && activeTopLevel.isValid() && block->nodeId() != activeTopLevel) {
      painter.save();
      painter.setOpacity(0.35);
      block->paint(painter, theme_, scrollY(), codeFenceScroll_, blockState);
      painter.restore();
    } else {
      block->paint(painter, theme_, scrollY(), codeFenceScroll_, blockState);
    }
  }
  if (cssPaintContext_.needsFrame && isVisible()) {
    if (!cssPaintTimer_->isActive())
      cssPaintTimer_->start();
  } else
    cssPaintTimer_->stop();
  paintSelection(painter);
  paintCurrentTableCell(painter);
  paintHtmlHoverOverlay(painter);
  paintPreedit(painter);
  paintInsertionCursor(painter);
  paintHeadingBadge(painter);
}

void EditorView::paintSelection(QPainter& painter) const {
  if (!layout_ || selection_.isCollapsed()) {
    return;
  }

  const QColor color = selectionOverlayColor(theme_);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);

  if (selection_.isSingleBlock()) {
    const BlockLayout* block = layout_->blockIfPromoted(selection_.focus.blockId);
    if (block) {
      paintSelectionRectsForBlock(painter, block, block->selectionRects(selection_, theme_));
      // A whole-selected non-text block (a thematic break) has no text to highlight, so paint a
      // thin Typora-style outline around it instead.
      if (block->type() == BlockType::ThematicBreak) {
        paintSelectedRuleOutline(painter, block);
      }
    }
  } else {
    // The per-block [start, end] ranges live in editor_geometry (shared with the right-click
    // in-selection test) — one source of truth. Self-only rects: the visitor lists every block
    // (containers and descendants) once, so we must NOT recurse — recursing would repaint each
    // nested item once per owning ancestor (a darker double-highlighted band) and smear this
    // block's offsets onto its children.
    editor_geometry::forEachMultiBlockSelectionBlock(
        *layout_, selection_, [this, &painter](const BlockLayout* block, qsizetype start, qsizetype end) {
          paintSelectionRectsForBlock(painter, block, block->selectionRectsSelfForOffsets(start, end, theme_));
        });
  }
  painter.restore();
}

void EditorView::paintSelectionRectsForBlock(QPainter& painter, const BlockLayout* block,
                                             const QVector<QRectF>& documentRects) const {
  if (block == nullptr) {
    return;
  }
  const bool scrollable = isScrollableCodeFence(block);
  qreal offset = 0.0;
  QRectF clipViewport; // empty unless scrollable; the visible text window minus the scrollbar strip
  if (scrollable && codeFenceScroll_ != nullptr) {
    offset = codeFenceScroll_->offsetFor(block->nodeId());
    const qreal stripH = BlockLayout::scrollBarStripHeight(theme_);
    const QRectF textDocument = block->literalContentRect(theme_).adjusted(0, 0, 0, -stripH);
    clipViewport = textDocument.translated(0, -scrollY());
  }
  for (QRectF rect : documentRects) {
    if (scrollable) {
      rect.translate(-offset, 0.0); // match the content's translate(-offset) in paintCodeFence
    }
    rect.translate(0, -scrollY());
    if (scrollable) {
      rect = rect.intersected(clipViewport);
      if (rect.isEmpty()) {
        continue; // selection scrolled out of the visible window
      }
    }
    painter.drawRoundedRect(rect, 2, 2);
  }
}

void EditorView::paintSelectedRuleOutline(QPainter& painter, const BlockLayout* block) const {
  if (block == nullptr) {
    return;
  }
  // Frame the <hr> line (drawn at rect().center().y()) with a thin blue rounded outline — the
  // Typora-style "this block is selected" affordance for a rule, which has no text to highlight.
  // Document coords → view coords via the same -scrollY translate paintSelectionRectsForBlock uses.
  const QRectF box = block->rect();
  const qreal cy = box.center().y();
  const qreal frameH = qMax(theme_.blockSpacing() * 0.8, 14.0);
  QRectF outline(box.left() + 2.0, cy - frameH / 2.0, qMax<qreal>(1.0, box.width() - 4.0), frameH);
  outline.translate(0.0, -scrollY());
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(theme_.selectionColor(), 1.0)); // the themed selection accent
  painter.drawRoundedRect(outline, 4.0, 4.0);
}

bool EditorView::isScrollableCodeFence(const BlockLayout* block) const {
  if (block == nullptr || block->type() != BlockType::CodeFence) {
    return false;
  }
  // Mirrors paintCodeFence's predicate: wrap off (implied by maxLineWidth being meaningful) and a
  // line wider than the content area.
  return block->codeMaxLineWidth() > block->literalContentRect(theme_).width() + 0.5;
}

void EditorView::paintCurrentTableCell(QPainter& painter) const {
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 ||
      cursorHit_.tableColumn < 0) {
    return;
  }

  const BlockLayout* table = layout_->blockIfPromoted(cursorHit_.blockId);
  if (!table || table->type() != BlockType::Table) {
    return;
  }

  QRectF rect = table->tableCellRect(cursorHit_.tableRow, cursorHit_.tableColumn);
  if (rect.isEmpty()) {
    return;
  }
  rect.translate(0, -scrollY());

  painter.save();
  painter.setPen(QPen(theme_.linkColor(), 1.4));
  QColor wash = theme_.selectionColor();
  wash.setAlpha(28); // a faint wash of the themed selection color (opaque themes included)
  painter.setBrush(wash);
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
  painter.restore();
}

void EditorView::paintPreedit(QPainter& painter) const {
  if (preedit_.isEmpty()) {
    return; // no composition — paintInsertionCursor owns lastPaintedCaretDocumentRect_
  }
  const QRectF caretDoc = effectiveCursorRect();
  if (!cursorHit_.isValid() || caretDoc.isEmpty()) {
    lastPaintedCaretDocumentRect_ = {};
    return;
  }

  // Inline-editable blocks (paragraph / heading / list item / TABLE CELL): the preedit is spliced
  // into the block's laid-out text (buildTextLayout), so the glyphs are already rendered by the
  // normal block paint and following text has shifted to make room — including the cell's right/
  // center alignment (TableCellLayout::textOrigin keys off cell.text.size().width(), which includes the
  // splice). Draw ONLY the composition caret from the layout; overlaying glyphs would double-paint.
  if (layout_) {
    const BlockLayout* block = layout_->blockIfPromoted(cursorHit_.blockId);
    const InlineLayout* inlineLayout = nullptr;
    QPointF origin;
    if (block && cursorHit_.zone == HitTestResult::Zone::Text) {
      inlineLayout = block->inlineLayout();
      if (inlineLayout) {
        origin = block->inlineTextOrigin();
      }
    } else if (block && cursorHit_.zone == HitTestResult::Zone::TableCell && cursorHit_.tableRow >= 0 &&
               cursorHit_.tableColumn >= 0) {
      const auto& rows = block->tableRows();
      if (cursorHit_.tableRow < static_cast<int>(rows.size())) {
        const auto& cells = rows.at(static_cast<size_t>(cursorHit_.tableRow)).cells;
        if (cursorHit_.tableColumn < static_cast<int>(cells.size())) {
          const auto& cell = cells.at(static_cast<size_t>(cursorHit_.tableColumn));
          inlineLayout = &cell.text;
          origin = cell.textOrigin();
        }
      }
    }
    if (inlineLayout && inlineLayout->hasPreedit()) {
      const QRectF cr = inlineLayout->preeditCursorRect(origin); // document space
      if (!cr.isEmpty()) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.linkColor());
        painter.drawRect(cr.translated(0, -scrollY()));
        painter.restore();
      }
      lastPaintedCaretDocumentRect_ = cr;
      return;
    }
  }

  // Source blocks paint the composition in their retained source layout. Only
  // its caret belongs here; drawing an overlay would duplicate the glyphs.
  if (layout_) {
    if (const auto* block = layout_->blockIfPromoted(cursorHit_.blockId);
        block &&
        (cursorHit_.zone == HitTestResult::Zone::Code || cursorHit_.zone == HitTestResult::Zone::Math ||
         cursorHit_.zone == HitTestResult::Zone::Html || cursorHit_.zone == HitTestResult::Zone::FrontMatter) &&
        block->literalTextLayout(theme_).hasPreedit()) {
      if (preeditCursor_ >= 0) {
        QRectF cursor = caretDoc.translated(0, -scrollY());
        cursor.setWidth(1.5);
        painter.fillRect(cursor, theme_.linkColor());
        lastPaintedCaretDocumentRect_ = caretDoc;
      } else {
        lastPaintedCaretDocumentRect_ = {};
      }
      return;
    }
  }
  const QRectF caretView = caretDoc.translated(0, -scrollY()); // document -> viewport coords

  TextLayout layout(preedit_, preeditFont());
  layout.setFormats(preeditFormats_);
  layout.beginLayout();
  TextLine line = layout.createLine();
  line.setLineWidth(qMax<qreal>(1.0, viewport()->width() - caretView.left() - 4.0));
  line.setPosition(QPointF(0, 0));
  layout.endLayout();

  painter.save();
  layout.draw(&painter, caretView.topLeft());
  if (preeditCursor_ >= 0 && preeditCursor_ <= preedit_.length()) {
    layout.drawCursor(&painter, caretView.topLeft(), preeditCursor_, 1.5);
  }
  painter.restore();

  // Record the painted overlay rect (document space) so the caret dirty-rect machinery erases it on
  // the next change/scroll — the same contract paintInsertionCursor honours for the blinking caret.
  lastPaintedCaretDocumentRect_ = QRectF(caretDoc.left(), caretDoc.top(), line.naturalTextWidth(), line.height());
}

QFont EditorView::preeditFont() const {
  if (cursorHit_.isValid() && layout_) {
    const auto* block = layout_->blockIfPromoted(cursorHit_.blockId);
    switch (cursorHit_.zone) {
    case HitTestResult::Zone::Code:
    case HitTestResult::Zone::Html:
    case HitTestResult::Zone::FrontMatter:
    case HitTestResult::Zone::Math:
      if (block)
        return block->literalFont();
      break;
    default:
      break;
    }
  }
  return theme_.paragraphFont();
}

void EditorView::paintInsertionCursor(QPainter& painter) const {
  if (editingLocked_) {
    lastPaintedCaretDocumentRect_ = {}; // 锁定编辑: the caret is never drawn
    return;
  }
  if (!preedit_.isEmpty()) {
    return; // a composition is active — paintPreedit drew the composition caret and owns the rect
  }
  if (!cursorVisible_ || !cursorHit_.isValid()) {
    lastPaintedCaretDocumentRect_ = {};
    return;
  }

  // effectiveCursorRect subtracts a scrollable code fence's horizontal offset, so the caret lands on
  // the translated character: the text is painted with painter.translate(-offset), so without this
  // the caret sat at the natural advance and was clipped out of view whenever the fence scrolled.
  QRectF cursor = effectiveCursorRect();
  // The cap only guards image/preview lines, whose line height can be hundreds
  // of px; text lines — including large headings (a 2.1rem H1 is ~38px) — must
  // use their natural line height, or the caret is clipped short and looks
  // vertically offset from the glyphs.
  const qreal height = qBound<qreal>(14.0, cursor.height(), 96.0);
  // Record exactly what is drawn (clamped height, document space, before the viewport translate so
  // off-screen carets still erase on scroll): erase coverage must match the drawn pixels. The old
  // un-clamped record under-covered short lines (drawn bottom top+14 vs recorded +4px margin when
  // h<10 — a ghost tail) and over-dirtied capped image lines.
  lastPaintedCaretDocumentRect_ = QRectF(cursor.left(), cursor.top(), 1.5, height);
  // Blink phase gates only the DRAWING — the recorded rect must survive so the next move (or
  // blink-on) still erases the last painted position.
  if (!caretBlinkOn_) {
    return;
  }
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  QRectF visibleCursor(cursor.left(), cursor.top(), 1.5, height);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme_.linkColor());
  painter.drawRect(visibleCursor);
  painter.restore();
}

void EditorView::paintHeadingBadge(QPainter& painter) const {
  const HeadingBadge badge = headingBadgeForBlock(cursorPosition_.blockId);
  if (!badge.isValid()) {
    return;
  }

  if (!viewport()->rect().intersects(badge.viewportRect.toAlignedRect())) {
    return;
  }

  const QString badgeText = QStringLiteral("H%1").arg(badge.level);

  painter.save();
  painter.setPen(QPen(theme_.codeBorderColor(), 1));
  painter.setBrush(theme_.backgroundColor());
  painter.drawRoundedRect(badge.viewportRect, 3.0, 3.0);
  QFont badgeFont = theme_.paragraphFont();
  badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.8);
  painter.setFont(badgeFont);
  painter.setPen(
      QColor(theme_.mutedTextColor().red(), theme_.mutedTextColor().green(), theme_.mutedTextColor().blue(), 140));
  painter.drawText(badge.viewportRect, Qt::AlignCenter, badgeText);
  painter.restore();
}

void EditorView::paintHtmlHoverOverlay(QPainter& painter) const {
  if (editingLocked_) {
    return; // 锁定编辑: the HTML source-editing toggle button stays hidden
  }
  htmlHover_.paint(painter, htmlHoverInputs(), viewport()->rect());
}

} // namespace muffin
