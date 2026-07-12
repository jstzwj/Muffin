#include "editor/EditorView.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "editor/EditorViewGeometry.h"
#include "editor/FocusAnimator.h"
#include "editor/HoverAnimator.h"
#include "editor/HtmlBlockHoverController.h"
#include "editor/KeyframeAnimator.h"
#include "render/BlockLayout.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"

#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QLoggingCategory>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QTextLayout>

namespace muffin {

// Pull the stateless geometry helpers (blockComesBefore, blocksBetween, selectableLength, …)
// into scope so the selection-paint call sites read as before.
using namespace editor_geometry;

namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

// Map a block to its CSS host key for decoration/hover lookup ("" → none).
// Duplicated from EditorView.cpp's anonymous namespace: both TUs are part of the
// same EditorView class, and `muffin.perf` is a per-TU category registered by name
// so logging rules apply uniformly. The alternative (a shared internal header)
// isn't worth it for two trivial helpers — revisit if a third TU needs them.
QString hostKeyForBlock(const BlockLayout& block) {
  switch (block.type()) {
    case BlockType::Heading: return QStringLiteral("h%1").arg(block.headingLevel());
    case BlockType::BlockQuote: return QStringLiteral("blockquote");
    default: return QString();
  }
}

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, viewPerf()) {}
};

}  // namespace

void EditorView::paintLoadingOverlay(QPainter& painter) const {
  const QFont font = theme_.headingFont(1);
  const QFontMetricsF metrics(font);
  const QString text = QCoreApplication::translate("muffin::EditorView", "Loading…");

  const QRectF rect = viewport()->rect();
  const qreal textHeight = metrics.height();
  const qreal ringRadius = textHeight * 0.8;
  const qreal dotRadius = qMax(2.0, ringRadius * 0.13);
  const qreal gap = textHeight * 0.45;
  const qreal ringExtent = 2.0 * (ringRadius + dotRadius);  // top dot top → bottom dot bottom
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
    if (behind >= 1.0) behind -= 1.0;
    const qreal brightness = (1.0 - behind) * (1.0 - behind);
    QColor c = dotBase;
    c.setAlphaF(0.12 + 0.88 * brightness);
    painter.setBrush(c);
    const qreal r = dotRadius * (0.6 + 0.4 * brightness);
    painter.save();
    painter.translate(centerX, ringCenterY);
    painter.rotate(i * (360.0 / kDotCount));  // dot 0 at 12 o'clock, advancing clockwise
    painter.drawEllipse(QPointF(0.0, -ringRadius), r, r);
    painter.restore();
  }

  painter.setFont(font);
  painter.setPen(theme_.textColorForElement(QStringLiteral("h1"), nullptr));
  const QRectF textRect(rect.left(), blockTop + ringExtent + gap, rect.width(), textHeight);
  painter.drawText(textRect, Qt::AlignCenter, text);
}

void EditorView::paintEvent(QPaintEvent* event) {
  PerfTimer perf("view.paint");
  const QRegion dirtyRegion = event ? event->region() : QRegion(viewport()->rect());
  if (dirtyRegion.isEmpty()) {
    return;
  }

  QPainter painter(viewport());
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
  DecorationPainter::paintBoxShadow(
      painter, page, theme_.pageBorderRadius(), theme_.pageShadowColor(),
      theme_.pageShadowOffsetX(), theme_.pageShadowOffsetY(),
      theme_.pageShadowBlur(), theme_.pageShadowSpread());
  painter.setBrush(theme_.pageBackgroundColor());
  if (theme_.pageBorderColor().isValid() && theme_.pageBorderWidth() > 0.0) {
    painter.setPen(QPen(theme_.pageBorderColor(), theme_.pageBorderWidth()));
  } else {
    painter.setPen(Qt::NoPen);
  }
  const qreal radius = theme_.pageBorderRadius();
  painter.drawRoundedRect(page, radius, radius);
  // CSS #write::before full-page texture overlay (e.g. phycat's faint dot grid):
  // painted over the card, clipped to it, under the text.
  painter.save();
  painter.setClipRect(page);
  DecorationPainter::paintWriteTexture(painter, theme_, page);
  painter.restore();
  painter.restore();

  ensureVisibleBuilt();

  const QRectF visible = documentViewportRect();
  const QRect dirtyBounds = dirtyRegion.boundingRect().intersected(viewport()->rect());
  const QRectF dirtyDocument = QRectF(dirtyBounds).translated(0, scrollY());
  const QVector<const BlockLayout*> blocks =
      layout_->visibleBlocks(dirtyDocument.intersected(visible).adjusted(0, -80, 0, 80), theme_);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const NodeId activeTopLevel =
      (focusMode_ && cursorPosition_.isValid()) ? layout_->topLevelBlockIdFor(cursorPosition_.blockId) : NodeId();

  // Tell the keyframe driver which animated hosts are visible (it filters to
  // infinite animations and starts/stops its timer accordingly).
  if (keyframeAnimator_ && keyframeAnimator_->hasAnimations()) {
    QSet<QString> visibleHosts;
    const QVector<const BlockLayout*> animationBlocks =
        layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80), theme_);
    for (const BlockLayout* b : animationBlocks) {
      const QString h = hostKeyForBlock(*b);
      if (!h.isEmpty()) { visibleHosts.insert(h); }
    }
    keyframeAnimator_->setVisibleHosts(visibleHosts);
  }

  for (const BlockLayout* block : blocks) {
    const QString host = hostKeyForBlock(*block);
    const AnimatedSample* anim = (keyframeAnimator_ && !host.isEmpty()) ? keyframeAnimator_->sampleFor(host) : nullptr;
    const bool hoverActive = hoverAnimator_ && block->nodeId() == hoverAnimator_->animatedBlockId() && hoverAnimator_->phase() > 0.0;
    const ThemeElementStyle* hoverStyle = (!host.isEmpty() && hoverActive) ? theme_.elementStyle(host + QStringLiteral(":hover")) : nullptr;
    // CSS :focus — the top-level block holding the caret. Parallel to hover; the
    // two are orthogonal (a block can be both hovered and focused).
    const bool focusActive = focusAnimator_ && block->nodeId() == focusAnimator_->animatedBlockId() && focusAnimator_->phase() > 0.0;
    const ThemeElementStyle* focusStyle = (!host.isEmpty() && focusActive) ? theme_.elementStyle(host + QStringLiteral(":focus")) : nullptr;
    const QRectF cssBox = block->cssBorderBox(theme_).translated(0, -scrollY());
    // CSS :focus glow/bg, painted UNDER hover so hover wins on overlap.
    if (focusActive && !host.isEmpty() && focusStyle) {
      if (focusStyle->paint.boxShadowColor.isValid() && focusStyle->paint.boxShadowBlur > 0.0) {
        DecorationPainter::paintGlow(painter, cssBox, focusStyle->paint.boxShadowColor, focusStyle->paint.boxShadowBlur, focusAnimator_->phase());
      }
      if (focusStyle->paint.backgroundColor.isValid()) {
        QColor tint = focusStyle->paint.backgroundColor;
        tint.setAlphaF(tint.alphaF() * focusAnimator_->phase());
        painter.fillRect(cssBox, tint);
      }
    }
    // CSS :hover paint diff from the computed hover style. Prefer the element-style
    // path; legacy HoverEffect remains as fallback for themes not yet represented.
    if (hoverActive && !host.isEmpty()) {
      if (hoverStyle && hoverStyle->paint.boxShadowColor.isValid() && hoverStyle->paint.boxShadowBlur > 0.0) {
        DecorationPainter::paintGlow(painter, cssBox, hoverStyle->paint.boxShadowColor, hoverStyle->paint.boxShadowBlur, hoverAnimator_->phase());
      } else {
        DecorationPainter::paintBlockHoverGlow(painter, theme_, host, cssBox, hoverAnimator_->phase());
      }
      if (hoverStyle && hoverStyle->paint.backgroundColor.isValid()) {
        QColor tint = hoverStyle->paint.backgroundColor;
        tint.setAlphaF(tint.alphaF() * hoverAnimator_->phase());
        painter.fillRect(cssBox, tint);
      }
    }
    // @keyframes glow (colour/blur from the sampled frame).
    if (anim && anim->hasGlow) {
      DecorationPainter::paintGlow(painter, block->rect().translated(0, -scrollY()), anim->glowColor, anim->glowBlur, 1.0);
    }
    // transform:scale() on :hover/:focus. When both are active, hover wins (it is
    // the more transient interaction); themes rarely declare scale on both.
    const qreal hoverScale = hoverStyle ? (1.0 + (hoverStyle->paint.transformScale - 1.0) * hoverAnimator_->phase()) : 1.0;
    const qreal focusScale = focusStyle ? (1.0 + (focusStyle->paint.transformScale - 1.0) * focusAnimator_->phase()) : 1.0;
    const qreal stateScale = qAbs(hoverScale - 1.0) > 0.001 ? hoverScale : focusScale;
    const bool stateWrap = qAbs(stateScale - 1.0) > 0.001;
    const bool wrap = stateWrap || (anim && (anim->hasOpacity || (anim->hasScale && qAbs(anim->scale - 1.0) > 0.001)));
    if (wrap) {
      painter.save();
      if (anim && anim->hasOpacity) { painter.setOpacity(anim->opacity); }
      if (anim && anim->hasScale) {
        const QRectF br = block->rect().translated(0, -scrollY());
        const QPointF c = br.center();
        painter.translate(c);
        painter.scale(anim->scale, anim->scale);
        painter.translate(-c);
      }
      if (stateWrap) {
        const QPointF c = cssBox.center();
        painter.translate(c);
        painter.scale(stateScale, stateScale);
        painter.translate(-c);
      }
    }
    const BlockLayout::BlockPaintState blockState{hoverActive, hoverAnimator_ ? hoverAnimator_->phase() : 0.0,
                                                  focusActive, focusAnimator_ ? focusAnimator_->phase() : 0.0};
    if (focusMode_ && activeTopLevel.isValid() && block->nodeId() != activeTopLevel) {
      painter.save();
      painter.setOpacity(0.35);
      block->paint(painter, theme_, scrollY(), codeFenceScroll_, blockState);
      painter.restore();
    } else {
      block->paint(painter, theme_, scrollY(), codeFenceScroll_, blockState);
    }
    if (wrap) { painter.restore(); }
  }
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

  const QColor color(79, 143, 247, 72);
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
    const bool anchorFirst = blockComesBefore(*layout_, selection_.anchor.blockId, selection_.focus.blockId);
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(*layout_, selection_.anchor.blockId, selection_.focus.blockId);
    for (qsizetype i = 0; i < selectedBlocks.size(); ++i) {
      const BlockLayout* block = selectedBlocks.at(i);
      if (!block) {
        continue;
      }
      const bool isAnchor = block->nodeId() == selection_.anchor.blockId;
      const bool isFocus = block->nodeId() == selection_.focus.blockId;
      qsizetype start = 0;
      qsizetype end = selectableLength(block);
      if (isAnchor) {
        if (anchorFirst) {
          start = block->type() == BlockType::Table ? 0 : selection_.anchor.text.textOffset;
        } else {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.anchor.text.textOffset;
        }
      }
      if (isFocus) {
        if (anchorFirst) {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.focus.text.textOffset;
        } else {
          start = block->type() == BlockType::Table ? 0 : selection_.focus.text.textOffset;
        }
      }
      // Self-only rects: blocksBetween already lists every block (containers and descendants) once,
      // so we must NOT recurse here — recursing would repaint each nested item once per owning
      // ancestor (a darker double-highlighted band) and smear this block's offsets onto its children.
      paintSelectionRectsForBlock(painter, block, block->selectionRectsSelfForOffsets(start, end, theme_));
    }
  }
  painter.restore();
}

void EditorView::paintSelectionRectsForBlock(QPainter& painter, const BlockLayout* block, const QVector<QRectF>& documentRects) const {
  if (block == nullptr) {
    return;
  }
  const bool scrollable = isScrollableCodeFence(block);
  qreal offset = 0.0;
  QRectF clipViewport;  // empty unless scrollable; the visible text window minus the scrollbar strip
  if (scrollable && codeFenceScroll_ != nullptr) {
    offset = codeFenceScroll_->offsetFor(block->nodeId());
    const qreal stripH = BlockLayout::scrollBarStripHeight(theme_);
    const QRectF textDocument = block->literalContentRect(theme_).adjusted(0, 0, 0, -stripH);
    clipViewport = textDocument.translated(0, -scrollY());
  }
  for (QRectF rect : documentRects) {
    if (scrollable) {
      rect.translate(-offset, 0.0);  // match the content's translate(-offset) in paintCodeFence
    }
    rect.translate(0, -scrollY());
    if (scrollable) {
      rect = rect.intersected(clipViewport);
      if (rect.isEmpty()) {
        continue;  // selection scrolled out of the visible window
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
  painter.setPen(QPen(QColor(79, 143, 247), 1.0));  // the selection accent blue
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
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
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
  painter.setBrush(QColor(79, 143, 247, 28));
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
  painter.restore();
}

void EditorView::paintPreedit(QPainter& painter) const {
  if (preedit_.isEmpty()) {
    return;  // no composition — paintInsertionCursor owns lastPaintedCaretDocumentRect_
  }
  const QRectF caretDoc = effectiveCursorRect();
  if (!cursorHit_.isValid() || caretDoc.isEmpty()) {
    lastPaintedCaretDocumentRect_ = {};
    return;
  }

  // Inline-editable blocks (paragraph / heading / list item / TABLE CELL): the preedit is spliced
  // into the block's laid-out text (buildTextLayout), so the glyphs are already rendered by the
  // normal block paint and following text has shifted to make room — including the cell's right/
  // center alignment (tableCellTextOrigin keys off cell.text.size().width(), which includes the
  // splice). Draw ONLY the composition caret from the layout; overlaying glyphs would double-paint.
  if (layout_) {
    const BlockLayout* block = layout_->blockIfPromoted(cursorHit_.blockId);
    const InlineLayout* inlineLayout = nullptr;
    QPointF origin;
    if (block && cursorHit_.zone == HitTestResult::Zone::Text) {
      inlineLayout = block->inlineLayout();
      if (inlineLayout) {
        origin = block->inlineTextOrigin(theme_);
      }
    } else if (block && cursorHit_.zone == HitTestResult::Zone::TableCell &&
               cursorHit_.tableRow >= 0 && cursorHit_.tableColumn >= 0) {
      const auto& rows = block->tableRows();
      if (cursorHit_.tableRow < static_cast<int>(rows.size())) {
        const auto& cells = rows.at(static_cast<size_t>(cursorHit_.tableRow)).cells;
        if (cursorHit_.tableColumn < static_cast<int>(cells.size())) {
          const auto& cell = cells.at(static_cast<size_t>(cursorHit_.tableColumn));
          inlineLayout = &cell.text;
          origin = editor_geometry::tableCellTextOrigin(cell, theme_);
        }
      }
    }
    if (inlineLayout && inlineLayout->hasPreedit()) {
      const QRectF cr = inlineLayout->preeditCursorRect(origin);  // document space
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

  // Literal blocks (code/math/html/front-matter): the preedit is not in the inline layout, so render
  // it as an overlay at the caret (the original approach).
  const QRectF caretView = caretDoc.translated(0, -scrollY());  // document -> viewport coords

  QTextLayout layout(preedit_, preeditFont(), painter.device());
  layout.setFormats(preeditFormats_);
  layout.beginLayout();
  QTextLine line = layout.createLine();
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
  lastPaintedCaretDocumentRect_ =
      QRectF(caretDoc.left(), caretDoc.top(), line.naturalTextWidth(), line.height());
}

QFont EditorView::preeditFont() const {
  // Match the rendered font at the caret so the composition baseline-aligns with surrounding text:
  // code/math/html/front-matter literals use their monospace/math font; everything else the body font.
  if (cursorHit_.isValid()) {
    switch (cursorHit_.zone) {
      case HitTestResult::Zone::Code:
      case HitTestResult::Zone::Html:
      case HitTestResult::Zone::FrontMatter:
        return theme_.codeFont();
      case HitTestResult::Zone::Math:
        return theme_.mathFont();
      default:
        break;
    }
  }
  return theme_.paragraphFont();
}

void EditorView::paintInsertionCursor(QPainter& painter) const {
  if (!preedit_.isEmpty()) {
    return;  // a composition is active — paintPreedit drew the composition caret and owns the rect
  }
  if (!cursorVisible_ || !cursorHit_.isValid()) {
    lastPaintedCaretDocumentRect_ = {};
    return;
  }

  // effectiveCursorRect subtracts a scrollable code fence's horizontal offset, so the caret lands on
  // the translated character: the text is painted with painter.translate(-offset), so without this
  // the caret sat at the natural advance and was clipped out of view whenever the fence scrolled.
  QRectF cursor = effectiveCursorRect();
  lastPaintedCaretDocumentRect_ = cursor;
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  // The cap only guards image/preview lines, whose line height can be hundreds
  // of px; text lines — including large headings (a 2.1rem H1 is ~38px) — must
  // use their natural line height, or the caret is clipped short and looks
  // vertically offset from the glyphs.
  const qreal height = qBound<qreal>(14.0, cursor.height(), 96.0);
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
  painter.setPen(QColor(theme_.mutedTextColor().red(), theme_.mutedTextColor().green(), theme_.mutedTextColor().blue(), 140));
  painter.drawText(badge.viewportRect, Qt::AlignCenter, badgeText);
  painter.restore();
}

void EditorView::paintHtmlHoverOverlay(QPainter& painter) const {
  htmlHover_.paint(painter, htmlHoverInputs(), viewport()->rect());
}

}  // namespace muffin
