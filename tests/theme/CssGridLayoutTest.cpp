#include "document/DocumentSession.h"
#include "html/HtmlRenderer.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "BrowserLayoutFont.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <iostream>

using namespace muffin;
namespace {
int failures = 0;
void require(bool condition, const QString& label) {
  if (!condition) {
    ++failures;
    std::cerr << label.toStdString() << '\n';
  }
}
void near(qreal actual, qreal expected, const QString& label) {
  const qreal tolerance = label.contains("baseline") || label.startsWith("inline-") ? 1.25 : .8;
  require(std::abs(actual - expected) < tolerance, label + QString(" actual=%1 expected=%2").arg(actual).arg(expected));
}
void collect(const html::HtmlBox& box, QPointF parent, QHash<QString, QRectF>& rects) {
  const auto& g = box.geometry();
  const auto origin = parent + QPointF(g.left, g.top);
  if (!box.cssId.isEmpty()) rects.insert(box.cssId, QRectF(origin, QSizeF(g.width, g.height)));
  for (const auto& child : box.children()) collect(*child, origin, rects);
}
void browserFixtures() {
  QFile file(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/grid-layout-browser.json"));
  require(file.open(QIODevice::ReadOnly), "open Grid browser fixture");
  const auto reference = QJsonDocument::fromJson(file.readAll()).object();
  const auto aliases = browserLayoutFont(reference);
  const auto cases = reference["cases"].toArray();
  require(cases.size() >= 60, "Grid browser case coverage");
  html::HtmlRenderer renderer;
  for (const auto& value : cases) {
    const auto c = value.toObject();
    for (const int scale : {1, 2}) {
      auto palette = html::HtmlColorPalette::defaultLight();
      palette.fontAliases = aliases;
      palette.cssZoom = scale;
      const auto result = renderer.render(c["html"].toString(), 12, c["viewportWidth"].toInt(800) * scale, {}, palette);
      require(result.valid(), "valid Grid rendering");
      if (!result.root()) continue;
      QHash<QString, QRectF> rects;
      collect(*result.root(), {}, rects);
      const auto origin = rects.value("case").topLeft();
      if (c.contains("imageContent")) {
        const auto ink = c["imageContent"].toObject();
        QImage image(1800, 1500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
          QPainter painter(&image);
          result.paint(painter, {});
        }
        QRect red;
        for (int y = 0; y < image.height(); ++y)
          for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.red() > 200 && color.green() < 40 && color.blue() < 40) red = red.united(QRect(x, y, 1, 1));
          }
        const auto label = c["id"].toString() + " painted image @" + QString::number(scale);
        near(red.x() - origin.x(), ink["x"].toDouble() * scale, label + " x");
        near(red.y() - origin.y(), ink["y"].toDouble() * scale, label + " y");
        near(red.width(), ink["width"].toDouble() * scale, label + " width");
        near(red.height(), ink["height"].toDouble() * scale, label + " height");
      }
      const auto expected = c["expected"].toObject();
      for (auto it = expected.begin(); it != expected.end(); ++it) {
        require(rects.contains(it.key()), "Grid contains " + it.key());
        const auto actual = rects.value(it.key()).translated(-origin);
        const auto e = it.value().toObject();
        const auto label = c["id"].toString() + " " + it.key() + " @" + QString::number(scale);
        near(actual.x(), e["x"].toDouble() * scale, label + " x");
        near(actual.y(), e["y"].toDouble() * scale, label + " y");
        near(actual.width(), e["width"].toDouble() * scale, label + " width");
        near(actual.height(), e["height"].toDouble() * scale, label + " height");
      }
    }
  }
}
void computedGrid() {
  const CssComputedStyleEngine engine(
      CssThemeParser::parse("div{font-size:20px;grid-template-columns:2em 1fr;grid-template-columns:minmax(1fr,20px);grid-column:2/span 3;"
                            "grid-column-start:0;grid-auto-flow:column dense;place-items:center end}"
                            "p{font-size:10px;grid-template-columns:inherit;grid-column:2/4;grid-column:initial;grid-auto-flow:row row}",
                            {}));
  CssElement div;
  div.tag = "div";
  const auto style = CssLayoutStyle::fromComputed(engine.styleFor(div));
  require(style.gridColumns.size() == 2 && style.gridColumns[0].minimum.length.px == 40,
          "Grid invalid track does not override; em computes in parent scope");
  require(style.gridLines[0].number == 2 && style.gridLines[1].span && style.gridLines[1].number == 3,
          "Grid line shorthand and invalid zero");
  require(style.gridFlowColumn && style.gridDense && style.alignItems == "center" && style.justifyItems == "end",
          "Grid flow and placement shorthands");
  CssElement p;
  p.tag = "p";
  p.parent = &div;
  const auto child = CssLayoutStyle::fromComputed(engine.styleFor(p));
  require(child.gridColumns.size() == 2 && child.gridColumns[0].minimum.length.px == 40,
          "Grid explicit inheritance copies computed tracks");
  require(child.gridLines[0].number == 0 && child.gridLines[1].number == 0, "Grid shorthand initial resets both lines");
  require(!child.gridFlowColumn && !child.gridDense, "Grid flow is not inherited");
  require(!parseCssGridTracks("repeat(1001, 1fr)") && !parseCssGridTracks("repeat(2, repeat(2, 1fr))") &&
              !parseCssGridTracks("minmax(10px,20px,)") && !parseCssGridLine("span 0") && !parseCssGridLine("-2147483648"),
          "Grid rejects invalid and unbounded input");
}
void markdownGrid() {
  DocumentSession session;
  session.setMarkdownText("alpha beta gamma delta\n\nsecond paragraph\n\nthird paragraph\n", false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:340px;padding:0;display:grid;grid-template-columns:100px 1fr;gap:10px}"
                              "p{font:16px Arial;line-height:20px;margin:0;min-width:0}p:first-child{grid-column:2;grid-row:1/3}",
                              "grid", {}));
  DocumentLayout eager, lazy;
  eager.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  for (const auto& node : session.document().root().children()) {
    const auto* a = eager.block(node->id());
    const auto* b = lazy.block(node->id());
    require(a && b, "Markdown Grid items built");
    if (!a || !b) continue;
    require(a->rect() == b->rect(), "Grid full/lazy rectangles match");
    const auto cursor = a->inlineLayout()->cursorRect(3).translated(a->inlineTextOrigin());
    const auto hit = eager.hitTest(cursor.center(), theme);
    require(hit.isValid() && hit.textNodeId == node->id() && hit.textOffset == 3, "Grid caret/hit round trip");
    const auto selection = a->selectionRectsForOffsets(0, 3, theme);
    const auto local = a->inlineLayout()->selectionRects(0, 3);
    require(!selection.isEmpty() && selection.size() == local.size(), "Grid selection fragments");
    for (int i = 0; i < selection.size() && i < local.size(); ++i)
      require(selection[i] == local[i].translated(a->inlineTextOrigin()).adjusted(-1, 0, 1, 0), "Grid selection uses allocated geometry");
    require(eager.blockAt(a->rect().center(), theme) == a, "Grid block hit uses both axes");
  }
  const auto& nodes = session.document().root().children();
  near(eager.block(nodes[0]->id())->rect().width(), 230, "Markdown second Grid track width");
  require(eager.block(nodes[0]->id())->rect().left() > eager.block(nodes[1]->id())->rect().left(),
          "Grid placement differs from source order");
  require(session.applyTextDelta(0, 0, "a longer first paragraph with several more words to wrap ", true), "real Grid text edit");
  require(eager.rebuildBlock(session.document().root().children()[0]->id(), session.document(), theme, {}).rebuilt,
          "Grid group invalidated after edit");
  lazy.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  near(eager.totalHeight(), lazy.totalHeight(), "Grid edited/fresh document height");
  for (const auto& node : session.document().root().children())
    require(eager.block(node->id())->rect() == lazy.block(node->id())->rect(), "Grid edit matches fresh geometry");
  session.setMarkdownText("> alpha beta gamma\n>\n> second paragraph\n\nTail", false);
  theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss("#write{max-width:340px;padding:0}blockquote{display:grid;grid-template-columns:"
                                                          "1fr 2fr;gap:10px;width:240px;padding:0;border:0;margin:0}"
                                                          "p{margin:0;font:16px Arial;line-height:20px;min-width:0}",
                                                          "nested-grid", {}));
  eager.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  const auto id = session.document().root().children()[0]->id();
  const auto* quote = eager.block(id);
  require(quote && quote->children().size() == 2 && quote->rect() == lazy.block(id)->rect(), "nested Markdown Grid full/lazy agreement");
  if (quote && quote->children().size() == 2) {
    near(quote->children()[0]->rect().width(), 230.0 / 3, "nested first Grid track");
    near(quote->children()[1]->rect().width(), 460.0 / 3, "nested second Grid track");
  }
  require(session.applyTextDelta(2, 0, "several extra words to increase the height of this nested item ", true), "edit nested Grid text");
  const auto nestedId = session.document().root().children()[0]->children()[0]->id();
  // A container reparse can replace node IDs. Follow the editor's range update
  // and full-rebuild fallback rather than querying the old index with a new ID.
  const auto changedRange = session.lastLocalTopLevelRangeChange();
  if (session.lastParseWasLocalEdit() && changedRange.isValid())
    require(eager.rebuildTopLevelRange(changedRange, session.document(), theme, {}).rebuilt, "nested Grid range rebuild");
  else if (!eager.rebuildBlock(nestedId, session.document(), theme, {}).rebuilt)
    eager.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  near(eager.totalHeight(), lazy.totalHeight(), "nested Grid edited/full layout agreement");
  const auto* edited = eager.block(nestedId);
  const auto* fresh = lazy.block(nestedId);
  require(edited && fresh && edited->rect() == fresh->rect(), "nested Grid edit preserves allocated child geometry");
  theme.setZoomPercent(200);
  eager.rebuild(session.document(), theme, 680, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(session.document(), theme, 680, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  const auto* zoomed = eager.block(nestedId);
  near(zoomed->rect().width(), 460.0 / 3, "Markdown Grid tracks scale once at 200 percent");
  require(zoomed->rect() == lazy.block(nestedId)->rect(), "zoomed Grid full/lazy agreement");
  const auto cursor = zoomed->inlineLayout()->cursorRect(3).translated(zoomed->inlineTextOrigin());
  const auto hit = eager.hitTest(cursor.center(), theme);
  require(hit.textNodeId == nestedId && hit.textOffset == 3, "zoomed nested Grid caret round trip");
}
void responsiveComputedValues() {
  const CssComputedStyleEngine engine(
      CssThemeParser::parse("div{font-size:20px;grid-template-columns:[Before]repeat(auto-fit,[Card]minmax(2em,1fr)[End])[After]40px;"
                            "grid-template-areas:'Title Title' 'Left Right';grid-area:Main}"
                            "p{font-size:10px;grid-template-columns:inherit;grid-template-areas:inherit;grid-area:Main/Right;"
                            "grid-template-areas:'bad bad' 'bad .';grid-template-columns:repeat(auto-fit,1fr)}",
                            {}));
  CssElement div;
  div.tag = "div";
  const auto parent = CssLayoutStyle::fromComputed(engine.styleFor(div));
  CssElement p;
  p.tag = "p";
  p.parent = &div;
  const auto child = CssLayoutStyle::fromComputed(engine.styleFor(p));
  require(parent.gridColumns.automatic && child.gridColumns == parent.gridColumns,
          "automatic repeat inheritance retains computed lengths and names");
  if (child.gridColumns.automatic) {
    const auto& repeat = *child.gridColumns.automatic;
    near(repeat.tracks.front().minimum.length.px, 40, "inherited auto-repeat em is frozen in parent scope");
    require(repeat.beforeNames.contains("Before") && repeat.lineNames.front().contains("Card") &&
                child.gridColumns.lineNames.front().contains("After"),
            "names on both sides of automatic repeat survive computation");
  }
  require(child.gridAreas == parent.gridAreas && child.gridAreas.rectangles.contains("Right"),
          "invalid nonrectangular areas do not override inheritance");
  require(parent.gridLines[0].name == "Main" && parent.gridLines[1].name == "Main" && parent.gridLines[2].name == "Main" &&
              parent.gridLines[3].name == "Main",
          "one named grid-area populates all four lines");
  require(child.gridLines[0].name == "Right" && child.gridLines[1].name == "Right" && child.gridLines[2].name == "Main" &&
              child.gridLines[3].name == "Main",
          "named grid-area omissions use corresponding start names");
  require(!parseCssGridTracks("repeat(auto-fill,1fr)") && !parseCssGridTracks("repeat(auto-fit,80px) repeat(auto-fill,80px)") &&
              !parseCssGridTracks("[auto]100px") && !parseCssGridTracks("[Card]", {}, true) &&
              !parseCssGridTracks("repeat(2,40px)", {}, false) && !parseCssGridTracks("[Card]40px", {}, false) &&
              !parseCssGridAreas("'A A' 'A .'") && !parseCssGridAreas("'A' 'B C'") && !parseCssGridAreas("'A/@'"),
          "invalid named/automatic templates and regions are rejected");
  const auto names = parseCssGridTracks("[A]40px[a]60px[End]");
  require(names && names->lineNames[0] == QStringList{"A"} && names->lineNames[1] == QStringList{"a"}, "custom line names preserve case");
}
void responsiveMarkdown() {
  DocumentSession session;
  session.setMarkdownText("alpha beta gamma delta\n\nsecond paragraph\n\nthird paragraph\n", false);
  const auto checkGeometry = [&](DocumentLayout& eager, DocumentLayout& lazy, const RenderTheme& theme) {
    near(eager.totalHeight(), lazy.totalHeight(), "responsive Grid full/lazy height");
    for (const auto& node : session.document().root().children()) {
      const auto* a = eager.block(node->id());
      const auto* b = lazy.block(node->id());
      require(a && b && a->rect() == b->rect(), "responsive Grid full/lazy boxes");
      if (!a || !b || !a->inlineLayout()) continue;
      const auto caret = a->inlineLayout()->cursorRect(3).translated(a->inlineTextOrigin());
      const auto hit = eager.hitTest(caret.center(), theme);
      require(hit.textNodeId == node->id() && hit.textOffset == 3, "responsive Grid caret round trip");
      const auto selection = a->selectionRectsForOffsets(0, 3, theme);
      const auto local = a->inlineLayout()->selectionRects(0, 3);
      require(!selection.isEmpty() && selection.size() == local.size(), "responsive Grid selection fragments");
      for (int i = 0; i < selection.size() && i < local.size(); ++i)
        require(selection[i] == local[i].translated(a->inlineTextOrigin()).adjusted(-1, 0, 1, 0),
                "responsive Grid selection uses same content origin");
    }
  };
  for (const int zoom : {100, 200}) {
    auto theme =
        RenderTheme::fromDefinition(CssThemeMapper::fromCss("#write{display:grid;width:100%;max-width:none;padding:0;margin:0;grid-"
                                                            "template-columns:repeat(auto-fit,[Card]minmax(140px,1fr)[End]);gap:10px}"
                                                            "p{font:16px Arial;line-height:20px;margin:0;min-width:0}",
                                                            "responsive-fit", {}),
                                    zoom);
    DocumentLayout eager, lazy;
    for (const int cssWidth : {500, 300, 160, 500}) {
      const qreal scale = zoom / 100.0, width = cssWidth * scale;
      theme.updateForViewport(width, 768 * scale);
      eager.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Eager);
      lazy.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      lazy.buildAll(theme);
      checkGeometry(eager, lazy, theme);
      const int columns = qMin(3, qMax(1, int(std::floor((eager.pageWidth() / scale + 10) / 150))));
      const auto* first = eager.block(session.document().root().children()[0]->id());
      near(first->rect().width(), (eager.pageWidth() - (columns - 1) * 10 * scale) / columns, "resized auto-fit recalculates track count");
      const auto* third = eager.block(session.document().root().children()[2]->id());
      require((columns == 3) == qFuzzyCompare(first->rect().top() + 1, third->rect().top() + 1), "auto-fit wraps items after resize");
    }
    theme = RenderTheme::fromDefinition(
        CssThemeMapper::fromCss("#write{display:grid;width:100%;max-width:none;margin:0;padding:0;grid-template-columns:80px "
                                "1fr;grid-template-areas:'Header Header' 'Side Main';gap:10px}"
                                "p{font:16px "
                                "Arial;line-height:20px;margin:0;min-width:0}p:first-child{grid-area:Main}p:nth-child(2){grid-area:Header}"
                                "p:nth-child(3){grid-area:Side}"
                                "@media(max-width:360px){#write{grid-template-columns:1fr;grid-template-areas:'Header' 'Main' 'Side'}}",
                                "responsive-areas", {}),
        zoom);
    for (const int cssWidth : {500, 300, 500}) {
      const qreal scale = zoom / 100.0, width = cssWidth * scale;
      theme.updateForViewport(width, 768 * scale);
      eager.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Eager);
      lazy.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      lazy.buildAll(theme);
      checkGeometry(eager, lazy, theme);
      const auto& nodes = session.document().root().children();
      const auto main = eager.block(nodes[0]->id())->rect(), header = eager.block(nodes[1]->id())->rect(),
                 side = eager.block(nodes[2]->id())->rect();
      require(header.top() < main.top(), "named header is above first source paragraph");
      require(cssWidth > 360 ? main.left() > side.left() && qAbs(main.top() - side.top()) < .1 : main.top() < side.top(),
              "media query switches named area layout");
    }
    require(session.applyTextDelta(0, 0, "text added after resizing the named grid ", true), "edit responsive named Grid");
    require(eager.rebuildBlock(session.document().root().children()[0]->id(), session.document(), theme, {}).rebuilt,
            "responsive named Grid edit invalidates context");
    lazy.rebuild(session.document(), theme, 500 * zoom / 100.0, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    lazy.buildAll(theme);
    checkGeometry(eager, lazy, theme);
  }
}
void overlappingPaintAndHit() {
  html::HtmlRenderer renderer;
  const auto result = renderer.render(
      "<div id='case' style='display:grid;width:120px;grid-template-columns:120px;grid-template-rows:40px;font:16px "
      "Arial;line-height:20px'>"
      "<div id='a' style='grid-area:1/1;order:2;background:red'><a href='front'>AAAA</a></div>"
      "<div id='b' style='grid-area:1/1;background:blue'><a href='back'>BBBB</a></div></div>",
      12, 300);
  require(result.valid(), "overlapping Grid renders");
  if (!result.root()) return;
  QHash<QString, QRectF> rects;
  collect(*result.root(), {}, rects);
  QImage image(350, 150, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    result.paint(painter, {});
  }
  require(image.pixelColor(rects["case"].topLeft().toPoint() + QPoint(100, 30)) == QColor(Qt::red), "Grid paints highest order last");
  require(result.hitTest(rects["case"].topLeft() + QPointF(5, 10)).linkHref == "front", "Grid hit agrees with overlapping paint order");
  const auto clipped = renderer.render(
      "<div id='clip' style='display:grid;width:50px;height:30px;overflow:hidden;grid-template-columns:100px'>"
      "<div><a href='hidden'>AAAAAAAAAAAA</a></div></div>",
      12, 300);
  QHash<QString, QRectF> clippedRects;
  if (clipped.root()) collect(*clipped.root(), {}, clippedRects);
  require(clipped.hitTest(clippedRects["clip"].topLeft() + QPointF(75, 10)).linkHref.isEmpty(),
          "clipped Grid overflow does not intercept clicks");

  DocumentSession session;
  session.setMarkdownText("first paragraph\n\nsecond paragraph", false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{display:grid;max-width:200px;padding:0;grid-template-columns:200px;grid-template-rows:40px}"
      "p{grid-area:1/1;font:16px Arial;line-height:20px;margin:0;background:blue}p:first-child{order:2;background:red}",
      "overlap", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 200, {}, {}, DocumentLayout::BuildPolicy::Eager);
  const auto first = session.document().root().children()[0]->id();
  const auto second = session.document().root().children()[1]->id();
  const auto* block = layout.block(first);
  const auto* topmost = layout.blockAt(block->rect().center(), theme);
  require(topmost && topmost->nodeId() == first, "root Grid overlap uses CSS order for hits");
  const auto visible = layout.visibleBlocks({0, 0, 200, 100}, theme);
  require(visible.size() == 2 && visible[0]->nodeId() == second && visible[1]->nodeId() == first,
          "root Grid paint order preserves separate source order");
  const auto cursor = block->inlineLayout()->cursorRect(3).translated(block->inlineTextOrigin());
  const auto hit = layout.hitTest(cursor.center(), theme);
  require(hit.textNodeId == first && hit.textOffset == 3, "overlapping Grid caret targets visible item");
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    for (const auto* item : visible) item->paint(painter, theme, 0, nullptr);
  }
  require(image.pixelColor(block->rect().topLeft().toPoint() + QPoint(150, 30)) == QColor(Qt::red),
          "Markdown Grid paint uses allocated overlapping boxes");
  theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{display:grid;max-width:200px;padding:0;grid-template-columns:100px}"
                              "p{font:16px Arial;line-height:20px;margin:0;height:30px;overflow:hidden;border:3px solid green}",
                              "grid-clip", {}));
  layout.rebuild(session.document(), theme, 200, {}, {}, DocumentLayout::BuildPolicy::Eager);
  block = layout.block(first);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    block->paint(painter, theme, 0, nullptr);
  }
  require(image.pixelColor(block->rect().topLeft().toPoint() + QPoint(1, 15)) == QColor("green"),
          "native Grid overflow preserves own border");
}
void computedSubgrid() {
  require(!parseCssGridTracks("subgrid 10px") && !parseCssGridTracks("subgrid repeat(auto-fit,[X])") &&
              !parseCssGridTracks("subgrid repeat(auto-fill,[X]) repeat(auto-fill,[Y])") && !parseCssGridTracks("subgrid", {}, false) &&
              !parseCssGridTracks("fit-content(1fr)") && !parseCssGridTracks("fit-content(-10px)") &&
              !parseCssGridTracks("repeat(auto-fill,fit-content(10px))"),
          "Subgrid and fit-content reject invalid template grammar");
  const CssComputedStyleEngine engine(CssThemeParser::parse(
      "div{font-size:20px;grid-template-columns:subgrid [Upper] repeat(auto-fill,[Cell]) [End];"
      "grid-template-rows:fit-content(2em)}p{font-size:10px;grid-template-columns:inherit;grid-template-rows:inherit}",
      {}));
  CssElement div;
  div.tag = "div";
  CssElement p;
  p.tag = "p";
  p.parent = &div;
  const auto parent = CssLayoutStyle::fromComputed(engine.styleFor(div));
  const auto child = CssLayoutStyle::fromComputed(engine.styleFor(p));
  require(parent.gridColumns.subgrid && parent.gridColumns.nameRepeat && parent.gridColumns.lineNames.front().contains("Upper"),
          "Computed subgrid retains authored case-sensitive names and automatic name repetition");
  require(child.gridColumns == parent.gridColumns && child.gridRows == parent.gridRows && child.gridRows[0].fitContent &&
              child.gridRows[0].maximum.length.px == 40,
          "Explicit inheritance copies computed subgrid and frozen fit-content length");
}
void markdownSubgrid() {
  for (const bool intrinsic : {false, true}) {
    DocumentSession session;
    session.setMarkdownText("> alpha beta gamma delta\n>\n> second paragraph with wrapping content\n\noutside left\n\noutside right\n",
                            false);
    for (const int zoom : {100, 200}) {
      auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
          "#write{display:grid;max-width:none;margin:0;padding:0;grid-template-columns:" + QString(intrinsic ? "min-content" : "80px") +
              " 1fr;gap:10px}blockquote{display:grid;grid-column:1/-1;grid-template-columns:subgrid;"
              "margin:0;padding:10px;border:2px solid green}p{font:16px Arial;line-height:20px;margin:0;min-width:0}",
          "subgrid", {}));
      theme.setZoomPercent(zoom);
      for (const int viewport : {400, 250, 400}) {
        const qreal scale = zoom / 100.0;
        DocumentLayout eager, lazy;
        eager.rebuild(session.document(), theme, viewport * scale, {}, {}, DocumentLayout::BuildPolicy::Eager);
        lazy.rebuild(session.document(), theme, viewport * scale, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        lazy.buildAll(theme);
        const auto& roots = session.document().root().children();
        require(roots.size() == 3 && roots[0]->children().size() == 2, "Subgrid Markdown tree");
        if (roots.size() != 3 || roots[0]->children().size() != 2) continue;
        const auto* quote = eager.block(roots[0]->id());
        const auto* outsideLeft = eager.block(roots[1]->id());
        const auto* outsideRight = eager.block(roots[2]->id());
        for (size_t i = 0; i < 2; ++i) {
          const auto& node = *roots[0]->children()[i];
          const auto* a = eager.block(node.id());
          const auto* b = lazy.block(node.id());
          require(a && b, "Subgrid native descendants built");
          if (!a || !b) continue;
          require(a->rect() == b->rect(), "Subgrid eager/lazy descendants share geometry");
          const auto* outside = i ? outsideRight : outsideLeft;
          near(a->rect().width(), outside->rect().width() - 12 * scale, "Subgrid uses shared track minus edge inset");
          near(a->rect().left(), outside->rect().left() + (i ? 0 : 12 * scale), "Subgrid native column alignment");
          const auto cursor = a->inlineLayout()->cursorRect(3).translated(a->inlineTextOrigin());
          const auto hit = eager.hitTest(cursor.center(), theme);
          require(hit.isValid() && hit.textNodeId == node.id() && hit.textOffset == 3, "Subgrid caret/hit round trip");
          const auto selected = a->selectionRectsForOffsets(0, 3, theme);
          require(!selected.isEmpty() && a->rect().contains(selected.front().center()), "Subgrid selection uses allocated box");
        }
        near(quote->rect().width(), viewport * scale, "Subgrid root span reflows with viewport");
      }
    }
  }
  DocumentSession session;
  session.setMarkdownText("> alpha beta\n>\n> second\n\noutside left\n\noutside right\n", false);
  const auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{display:grid;max-width:none;margin:0;padding:0;grid-template-columns:min-content 1fr;gap:10px}"
                              "blockquote{display:grid;grid-column:1/-1;grid-template-columns:subgrid;margin:0;padding:0;border:0}"
                              "p{font:16px Arial;line-height:20px;margin:0;min-width:0}",
                              "subgrid-edit", {}));
  DocumentLayout edited, fresh;
  edited.rebuild(session.document(), theme, 400, {}, {}, DocumentLayout::BuildPolicy::Eager);
  const auto before = edited.block(session.document().root().children()[1]->id())->rect().width();
  require(session.applyTextDelta(2, 0, "WWWWWWWWWW ", true), "Edit subgrid descendant text");
  const auto& roots = session.document().root().children();
  require(edited.rebuildBlock(roots[0]->children()[0]->id(), session.document(), theme, {}).rebuilt,
          "Subgrid descendant edit invalidates owning formatting context");
  fresh.rebuild(session.document(), theme, 400, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  fresh.buildAll(theme);
  const auto after = edited.block(roots[1]->id())->rect().width();
  require(after > before + 30, "Subgrid descendant intrinsic text updates parent track");
  for (const auto& node : roots) {
    require(edited.block(node->id())->rect() == fresh.block(node->id())->rect(), "Edited subgrid agrees with fresh lazy layout");
    for (const auto& child : node->children())
      require(edited.block(child->id())->rect() == fresh.block(child->id())->rect(), "Edited subgrid child geometry refreshes");
  }
}
void markdownRowSubgrid() {
  DocumentSession session;
  session.setMarkdownText("> first child\n>\n> second child\n\noutside first\n\noutside second\n", false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{display:grid;max-width:none;margin:0;padding:0;grid-template-columns:100px 1fr;grid-template-rows:auto auto;gap:10px}"
      "blockquote{display:grid;grid-column:1;grid-row:1/3;grid-template-columns:1fr;grid-template-rows:subgrid;"
      "margin:0;padding:5px;border:2px solid green}p{font:16px Arial;line-height:20px;margin:0;min-width:0}"
      "#write>p:nth-child(2){grid-column:2;grid-row:1}#write>p:nth-child(3){grid-column:2;grid-row:2}",
      "row-subgrid", {}));
  for (const int zoom : {100, 200}) {
    theme.setZoomPercent(zoom);
    const qreal scale = zoom / 100.0;
    DocumentLayout eager, lazy;
    eager.rebuild(session.document(), theme, 300 * scale, {}, {}, DocumentLayout::BuildPolicy::Eager);
    lazy.rebuild(session.document(), theme, 300 * scale, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    lazy.buildAll(theme);
    const auto& roots = session.document().root().children();
    require(roots.size() == 3 && roots[0]->children().size() == 2, "Row-subgrid Markdown tree");
    if (roots.size() != 3 || roots[0]->children().size() != 2) continue;
    for (size_t i = 0; i < 2; ++i) {
      const auto& node = *roots[0]->children()[i];
      const auto* a = eager.block(node.id());
      const auto* b = lazy.block(node.id());
      const auto* outside = eager.block(roots[i + 1]->id());
      require(a && b && outside, "Row-subgrid descendants built");
      if (!a || !b || !outside) continue;
      require(a->rect() == b->rect(), "Row-subgrid eager/lazy rectangles");
      near(a->rect().top(), outside->rect().top() + (i ? 0 : 7 * scale), "Native subgrid row starts");
      near(a->rect().height(), outside->rect().height() - 7 * scale, "Native subgrid row sizes");
      const auto caret = a->inlineLayout()->cursorRect(3).translated(a->inlineTextOrigin());
      const auto hit = eager.hitTest(caret.center(), theme);
      require(hit.textNodeId == node.id() && hit.textOffset == 3, "Row-subgrid caret/hit round trip");
    }
    if (zoom == 100) {
      const qreal before = eager.totalHeight();
      require(session.applyTextDelta(2, 0, "many extra words that wrap in the narrow first shared row ", true),
              "Row-subgrid descendant edit");
      const auto& updated = session.document().root().children();
      require(eager.rebuildBlock(updated[0]->children()[0]->id(), session.document(), theme, {}).rebuilt,
              "Row-subgrid edit rebuilds parent tracks");
      require(eager.totalHeight() > before, "Row-subgrid text enlarges parent row");
      lazy.rebuild(session.document(), theme, 300, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      lazy.buildAll(theme);
      near(eager.totalHeight(), lazy.totalHeight(), "Row-subgrid edited/fresh height");
    }
  }
}
void intrinsicMeasurementBoundary() {
  const CssComputedStyleEngine engine(CssThemeParser::parse("div{display:grid;grid-template-columns:1fr 2fr;gap:10px}", {}));
  CssElement element;
  element.tag = "div";
  ThemeElementStyle style;
  style.layout = CssLayoutStyle::fromComputed(engine.styleFor(element));
  int measured = 0;
  CssFormattingItem leaf;
  leaf.intrinsic = {40, 80};
  leaf.measure = [&](const CssMeasureRequest& request) {
    const auto width = request.width;
    ++measured;
    return CssMeasuredContent{{width, 20}, 15};
  };
  const auto intrinsic = intrinsicGridWidths(style, {leaf, leaf});
  near(intrinsic.minContent, 90, "Intrinsic Grid minimum includes tracks and gutter");
  near(intrinsic.maxContent, 250, "Indefinite fr unit includes max-content contribution");
  require(measured == 0, "Intrinsic width query does not perform block layout or text height measurement");
  leaf.style.layout.display = "grid";
  const auto result = layoutGridItems(style, {leaf, leaf}, 300);
  require(measured == 2, "Leaves retain adapter text measurement when CSS display is grid");
  near(result.size.height(), 20, "Grid-styled text leaves retain their content height");
}
void nativeSizingDependencies() {
  DocumentSession session;
  session.setMarkdownText("first\n\nsecond\n\nthird\n", false);
  for (const int zoom : {100, 200}) {
    for (const bool repeat : {false, true}) {
      auto theme = RenderTheme::fromDefinition(
          CssThemeMapper::fromCss("#write{display:grid;max-width:none;margin:0;padding:0;grid-template-columns:1fr;" +
                                      QString(repeat ? "grid-template-rows:repeat(auto-fill,100px);min-height:250px;row-gap:10px"
                                                     : "grid-template-rows:50% auto auto;min-height:180px;row-gap:calc(10% + 5px)") +
                                      "}p{font:16px Arial;line-height:20px;margin:0;height:20px}",
                                  "sizing-native", {}));
      theme.setZoomPercent(zoom);
      const qreal scale = zoom / 100.0;
      for (const qreal width : {300, 160, 300}) {
        DocumentLayout eager, lazy;
        eager.rebuild(session.document(), theme, width * scale, {}, {}, DocumentLayout::BuildPolicy::Eager);
        lazy.rebuild(session.document(), theme, width * scale, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        lazy.buildAll(theme);
        const auto& nodes = session.document().root().children();
        for (const auto& node : nodes)
          require(eager.block(node->id())->rect() == lazy.block(node->id())->rect(), "Sizing dependency eager/lazy geometry");
        near(eager.block(nodes[1]->id())->rect().top() - eager.block(nodes[0]->id())->rect().top(), (repeat ? 110 : 113) * scale,
             "Native percentage or constrained repeat placement");
        near(eager.totalHeight(), lazy.totalHeight(), "Sizing dependency full/lazy height");
        const auto* block = eager.block(nodes[1]->id());
        const auto caret = block->inlineLayout()->cursorRect(3).translated(block->inlineTextOrigin());
        const auto hit = eager.hitTest(caret.center(), theme);
        require(hit.textNodeId == nodes[1]->id() && hit.textOffset == 3, "Sizing dependency caret/hit consistency");
      }
    }
  }
}
void markdownImageSizing() {
  const QByteArray svg(
      "<svg xmlns='http://www.w3.org/2000/svg' width='200' height='100'>"
      "<rect width='200' height='100' fill='red'/></svg>");
  const QString uri = "data:image/svg+xml;base64," + QString::fromLatin1(svg.toBase64());
  DocumentSession session;
  session.setMarkdownText("![x](" + uri + ") after\n", false);
  for (const int zoom : {100, 200}) {
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write{max-width:none;margin:0;padding:0}p{margin:0;font:16px Arial}img{max-width:50%;aspect-ratio:1}", "image-sizing", {}));
    theme.setZoomPercent(zoom);
    const qreal scale = zoom / 100.0;
    for (const int width : {300, 160, 300}) {
      DocumentLayout eager, lazy;
      eager.rebuild(session.document(), theme, width * scale, {}, {}, DocumentLayout::BuildPolicy::Eager);
      lazy.rebuild(session.document(), theme, width * scale, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      lazy.buildAll(theme);
      const auto id = session.document().root().children()[0]->id();
      const auto* a = eager.block(id);
      const auto* b = lazy.block(id);
      require(a && b && a->rect() == b->rect(), "Image percentage eager/lazy layout");
      if (!a || !b) continue;
      QImage image(qCeil(width * scale), qCeil(a->height() + 8), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::white);
      {
        QPainter painter(&image);
        painter.translate(-a->rect().topLeft());
        a->paint(painter, theme, 0, nullptr);
      }
      QRect red;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
          const auto c = image.pixelColor(x, y);
          if (c.red() > 200 && c.green() < 40 && c.blue() < 40) red = red.united(QRect(x, y, 1, 1));
        }
      require(red.isValid(), "Image CSS paints decoded image");
      near(red.width(), width * scale / 2, "Markdown image percentage width");
      near(red.height(), width * scale / 2, "Markdown image authored aspect ratio");
      const auto local = QPointF(red.center()) + a->rect().topLeft() - a->inlineTextOrigin();
      require(a->inlineLayout()->imageSrcAtLocalPos(local) == uri, "Image hit uses the painted CSS size");
      const auto caret = a->inlineLayout()->cursorRect(5).translated(a->inlineTextOrigin());
      const auto hit = eager.hitTest(caret.center(), theme);
      require(hit.textNodeId == id && hit.textOffset == 5, "Caret following resized image round trips");
    }
  }
  auto boxed = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:none;margin:0;padding:0}p{margin:0}img{width:100px;aspect-ratio:1;box-sizing:border-box;"
                              "padding:10px;border:2px solid green}",
                              "image-box", {}));
  DocumentLayout boxedLayout;
  boxedLayout.rebuild(session.document(), boxed, 300, {}, {}, DocumentLayout::BuildPolicy::Eager);
  const auto* block = boxedLayout.block(session.document().root().children()[0]->id());
  QImage painted(400, qCeil(block->height() + 8), QImage::Format_ARGB32_Premultiplied);
  painted.fill(Qt::white);
  {
    QPainter painter(&painted);
    painter.translate(-block->rect().topLeft());
    block->paint(painter, boxed, 0, nullptr);
  }
  QRect red;
  for (int y = 0; y < painted.height(); ++y)
    for (int x = 0; x < painted.width(); ++x) {
      const auto c = painted.pixelColor(x, y);
      if (c.red() > 200 && c.green() < 40 && c.blue() < 40) red = red.united(QRect(x, y, 1, 1));
    }
  near(red.width(), 76, "Native image excludes horizontal padding and borders from painted content");
  near(red.height(), 76, "Native image border-box authored ratio");
  require(painted.pixelColor(red.left() - 11, red.center().y()) == QColor("green"), "Native image paints shared CSS border");
  require(!parseCssAspectRatio("1 auto / 2") && !parseCssAspectRatio("-2") && !parseCssAspectRatio("auto auto") &&
              !parseCssAspectRatio("1/2/3") && !parseCssAspectRatio("1e300/1e-300"),
          "Invalid aspect-ratio declarations rejected");
  const CssComputedStyleEngine engine(
      CssThemeParser::parse("div{aspect-ratio:2;aspect-ratio:-1}p{aspect-ratio:inherit}span{aspect-ratio:initial}", {}));
  CssElement div;
  div.tag = "div";
  CssElement p;
  p.tag = "p";
  p.parent = &div;
  CssElement span;
  span.tag = "span";
  span.parent = &div;
  near(CssLayoutStyle::fromComputed(engine.styleFor(p)).aspectRatio.value, 2, "Computed aspect-ratio explicit inheritance");
  require(CssLayoutStyle::fromComputed(engine.styleFor(span)).aspectRatio.automatic, "Aspect ratio resets to automatic");
}
void markdownInlineContexts() {
  for (const auto& display : {QString("inline-flex"), QString("inline-grid")})
    for (const int zoom : {100, 200}) {
      const QString source = "before <span style=\"display:" + display +
                             ";width:120px;gap:10px;grid-template-columns:50px 60px;background:red\">"
                             "<span style=\"width:50px;height:30px\">AB</span><span style=\"width:60px\"><a "
                             "href=\"https://example.org\">CD</a></span></span> after\n";
      DocumentSession session;
      session.setMarkdownText(source, false);
      auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
          "#write{max-width:none;margin:0;padding:0}p{margin:0;font:16px Arial;line-height:20px}", "inline-context", {}));
      theme.setZoomPercent(zoom);
      const auto id = session.document().root().children()[0]->id();
      for (const int width : {300, 150}) {
        DocumentLayout eager, lazy;
        eager.rebuild(session.document(), theme, width * zoom / 100.0, {}, {}, DocumentLayout::BuildPolicy::Eager);
        lazy.rebuild(session.document(), theme, width * zoom / 100.0, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        lazy.buildAll(theme);
        const auto* block = eager.block(id);
        require(block && block->inlineLayout(), "Markdown inline formatting context exists");
        if (!block || !block->inlineLayout()) continue;
        const auto* text = block->inlineLayout();
        const auto atoms = text->htmlAtomRects();
        require(atoms.size() == 1, "Nested same-name span retains one inline formatting atom");
        if (atoms.isEmpty()) continue;
        near(atoms[0].width(), 120 * zoom / 100.0, "Markdown inline component width");
        require(block->rect() == lazy.block(id)->rect(), "Inline component eager/lazy geometry");
        const auto offset = source.indexOf("AB") + 1;
        const auto caret = text->cursorRectForSourceOffset(offset);
        require(atoms[0].contains(caret.center()), "Inline child caret uses component geometry");
        require(text->hitTestSourceOffset(caret.center()) == offset, "Inline child source caret round trip");
        require(!text->selectionRectsForSourceOffsets(offset - 1, offset + 1).isEmpty(), "Inline child selection has geometry");
        const auto linkCaret = text->cursorRectForSourceOffset(source.indexOf("CD"));
        require(text->linkHrefAtLocalPos(linkCaret.center() + QPointF(3, 0)) == "https://example.org", "Inline child link hit");
        if (width == 150) require(text->visualLineCount() >= 2, "Atomic component participates in wrapping");
        QImage image(width * zoom / 100, qCeil(block->height() + 4), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
          QPainter painter(&image);
          painter.translate(-block->rect().topLeft());
          block->paint(painter, theme, 0, nullptr);
        }
        const auto point = (atoms[0].bottomRight() - QPointF(2, 2) + block->inlineTextOrigin() - block->rect().topLeft()).toPoint();
        require(image.pixelColor(point).red() > 200 && image.pixelColor(point).green() < 40, "Inline component paints shared box");
        SelectionRange selected;
        selected.focus.blockId = id;
        selected.focus.text.nodeId = id;
        selected.focus.text.sourceOffset = offset;
        selected.focus.text.textOffset = 8;
        selected.anchor = selected.focus;
        eager.rebuildBlock(id, session.document(), theme, selected);
        const auto* active = eager.block(id)->inlineLayout();
        require(active->htmlAtomRects().isEmpty() && active->displayText().contains("display:"),
                "Entering inline component reveals editable source");
      }
    }
}
void incrementalFormattingReuse() {
  for (const auto& display : {QString("flex"), QString("grid")}) {
    DocumentSession session;
    session.setMarkdownText("first\n\nsecond\n\nthird", false);
    const QString css =
        "#write{max-width:none;margin:0;padding:0;display:" + display +
        ";grid-template-columns:100px 100px 100px;align-items:start;gap:10px}p{width:100px;margin:0;font:16px Arial;line-height:20px}";
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "reuse", {}));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 340);
    const auto second = session.document().root().children()[1]->id();
    const auto* before = layout.block(second);
    require(session.applyTextDelta(0, 0, "much longer paragraph text ", true), "Incremental formatting real edit");
    const auto first = session.document().root().children()[0]->id();
    require(layout.rebuildBlock(first, session.document(), theme, {}).rebuilt, "Incremental formatting rebuild");
    const auto stats = layout.formattingReuseStats();
    require(stats.reusedBlocks >= 2 && stats.builtBlocks == 1, "Unchanged allocated siblings reuse native layout boxes");
    require(stats.measurementHits > 0, "Unchanged siblings reuse content measurements across edits");
    require(layout.block(second) == before, "Unchanged formatting child keeps layout identity");
    DocumentLayout fresh;
    fresh.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    fresh.buildAll(theme);
    for (const auto& node : session.document().root().children()) {
      require(layout.block(node->id())->rect() == fresh.block(node->id())->rect(), "Incremental formatting equals fresh lazy allocation");
      const auto* block = layout.block(node->id());
      const auto caret = block->inlineLayout()->cursorRectForSourceOffset(1).translated(block->inlineTextOrigin());
      const auto hit = layout.hitTest(caret.center(), theme);
      require(hit.sourceOffset == block->contentSourceStart() + 1, "Reused suffix updates absolute source positions");
    }
    auto decorated = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css + "p::before{content:'prefix'}", "reuse-decoration", {}));
    layout.rebuildBlock(first, session.document(), decorated, {});
    require(layout.formattingReuseStats().reusedBlocks == 0, "Pseudo-element theme changes invalidate materialized boxes");
    auto changed = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css + "p{font-size:24px}", "reuse-change", {}));
    layout.rebuildBlock(first, session.document(), changed, {});
    require(layout.formattingReuseStats().reusedBlocks == 0, "Changed computed styles invalidate reused measurements and boxes");
    layout.rebuild(session.document(), theme, 200);
    require(layout.formattingReuseStats().reusedBlocks == 0, "Explicit full refresh invalidates formatting resources");
  }
  CssFormattingItem item;
  int measurements = 0;
  item.measure = [&](const CssMeasureRequest& request) {
    ++measurements;
    return CssMeasuredContent{{request.width, 20}, 10};
  };
  CssMeasureRequest request{100, 120, 80};
  measureCssItem(item, request);
  measureCssItem(item, request);
  require(measurements == 1, "Identical phase and references reuse measurement");
  request.containingHeight = -1;
  measureCssItem(item, request);
  request.phase = CssLayoutPhase::Intrinsic;
  measureCssItem(item, request);
  CssGridAxisGeometry axis;
  axis.sizes = {100};
  axis.starts = {0};
  axis.definitions.resize(1);
  request.inherited.columns = axis;
  measureCssItem(item, request);
  request.inherited.columns->definitions[0].minimum.kind = CssGridBreadthKind::MinContent;
  measureCssItem(item, request);
  require(measurements == 5, "Phase, definiteness and inherited track definitions independently invalidate measurement");
  for (int width = 0; width < 100; ++width) {
    request.width = width;
    measureCssItem(item, request);
  }
  require(item.measurements->entries.size() <= 64, "Measurement request cache is bounded");
}
void inlineSourceMapping() {
  const QString source =
      "before $x$ ![x](missing.png) <span style=\"display:inline-grid;grid-template-columns:40px 40px\">"
      "<span>A&amp;B &bogus;</span><span>CD</span></span> after";
  DocumentSession session;
  session.setMarkdownText(source, false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:none;margin:0;padding:0}p{font:16px Arial;line-height:20px}", "source-mapping", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 400);
  const auto id = session.document().root().children()[0]->id();
  const auto* text = layout.block(id)->inlineLayout();
  require(text->mathAtomCount() == 1 && text->htmlAtomRects().size() == 1, "Math, image and HTML atoms compose their display mappings");
  const auto offset = source.indexOf(";B") + 1;
  const auto caret = text->cursorRectForSourceOffset(offset);
  require(text->hitTestSourceOffset(caret.center()) == offset, "Entity inside inline Grid preserves source caret positions");
  const auto unknown = source.indexOf("bogus") + 2;
  const auto unknownCaret = text->cursorRectForSourceOffset(unknown);
  require(text->hitTestSourceOffset(unknownCaret.center()) == unknown, "Unknown entities retain literal character source positions");
  const auto after = text->cursorRectForSourceOffset(source.indexOf("after") + 2);
  require(text->hitTestSourceOffset(after.center()) == source.indexOf("after") + 2,
          "Text following mixed atoms preserves source positions");
  SelectionRange selected;
  require(session.applyTextDelta(offset, 0, "Z", true), "Real inline component edit");
  const auto editedId = session.document().root().children()[0]->id();
  selected.focus.blockId = editedId;
  selected.focus.text = {editedId, -1, offset + 1};
  selected.anchor = selected.focus;
  const auto changed = session.lastLocalTopLevelRangeChange();
  if (changed.isValid())
    require(layout.rebuildTopLevelRange(changed, session.document(), theme, selected).rebuilt, "Inline component edit range rebuild");
  else
    require(layout.rebuildBlock(editedId, session.document(), theme, selected).rebuilt, "Inline component edit block rebuild");
  const auto* active = layout.block(editedId)->inlineLayout();
  require(active->htmlAtomRects().isEmpty() && active->displayText().contains("ZB"), "Typed inline component text remains editable");
}
}  // namespace
int main(int argc, char** argv) {
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  browserFixtures();
  computedGrid();
  computedSubgrid();
  markdownSubgrid();
  markdownRowSubgrid();
  intrinsicMeasurementBoundary();
  nativeSizingDependencies();
  markdownImageSizing();
  markdownGrid();
  overlappingPaintAndHit();
  responsiveComputedValues();
  responsiveMarkdown();
  markdownInlineContexts();
  incrementalFormattingReuse();
  inlineSourceMapping();
  return failures ? 1 : 0;
}
