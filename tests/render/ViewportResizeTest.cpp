#include "EditorViewTestUtils.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QFile>
#include <QPaintEvent>
#include <QTimer>
#include <QEasingCurve>

namespace {
void testOpaqueViewportSkipsParentPaintAndTracksTheme() {
  struct Parent : QWidget {
    int paints = 0;
    void paintEvent(QPaintEvent* event) override {
      ++paints;
      QWidget::paintEvent(event);
    }
  } parent;
  parent.resize(840, 640);
  EditorView view(&parent);
  view.setGeometry(10, 10, 800, 600);
  DocumentSession session;
  session.setMarkdownText("ordinary paragraph\n", false);
  view.setDocument(session.document());
  const auto definition = CssThemeMapper::fromCss("html,body{background:#123456}#write{background:transparent}"
                                                  "@media(max-width:600px){html,body{background:rgba(20,40,60,.5)}}",
                                                  "opacity", {});
  view.setTheme(RenderTheme::fromDefinition(definition));
  parent.show();
  QApplication::processEvents();
  require(view.viewport()->testAttribute(Qt::WA_OpaquePaintEvent), "solid viewport background is opaque");
  const auto image = view.viewport()->grab().toImage();
  require(image.pixelColor(image.width() - 8, image.height() - 8) == QColor("#123456"),
          "opaque viewport still paints its complete dirty background");
  parent.paints = 0;
  view.viewport()->update(QRect(8, 8, 2, 20));
  QApplication::processEvents();
  require(parent.paints == 0, "caret-size update must not repaint the parent under an opaque viewport");
  view.resize(500, 600);
  QApplication::processEvents();
  require(view.theme().viewportBackgroundColor().alpha() < 255 &&
              !view.viewport()->testAttribute(Qt::WA_OpaquePaintEvent),
          "responsive translucent background restores parent composition");
  view.resize(800, 600);
  QApplication::processEvents();
  require(view.viewport()->testAttribute(Qt::WA_OpaquePaintEvent),
          "returning to a solid media bucket restores opacity");
}

RenderTheme resizeTheme() {
  return RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      QStringLiteral("#write { max-width:900px; padding:20px } p { font-size:16px; margin:12px 0 } "
                     "@media (max-width:600px) { p { font-size:20px; margin:18px 0 } }"),
      QStringLiteral("resize-test"), {}));
}

void drainEstimates(DocumentLayout& layout, const RenderTheme& theme) {
  int slices = 0;
  while (layout.hasPendingViewportEstimates()) {
    layout.refreshViewportEstimates(theme, 64);
    require(++slices < 10000, "viewport estimates must finish");
  }
}

void testLazyResizeRetainsIndicesAndMatchesFreshLayout() {
  DocumentSession session;
  QString markdown;
  for (int i = 0; i < 400; ++i) {
    markdown +=
        QStringLiteral("Paragraph %1 **bold** with wrapping words and a soft\nbreak. ").arg(i).repeated(3) + "\n\n";
    if (i % 50 == 0)
      markdown += "- nested **list** item with words\n  - child item\n\n```cpp\nint value = 42;\n```\n\n";
  }
  session.setMarkdownText(markdown, false);
  auto theme = resizeTheme();
  theme.updateForViewport(1000, 700);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1000, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.ensureBuilt(0, 10, theme);
  const auto& children = session.document().root().children();
  const NodeId distantId = children.back()->id();
  const qsizetype distantIndex = layout.topLevelIndexFor(distantId);
  const NodeId nestedId = children[1]->children().front()->id();
  require(layout.blockIfPromoted(nestedId), "nested list should initially be promoted");

  // Repeat before the previous idle pass finishes: only the newest width wins.
  for (const int width : {720, 480, 650, 500}) {
    const bool stylesChanged = theme.updateForViewport(width, 700);
    require(layout.relayoutForViewportWidth(theme, width, stylesChanged), "lazy resize should retain document indices");
    require(layout.hasPendingViewportEstimates(), "offscreen height work must be deferred");
    require(layout.topLevelIndexFor(distantId) == distantIndex, "resize must preserve distant node lookup");
    require(!layout.blockIfPromoted(nestedId), "stale nested geometry must not escape");
    layout.ensureBuilt(0, 10, theme);
    require(layout.blockIfPromoted(nestedId), "nested geometry must be rebuilt on demand");
    require(!layout.blockIfPromoted(distantId), "resize must not materialize distant blocks");
    layout.refreshViewportEstimates(theme, 1);
    require(layout.hasPendingViewportEstimates(), "one idle slice must not process the whole document");
  }
  drainEstimates(layout, theme);
  layout.buildAll(theme);
  DocumentLayout fresh;
  fresh.rebuild(session.document(), theme, 500);
  require(qAbs(layout.totalHeight() - fresh.totalHeight()) < .01, "resized height must match a fresh layout");
  for (const auto& child : children) {
    const auto* resized = layout.block(child->id());
    const auto* expected = fresh.block(child->id());
    require(resized && expected, "all built blocks must remain indexed");
    require(qAbs(resized->rect().top() - expected->rect().top()) < .01 &&
                qAbs(resized->rect().left() - expected->rect().left()) < .01 &&
                qAbs(resized->height() - expected->height()) < .01,
            QStringLiteral("resized block geometry differs at index %1").arg(layout.topLevelIndexFor(child->id())));
  }
  const auto* block = layout.block(nestedId, theme);
  const QPointF point = block->hitForCursorPosition(inlineCursor(nestedId, 3, 3), theme).cursorRect.center();
  require(layout.hitTest(point, theme).blockId == nestedId, "nested hit testing must use resized geometry");
}

void testCenteringIsAppliedOnDemand() {
  DocumentSession session;
  session.setMarkdownText("first\n\n- nested item\n\nlast", false);
  auto theme = resizeTheme();
  theme.updateForViewport(1200, 700);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1200, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.buildAll(theme);
  const auto id = session.document().root().children()[1]->children().front()->id();
  const qreal before = layout.block(id)->rect().left();
  const bool changed = theme.updateForViewport(1400, 700);
  require(layout.relayoutForViewportWidth(theme, 1400, changed), "centering should reuse layouts");
  require(!layout.hasPendingViewportEstimates(), "centering needs no height estimation");
  require(qAbs(layout.block(id)->rect().left() - before - 100) < .01, "nested geometry must translate exactly once");
  require(qAbs(layout.block(id)->rect().left() - before - 100) < .01, "repeated lookup must not translate twice");
}

void testHtmlViewportUnitsAndEditsDuringResize() {
  DocumentSession session;
  session.setMarkdownText("first paragraph\n\n<div style=\"height:20v&#104;\">HTML</div>\n\nlast paragraph", false);
  auto theme = resizeTheme();
  theme.updateForViewport(1200, 700);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1200, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.buildAll(theme);
  const auto html = session.document().root().children()[1]->id();
  const qreal before = layout.block(html)->height();
  const bool changed = theme.updateForViewport(1200, 1000);
  require(layout.relayoutForViewportWidth(theme, 1200, changed), "HTML viewport change should be incremental");
  require(layout.block(html, theme)->height() > before + 40, "height-only resize must update HTML vh units");
  const auto first = session.document().root().children().front()->id();
  require(session.applyTextEdit(5, 0, "\n\nnew paragraph\n\n", {{first, 5, BlockType::Paragraph}}) !=
              muffin::TextEditResult::Rejected,
          "edit during resize");
  const auto range = session.lastLocalTopLevelRangeChange();
  require(range.isValid() && layout.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt,
          "structural edits must work with unfinished viewport estimates");
  drainEstimates(layout, theme);
  layout.buildAll(theme);
  DocumentLayout fresh;
  fresh.rebuild(session.document(), theme, 1200);
  require(qAbs(layout.totalHeight() - fresh.totalHeight()) < .01, "edited resize geometry must match fresh layout");
}

void testHtmlResizeInvalidatesOnlyViewportStyles() {
  for (int scenario = 0; scenario < 3; ++scenario) {
    const bool dynamic = scenario != 0;
    DocumentSession session;
    session.setMarkdownText(scenario == 0 ? "before <kbd>static</kbd> after"
                            : scenario == 1
                                ? "before <span style=\"font-size:5vh\"><em>dynamic</em></span> after"
                                : "before <span style=\"display:inline-block;font-size:5vh\">dynamic</span> after",
                            false);
    auto theme = resizeTheme();
    theme.updateForViewport(1200, 700);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 1200, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    layout.buildAll(theme);
    const auto& node = *session.document().root().children().front();
    const auto* snapshot = theme.elementStyleForNode(node, "p");
    const auto* element = theme.cssElementForNode(node);
    const bool changed = theme.updateForViewport(1200, 1000);
    require(!changed, "inline HTML does not change theme projections");
    require(layout.relayoutForViewportWidth(theme, 1200, changed), "HTML resize remains incremental");
    if (!dynamic)
      require(!layout.hasPendingViewportEstimates(), "static HTML needs no reflow when only height changes");
    layout.buildAll(theme);
    if (!dynamic) {
      require(theme.elementStyleForNode(node, "p") == snapshot && theme.cssElementForNode(node) == element,
              "ordinary HTML retains the live Markdown style cache");
    } else {
      const auto offset = session.markdownText().indexOf(QStringLiteral("dynamic"));
      const qreal fontSize = theme.inlineStyleForNode(node, offset).text.fontSizePx;
      require(qAbs(fontSize - 50) < .01, "viewport font inheritance through an inline child is refreshed");
    }
    const qreal height = layout.totalHeight();
    auto freshTheme = resizeTheme();
    freshTheme.updateForViewport(1200, 1000);
    DocumentLayout fresh;
    fresh.rebuild(session.document(), freshTheme, 1200);
    require(qAbs(height - fresh.totalHeight()) < .01, "HTML viewport resize agrees with a fresh layout");
  }
}

void testResizeKeepsReadingPositionWithOffscreenCaret() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("a paragraph with enough words to wrap at a narrow width. ")
                              .repeated(4)
                              .append("\n\n")
                              .repeated(80),
                          false);
  EditorView view;
  view.resize(700, 400);
  view.setDocument(session.document());
  view.show();
  QApplication::processEvents();
  const auto& children = session.document().root().children();
  view.setCursorPosition(inlineCursor(children.front()->id(), 0, 0));
  const NodeId anchor = children[40]->id();
  view.verticalScrollBar()->setValue(qRound(view.nodeRect(anchor).top()));
  const qreal before = view.nodeRect(anchor).top() - view.verticalScrollBar()->value();
  view.resize(480, 520);
  QApplication::processEvents();
  const qreal after = view.nodeRect(anchor).top() - view.verticalScrollBar()->value();
  require(qAbs(after - before) < 1, "resize must keep the reading block anchored, even with the caret at the start");
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300)
    QApplication::processEvents();
  require(qAbs(view.nodeRect(anchor).top() - view.verticalScrollBar()->value() - before) < 1,
          "idle height estimation must preserve the same reading anchor");
}

void testLongDocumentResizeDoesNotAccumulateRounding() {
  // Offscreen views use eager layout. The minimal/native runs exercise the
  // same deferred estimates used by the application.
  if (QApplication::platformName() == QLatin1String("offscreen"))
    return;
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("A paragraph with enough words for multiple lines while resizing. ")
                              .repeated(6)
                              .append("\n\n")
                              .repeated(12000),
                          false);
  EditorView view;
  view.setAttribute(Qt::WA_DontShowOnScreen);
  view.resize(1000, 500);
  view.setTheme(RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:900px;padding:20px}p{font-size:17px;line-height:1.65;margin:13.3px 0}",
                              "resize-rounding", {})));
  view.setDocument(session.document());
  view.show();
  QApplication::processEvents();
  const auto& children = session.document().root().children();
  NodeId anchor = children[9600]->id();
  view.scrollToNode(anchor);
  view.verticalScrollBar()->setValue(qRound(view.nodeRect(anchor).top() + 30));
  auto* estimates = view.findChild<QTimer*>("viewportEstimates");
  require(estimates, "deferred viewport estimate timer exists");
  const auto stopTimers = [&] {
    for (auto* timer : view.findChildren<QTimer*>())
      timer->stop();
  };
  stopTimers();
  const auto screenY = [&] { return view.mapDocumentToViewport(view.nodeRect(anchor).topLeft()).y(); };
  const auto readingImage = [&] { return view.viewport()->grab(QRect(40, 0, 200, 120)).toImage(); };
  for (int toggle = 0; toggle < 4; ++toggle) {
    qreal before = screenY();
    const int from = toggle % 2 == 0 ? 1000 : 740;
    const int to = toggle % 2 == 0 ? 740 : 1000;
    const QEasingCurve easing(QEasingCurve::OutCubic);
    for (int frame = 1; frame <= 12; ++frame) {
      view.resize(qRound(from + (to - from) * easing.valueForProgress(frame / 12.0)), 500);
      stopTimers();
      require(qAbs(screenY() - before) < .001, "sidebar width animation keeps the reading block stationary");
    }
    QImage referenceImage = readingImage();
    stopTimers();
    // More than 40 slices: repeated rounding must remain bounded throughout
    // the idle pass, rather than merely cancel after opening AND closing.
    bool complete = false;
    for (int slice = 0; slice < 500; ++slice) {
      QMetaObject::invokeMethod(estimates, "timeout", Qt::DirectConnection);
      complete = !estimates->isActive();
      stopTimers();
      require(qAbs(screenY() - before) < .001, QStringLiteral("reading anchor drifted during toggle %1, slice %2: %3px")
                                                   .arg(toggle)
                                                   .arg(slice)
                                                   .arg(screenY() - before));
      require(readingImage() == referenceImage,
              QStringLiteral("visible text pixels changed during idle toggle %1, slice %2").arg(toggle).arg(slice));
      stopTimers();
      if (toggle == 1 && slice == 2) {
        // Explicit navigation supersedes the previous reading position.
        anchor = children[9000]->id();
        view.scrollToNode(anchor);
        view.verticalScrollBar()->setValue(qRound(view.nodeRect(anchor).top() + 30));
        before = screenY();
        referenceImage = readingImage();
      } else if (toggle == 2 && slice == 2) {
        // A scrollbar move must reset rounding before visible promotion runs.
        anchor = children[8400]->id();
        view.verticalScrollBar()->setValue(qRound(view.nodeRect(anchor).top() + 30));
        before = screenY();
        referenceImage = readingImage();
      } else if (toggle == 3 && slice == 2) {
        anchor = children.front()->id();
        view.verticalScrollBar()->setValue(0);
        before = screenY();
        referenceImage = readingImage();
      }
      if (complete)
        break;
    }
    require(complete, "long document resize estimates finish");
    if (toggle == 3)
      require(view.verticalScrollBar()->value() == 0, "idle estimates do not pull away from the document start");
  }
}

void testBudgetedPrefetchAndEviction() {
  DocumentSession session;
  session.setMarkdownText(
      QString("A **bold** *em* paragraph with words for wrapping. ").repeated(3).append("\n\n").repeated(160), false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "p{margin:12px 0}p:nth-child(3n){margin-top:19px}strong:hover{color:red}em:focus{color:blue}"
      "p::before{content:counter(n) '. ';counter-increment:n}",
      "eviction", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 700, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  require(!layout.prefetch(0, 20, theme, 1, 1000) && layout.residentDetailCount() == 1,
          "idle promotion must respect its block budget");
  require(layout.prefetch(0, 0, theme, 1), "an already built range is complete");
  layout.buildAll(theme);
  const qreal total = layout.totalHeight(), top = layout.slotTop(100);
  const auto& nodes = session.document().root().children();
  const NodeId caret = nodes.front()->id();
  {
    auto snapshot = theme;
    const auto* inlineElement = snapshot.cssInlineElement(*nodes[30], 4, "strong");
    const auto count = snapshot.styleCacheStats().elements;
    theme.releaseInlineStyles({nodes[30]->id()});
    require(inlineElement && snapshot.styleCacheStats().elements == count &&
                snapshot.cssInlineElement(*nodes[30], 4, "strong") == inlineElement,
            "retained theme snapshots keep their inline pointers valid");
  }
  const auto before = theme.styleCacheStats().elements;
  const auto evicted = layout.trimDetails(100, 103, theme, {caret}, 8);
  require(evicted == layout.slotCount() - 8 && layout.residentDetailCount() == 8,
          QString("far detail residency must be bounded: slots=%1 evicted=%2 retained=%3")
              .arg(layout.slotCount())
              .arg(evicted)
              .arg(layout.residentDetailCount()));
  require(layout.blockIfPromoted(caret) && layout.blockIfPromoted(nodes[101]->id()) &&
              !layout.blockIfPromoted(nodes[30]->id()),
          "only unprotected distant geometry is released");
  require(qAbs(total - layout.totalHeight()) < .01 && qAbs(top - layout.slotTop(100)) < .01,
          "eviction must retain measured heights and position tokens");
  require(theme.styleCacheStats().elements < before / 2,
          "evicted inline semantic views must be released with layout detail");
  for (int i = 0; i < 3; ++i) {
    layout.ensureBuilt(0, 20, theme);
    layout.trimDetails(0, 4, theme, {caret}, 8);
    layout.ensureBuilt(100, 120, theme);
    layout.trimDetails(100, 103, theme, {caret}, 8);
  }
  const auto offset = nodes[30]->sourceRange().byteStart + 2;
  require(session.applyTextEdit(offset, 0, "new ") != muffin::TextEditResult::Rejected, "edit an evicted block");
  require(layout.rebuildTopLevelRange(session.lastLocalTopLevelRangeChange(), session.document(), theme, {}).rebuilt,
          "local edits must rebind retained navigation after eviction");
  layout.buildAll(theme);
  auto freshTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "p{margin:12px 0}p:nth-child(3n){margin-top:19px}strong:hover{color:red}em:focus{color:blue}"
      "p::before{content:counter(n) '. ';counter-increment:n}",
      "eviction", {}));
  DocumentLayout fresh;
  fresh.rebuild(session.document(), freshTheme, 700);
  require(qAbs(layout.totalHeight() - fresh.totalHeight()) < .01, "rebuilt evicted geometry must match full layout");
  for (const auto& node : session.document().root().children()) {
    const auto* actual = layout.block(node->id());
    const auto* expected = fresh.block(node->id());
    require(actual && expected && qAbs(actual->rect().top() - expected->rect().top()) < .01 &&
                qAbs(actual->height() - expected->height()) < .01,
            "revisiting evicted blocks must preserve structural spacing and counter geometry");
    require(theme.pseudoForNode(*node, "before")->content == freshTheme.pseudoForNode(*node, "before")->content,
            "counter results must survive inline/pseudo eviction");
  }
}

void benchmarkLargeResize() {
  if (!qEnvironmentVariableIsSet("MUFFIN_RESIZE_BENCH"))
    return;
  DocumentSession session;
  QString markdown;
  for (int i = 0; i < 50000; ++i)
    markdown += QStringLiteral("Paragraph %1 repeated words for wrapping. ").arg(i).repeated(4) + "\n\n";
  session.setMarkdownText(markdown, false);
  auto theme = resizeTheme();
  theme.updateForViewport(1000, 700);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1000, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  QElapsedTimer timer;
  timer.start();
  const bool fullRebuild = qEnvironmentVariableIsSet("MUFFIN_RESIZE_BENCH_FULL");
  for (int i = 0; i < 24; ++i) {
    const int width = 500 + (i % 7) * 50;
    const bool changed = theme.updateForViewport(width, 700);
    if (fullRebuild)
      layout.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    else
      require(layout.relayoutForViewportWidth(theme, width, changed), "benchmark must use incremental resize");
    layout.visibleBlocks({0, 0, qreal(width), 700}, theme);
  }
  std::cout << "[resize-bench] mode=" << (fullRebuild ? "full" : "incremental") << " blocks=" << layout.slotCount()
            << " frames=24 totalMs=" << timer.elapsed() << '\n';
}
void benchmarkEditorResize() {
  if (!qEnvironmentVariableIsSet("MUFFIN_RESIZE_VIEW_BENCH"))
    return;
  QFile file(qEnvironmentVariable("MUFFIN_RESIZE_DOCUMENT"));
  require(file.open(QIODevice::ReadOnly), "open resize benchmark document");
  DocumentSession session;
  session.setMarkdownText(QString::fromUtf8(file.readAll()), false);
  const auto cssPath = qEnvironmentVariable("MUFFIN_RESIZE_THEME");
  auto theme = cssPath.isEmpty() ? resizeTheme()
                                 : RenderTheme::fromDefinition(ThemeDefinition::fromCss(cssPath, "resize-benchmark"));
  EditorView view;
  view.resize(1000, 700);
  view.setTheme(theme);
  view.setDocument(session.document());
  view.show();
  QApplication::processEvents();
  if (!session.document().root().children().empty())
    view.setCursorPosition(inlineCursor(session.document().root().children().front()->id(), 0, 0));
  QVector<qreal> resizeMs, frameMs;
  QElapsedTimer timer;
  const int frames = qEnvironmentVariableIntValue("MUFFIN_RESIZE_FRAMES");
  for (int i = 0; i < (frames > 0 ? frames : 40); ++i) {
    timer.start();
    view.resize(700 + (i % 20) * 10, 700 + (i % 3));
    resizeMs.append(timer.nsecsElapsed() / 1000000.);
    QApplication::processEvents();
    view.viewport()->repaint();
    frameMs.append(timer.nsecsElapsed() / 1000000.);
  }
  const auto report = [&](QVector<qreal> values, const char* phase) {
    std::sort(values.begin(), values.end());
    std::cout << "[resize-view-bench] " << phase << " theme=" << qPrintable(cssPath)
              << " p50Ms=" << values[values.size() / 2] << " p95Ms=" << values[values.size() * 95 / 100]
              << " maxMs=" << values.back() << '\n';
  };
  report(resizeMs, "resize");
  report(frameMs, "frame");
}
} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty())
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsSet("MUFFIN_RESIZE_VIEW_BENCH") ? "minimal" : "offscreen");
  QApplication app(argc, argv);
  runTest("lazy resize matches fresh layout", testLazyResizeRetainsIndicesAndMatchesFreshLayout);
  runTest("centering on demand", testCenteringIsAppliedOnDemand);
  runTest("HTML viewport and edits", testHtmlViewportUnitsAndEditsDuringResize);
  runTest("HTML viewport style dependencies", testHtmlResizeInvalidatesOnlyViewportStyles);
  runTest("resize reading anchor", testResizeKeepsReadingPositionWithOffscreenCaret);
  runTest("long document resize rounding", testLongDocumentResizeDoesNotAccumulateRounding);
  runTest("budgeted prefetch and eviction", testBudgetedPrefetchAndEviction);
  runTest("viewport opacity and parent paint", testOpaqueViewportSkipsParentPaintAndTracksTheme);
  benchmarkLargeResize();
  benchmarkEditorResize();
  return 0;
}
