#include "document/DocumentSession.h"
#include "render/DocumentLayout.h"
#include "render/SvgDocument.h"
#include "render/ImageLoader.h"
#include "render/LayoutResources.h"
#include "theme/MarkdownFlow.h"
#include "theme/CssThemeMapper.h"
#include "RenderTestUtils.h"
#include "DocumentVisualFonts.h"

#include <QApplication>
#include <QDir>
#include <QXmlStreamReader>
#include <QTextLayout>
#include <QGlyphRun>

using namespace muffin;

namespace {
void mixedFlow() {
  const QString source = QStringLiteral(
      "<div align=\"center\" class=\"hero\">\n\n# 中文标题\n\n正文 **加粗** 与 [链接](note.md)。\n\n<section style=\"text-align:right\">\n\n嵌套段落\n\n</section>\n\n</div>\n\n外部段落\n");
  DocumentSession session;
  session.setMarkdownText(source, false);
  auto& document = session.document();
  const auto& nodes = document.root().children();
  const auto count = nodes.size();
  MarkdownFlow flow(document.root());
  require(!flow.empty(), "paired boundaries produce render containers");
  require(flow.children(document.root()).size() == 2, "only wrapper and outside paragraph are root children");
  const auto* container = flow.container(*nodes.front());
  require(container && container->children.size() == 3, "wrapper borrows heading, prose and nested section");
  const auto* heading = container->children[0];
  require(heading->parent() == &document.root(), "original AST ownership is unchanged");
  require(flow.parent(*heading) == nodes.front().get(), "render parent is HTML container");
  require(flow.previous(*heading) == nullptr && flow.next(*heading) == container->children[1],
          "semantic sibling order excludes tags");
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000);
  const auto* rendered = layout.block(heading->id());
  require(rendered && rendered->inlineLayout(), "Markdown child remains editable native text");
  require(theme.textAlignmentForElement("h1", heading) & Qt::AlignHCenter, "align inherited through wrapper");
  require(layout.topLevelBlockIdFor(heading->id()) == nodes.front()->id(),
          "navigation resolves nested render ownership");
  const auto* close = layout.block(container->closing->id());
  require(close && close->height() == 0, "closing boundary is mapped without a raw-source bar");
  const auto headingRect = rendered->rect();
  require(headingRect.top() < 100, "opener does not reserve code-block space");
  const auto* outsideLayout = layout.block(nodes.back()->id());
  const auto selectionUpdate =
      layout.rebuildBlock(heading->id(), document, theme, {}, DocumentLayout::BlockUpdateReason::Selection);
  require(selectionUpdate.rebuilt && layout.block(nodes.back()->id()) == outsideLayout,
          "selection in a mixed container retains unrelated document layouts");
  require(layout.block(nodes.front()->id())->selectionRectsSelfForOffsets(0, 5, theme).isEmpty(),
          "hidden opening source does not paint a selection across descendants");
  layout.setEditingHtmlBlock(nodes.front()->id());
  layout.rebuild(document, theme, 1000);
  require(layout.block(nodes.front()->id())->literalEditing(), "opening tag can be edited");
  layout.setEditingHtmlBlock(container->closing->id());
  layout.rebuild(document, theme, 1000);
  require(layout.block(container->closing->id())->literalEditing(), "closing tag can be edited");
  require(document.root().children().size() == count && session.markdownText().toString() == source,
          "layout never rewrites source or AST");
}

void malformedFlow() {
  DocumentSession session;
  session.setMarkdownText("<div>\n\n# Keep me\n\n</section>\n", false);
  MarkdownFlow flow(session.document().root());
  require(flow.empty(), "unmatched/malformed containers retain source fallback");
  session.setMarkdownText(QString("<div>\n\n").repeated(65) + "Body\n\n" + QString("</div>\n\n").repeated(65), false);
  require(MarkdownFlow(session.document().root()).empty(),
          "excessive nesting falls back to source without mistaking an opener for a closer");
}

void editableContainerGeometry() {
  for (bool flex : {false, true}) {
    DocumentSession session;
    session.setMarkdownText(
        "<div class='hero' title='a long attribute to wrap the editable opening tag'>\n\nBody\n\n</div>\n", false);
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        QString(".hero{font-size:20px;line-height:48px;padding:10px;%1}").arg(flex ? "display:flex" : ""), "source",
        {}));
    const auto& nodes = session.document().root().children();
    DocumentLayout layout;
    layout.setEditingHtmlBlock(nodes.front()->id());
    layout.rebuild(session.document(), theme, 360);
    const auto* container = layout.block(nodes.front()->id());
    const auto& source = container->literalTextLayout(theme);
    require(source.lines().size() > 1, "editable tag wraps across multiple source lines");
    require(qAbs(container->literalLineHeight() - 48) < .01 && source.lines().front().rect.height() >= 48,
            "source layout honors the container's computed line height");
    require(layout.block(nodes[1]->id())->rect().top() >= container->literalContentRect(theme).top() + source.height(),
            "rendered descendants start below the complete editable source region");
    const auto caret = container->literalVisualCursorRect(5, theme);
    const auto hit = layout.hitTest({caret.left() + .1, caret.center().y()}, theme);
    require(hit.isValid() && hit.blockId == nodes.front()->id() && qAbs(hit.cursorRect.left() - caret.left()) < 2,
            "wrapped opening tag remains editable at its painted caret position");
  }
}

void centeredImages() {
  const QString src = "https://muffin.test/flow.png";
  auto& resources = LayoutResources::instance();
  const auto key = LayoutResources::imageKey(src);
  LayoutResourceVersion previous;
  for (qreal dpr : {1., 1.25, 1.5, 2.}) {
    QImage image(qRound(80 * dpr), qRound(40 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::blue);
    ImageLoader::instance().store(src, image);
    const auto version = resources.read(key);
    if (previous.state)
      require(version.geometry == previous.geometry && version.paint != previous.paint,
              "higher pixel density changes paint without invalidating logical image geometry");
    previous = version;
    DocumentSession session;
    session.setMarkdownText("<div align=\"center\">\n\n![image](" + src +
                                ")\n\n</div>\n\n"
                                "<div align=\"center\"><img src=\"" +
                                src + "\"></div>\n",
                            false);
    RenderTheme theme = RenderTheme::github();
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 1000);
    const auto& nodes = session.document().root().children();
    for (const auto index : {size_t(1), size_t(3)}) {
      const auto* block = layout.block(nodes[index]->id());
      require(block, "image block remains mapped to original AST");
      qreal first = -1, last = -1;
      for (qreal x = block->rect().left(); x <= block->rect().right(); x += 1) {
        const auto hit = layout.hitTest({x, block->rect().center().y()}, theme);
        if (hit.imageSrc == src) {
          if (first < 0)
            first = x;
          last = x;
        }
      }
      require(first >= 0 && last - first >= 78 && last - first <= 81,
              QString("native image uses logical width: DPR %1, block %2, hits %3..%4")
                  .arg(dpr)
                  .arg(index)
                  .arg(first)
                  .arg(last));
      require(qAbs((first + last) / 2 - block->rect().center().x()) < 2,
              "HTML and Markdown center the image box, including replaced-element advance");
      QImage painted(qCeil(block->rect().width()), qCeil(block->height() + 4), QImage::Format_ARGB32_Premultiplied);
      painted.fill(Qt::white);
      QPainter painter(&painted);
      painter.translate(-block->rect().topLeft());
      block->paint(painter, theme, 0);
      painter.end();
      const auto ink = imageInkBounds(painted, Qt::white);
      require(ink.width() >= 79 && ink.width() <= 81 &&
                  qAbs(ink.center().x() + block->rect().left() - (first + last) / 2) < 2,
              "painted pixels agree with image size and hit geometry at every DPR");
    }
  }
  QImage samePixels(160, 80, QImage::Format_ARGB32_Premultiplied);
  samePixels.fill(Qt::blue);
  ImageLoader::instance().store(src, samePixels);
  require(resources.read(key).geometry != previous.geometry,
          "changing image DPR with identical pixels invalidates its logical dimensions");
}

void containerCssAndEdits() {
  const QString source =
      "<div class=\"hero\" align=\"center\">\n\n# Title\n\nFirst paragraph\n\nSecond paragraph\n\n</div>\n\nOutside\n";
  DocumentSession session;
  session.setMarkdownText(source, false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss(".hero{text-align:left;color:#123456;font-size:20px;padding:10px}"
                              ".hero > h1{color:#ee2200}.hero > p:nth-child(2){font-size:25px}"
                              ".hero + p{color:#008800}.hero:has(> h1){border:2px solid blue}",
                              "flow", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 900);
  const auto& nodes = session.document().root().children();
  const auto* heading = nodes[1].get();
  require(theme.textColorForElement("h1", heading) == QColor("#ee2200"),
          "direct-child selector follows render containment");
  require(theme.textAlignmentForElement("p", nodes[2].get()) & Qt::AlignLeft,
          "author CSS overrides align presentational hint");
  require(qAbs(theme.textFontForElement("p", nodes[2].get()).pointSizeF() - 18.75) < .01,
          "nth-child excludes closing tags and inherits used font metrics");
  require(theme.textColorForElement("p", nodes.back().get()) == QColor("#008800"),
          "adjacent selector sees the container as one sibling");
  require(layout.block(nodes.front()->id())->cssBoxGeometry().usedBox.borderTopWidth == 2,
          "has-child selector sees Markdown heading");
  const auto openId = nodes.front()->id();
  require(session.applyTextDelta(source.indexOf("center"), 6, "right", true),
          "tag source is editable through the normal document edit API");
  const auto result = layout.rebuildBlock(openId, session.document(), theme, {});
  require(result.rebuilt, "tag edit rebuilds borrowed relationships safely");
  const auto flow = theme.markdownFlow(session.document().root());
  require(!flow->empty(), "editing an attribute retains container structure");
  require(session.applyTextDelta(session.markdownText().toString().indexOf("</div>"), 6, "</aside>", true),
          "closing tag can be edited");
  layout.rebuild(session.document(), theme, 900);
  require(theme.markdownFlow(session.document().root())->empty(), "broken pairing returns to explicit source fallback");
}

void svgCapabilities() {
  const QByteArray source =
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><defs><filter id="s" x="-100%" y="-100%" width="300%" height="300%"><feDropShadow dx="12" dy="10" stdDeviation="2" flood-color="red" flood-opacity="0.5"/></filter></defs><rect x="25" y="25" width="30" height="30" fill="blue" filter="url(#s)"/></svg>)svg";
  const auto lowered = svg::qtCompatible(source);
  require(!lowered.contains("feDropShadow") && lowered.contains("feMerge"),
          "drop-shadow lowers to native SVG primitives");
  QXmlStreamReader xml(lowered);
  while (!xml.atEnd())
    xml.readNext();
  require(!xml.hasError(), "lowering preserves valid XML");
  for (qreal dpr : {1., 1.25, 1.5, 2.}) {
    const auto image = svg::rasterize(source, {200, 200}, dpr);
    require(!image.isNull() && image.size() == QSize(qRound(200 * dpr), qRound(200 * dpr)),
            "target and physical DPI dimensions agree");
    const auto sample = [&](int x, int y) { return image.pixelColor(qRound(x * 2 * dpr), qRound(y * 2 * dpr)); };
    require(sample(35, 35).blue() > 200 && sample(35, 35).alpha() > 250, "source graphic survives");
    require(sample(62, 60).red() > 200 && sample(62, 60).alpha() > 30 && sample(62, 60).alpha() < 180,
            "translucent offset shadow renders beyond source bounds");
    require(sample(3, 3).alpha() == 0, "transparent background remains transparent");
  }
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  prepareDocumentVisualFonts();
  runTest("mixedFlow", mixedFlow);
  runTest("malformedFlow", malformedFlow);
  runTest("editableContainerGeometry", editableContainerGeometry);
  runTest("centeredImages", centeredImages);
  runTest("containerCssAndEdits", containerCssAndEdits);
  runTest("svgCapabilities", svgCapabilities);
  runTest(
      "pendingGenerationPromotion", +[] {
        DocumentSession session;
        session.setMarkdownText("Before\n\n# Heading\n\nAfter\n", false);
        auto theme = RenderTheme::github();
        DocumentLayout layout;
        layout.rebuild(session.document(), theme, 900, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        require(session.applyTextDelta(0, session.markdownText().size(), "Replacement\n", true),
                "replace AST before its queued layout refresh");
        layout.ensureBuilt(0, layout.slotCount(), theme);
        layout.rebuild(session.document(), theme, 900, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        layout.buildAll(theme);
        require(layout.block(session.document().root().children().front()->id()),
                "promotion resumes after the matching document generation is installed");
      });
  runTest(
      "typography", +[] {
        const auto theme = RenderTheme::github();
        const auto font = theme.paragraphFont();
        const auto families = QFontDatabase::families();
        const bool symbols = families.contains(QStringLiteral("Segoe UI Emoji")) ||
                             families.contains(QStringLiteral("Apple Color Emoji")) ||
                             families.contains(QStringLiteral("Noto Color Emoji"));
        QTextLayout shaped(symbols ? QStringLiteral("中文 ✅ ❌") : QStringLiteral("中文"), font);
        shaped.beginLayout();
        auto line = shaped.createLine();
        line.setLineWidth(900);
        shaped.endLayout();
        for (const auto& run : shaped.glyphRuns()) {
          for (auto glyph : run.glyphIndexes())
            require(glyph != 0, "document font fallback covers Chinese and common status symbols");
        }
        DocumentSession session;
        session.setMarkdownText("H<sub>2</sub>O and x<sup>2</sup>", false);
        DocumentLayout layout;
        layout.rebuild(session.document(), theme, 900);
        const auto* block = layout.block(session.document().root().children().front()->id());
        bool sub = false, sup = false;
        for (const auto& range : block->inlineLayout()->debugTextFormats(theme, font)) {
          const auto shift = range.format.baselineOffset();
          if (qFuzzyIsNull(shift))
            continue;
          sub |= shift < 0;
          sup |= shift > 0;
          require(range.format.verticalAlignment() == QTextCharFormat::AlignNormal &&
                      range.format.font().pointSizeF() >= font.pointSizeF() * .6,
                  "scripts retain CSS font size without a second Qt size reduction");
        }
        require(sub && sup, "both script baselines use the shared shaping rule");
      });
  runTest(
      "rangeProjectionChanges", +[] {
        DocumentSession session;
        session.setMarkdownText("# Title\n\nBody\n\n</div>\n\nOutside\n", false);
        auto theme = RenderTheme::github();
        DocumentLayout layout;
        layout.rebuild(session.document(), theme, 900);
        for (bool add : {true, false}) {
          const qsizetype oldCount = session.document().root().children().size();
          const QString opener = "<div align=\"center\">\n\n";
          require(session.applyTextDelta(0, add ? 0 : opener.size(), add ? opener : QString(), true),
                  "insert/remove a container boundary through document editing");
          const TopLevelRangeChange change{0, oldCount, qsizetype(session.document().root().children().size()),
                                           session.document().revision()};
          require(layout.rebuildTopLevelRange(change, session.document(), theme, {}).rebuilt,
                  "range update handles entering and leaving a projected container");
          const auto* heading = session.document().root().children()[add ? 1 : 0].get();
          require(bool(theme.textAlignmentForElement("h1", heading) & Qt::AlignHCenter) == add,
                  "range update refreshes inherited alignment");
          DocumentLayout fresh;
          fresh.rebuild(session.document(), theme, 900);
          require(qAbs(layout.totalHeight() - fresh.totalHeight()) < .01 &&
                      layout.block(heading->id())->rect() == fresh.block(heading->id())->rect(),
                  "incremental topology change agrees with a fresh document layout");
        }
      });
}
