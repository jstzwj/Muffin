#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/DecorationPainter.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"
#include "theme/FontRendering.h"
#include <QRawFont>
#include <QtEndian>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRgb>

#include <functional>
#include <cmath>
#include <limits>

#include "RenderTestUtils.h"
#include "../theme/BrowserLayoutFont.h"

using namespace muffin;

namespace {

// Render a single heading block into an opaque white image translated to the
// block's origin, so rect.left() maps to image x = 0.
QImage renderHeadingImage(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  const QRectF rect = block->rect();
  require(rect.width() > 100.0 && rect.height() > 4.0, QStringLiteral("heading block should be non-trivial"));

  QImage image(int(rect.width()), int(rect.height() + 4), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return image;
}

// Leftmost x where a pixel matches `pred` (scanning columns left→right), or -1.
int leftmostX(const QImage& image, const std::function<bool(QRgb)>& pred) {
  for (int x = 0; x < image.width(); ++x) {
    for (int y = 0; y < image.height(); ++y) {
      if (pred(image.pixel(x, y))) { return x; }
    }
  }
  return -1;
}

int rightmostX(const QImage& image, const std::function<bool(QRgb)>& pred) {
  for (int x = image.width() - 1; x >= 0; --x) {
    for (int y = 0; y < image.height(); ++y) {
      if (pred(image.pixel(x, y))) { return x; }
    }
  }
  return -1;
}

const std::function<bool(QRgb)> isRed = [](QRgb p) {
  return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90;
};
const std::function<bool(QRgb)> isBlack = [](QRgb p) {
  return qRed(p) < 80 && qGreen(p) < 80 && qBlue(p) < 80;
};

QString kBase;

QRect inkBounds(const QImage& image, const std::function<bool(QRgb)>& pred) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (pred(image.pixel(x, y))) bounds = bounds.united(QRect(x, y, 1, 1));
  return bounds;
}

void testAbsolutePseudoUsesHostGeometryAndZoom() {
  const QString css = kBase + QStringLiteral(
      "h3{width:200px;font-size:20px;line-height:40px;padding:10px;box-sizing:border-box}"
      "h3::before{content:'';position:absolute;left:25%;top:50%;width:1em;height:10px;"
      "transform:translateY(-50%);background:#d00000}"
      "h3::after{content:'';position:absolute;right:10%;bottom:5px;width:40px;height:4px;background:#0000d0}");
  for (int zoom : {100, 200}) {
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "positioned", {}), zoom);
    const auto image = renderHeadingImage(theme, "### Heading\n");
    const auto red = inkBounds(image, isRed);
    const auto blue = inkBounds(image, [](QRgb p) { return qBlue(p) > 150 && qRed(p) < 90 && qGreen(p) < 90; });
    const int scale = zoom / 100;
    require(red == QRect(50 * scale, 25 * scale, 20 * scale, 10 * scale), "absolute ::before resolves em, percentages and translate against the host");
    require(blue == QRect(140 * scale, 51 * scale, 40 * scale, 4 * scale), "absolute ::after honors right/bottom at every zoom");
  }
}

void testMaskImageHasItsOwnSize() {
  const QString svg = QStringLiteral("url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
                                     "<rect width='10' height='10'/></svg>\")");
  const QString css = kBase + QStringLiteral(
      "h3{font-size:20px;line-height:50px}h3.md-heading::after{content:'';display:inline-block;"
      "width:2em;height:2em;vertical-align:top;mask-image:%1;mask-size:24px 24px;mask-position:center;mask-repeat:no-repeat;"
      "background:#00d000}").arg(svg);
  for (int zoom : {100, 200}) {
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "mask-size", {}), zoom);
    const auto image = renderHeadingImage(theme, "### Hi\n");
    const auto green = inkBounds(image, [](QRgb p) { return qGreen(p) > 150 && qRed(p) < 90 && qBlue(p) < 90; });
    const int scale = zoom / 100;
    require(green.size() == QSize(24 * scale, 24 * scale), "mask image does not stretch to the pseudo's 2em box");
    require(green.top() == 8 * scale, "vertical-align:top uses the allocated CSS line box");
  }
}

void testShadowCoverageIgnoresFloatingPointNoise() {
  const auto paint = [](qreal top) {
    QImage image(120, 90, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    DecorationPainter::paintBoxShadow(painter, QRectF(20, top, 50, 15.125), 2,
        QColor(90, 120, 150, 100), 0, 2, 3, 1);
    return image;
  };
  // Full and incremental layout can reach the same subpixel boundary from
  // opposite floating-point directions (e.g. imported cm-based page padding).
  const auto low = paint(std::nextafter(20.5, -std::numeric_limits<qreal>::infinity()));
  const auto high = paint(std::nextafter(20.5, std::numeric_limits<qreal>::infinity()));
  require(low == high, "equivalent used geometry gives identical shadow mask coverage");
}

// h3 `::before { position:absolute; left:0; … }` paints a bar at the heading's
// left edge while the (padding-inset) text stays clear of it.
void testAbsoluteBeforeBarAtLeftEdge() {
  const QString css = kBase + QStringLiteral(
      "#write h3 { padding-left:15px; }"
      "#write h3::before { content:''; position:absolute; left:0; width:4px; height:16px;"
      "  background-color:#d00000; border-radius:2px; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("abs"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("### Heading Three\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0, QStringLiteral("absolute ::before bar should paint red ink"));
  require(red < 6, QStringLiteral("absolute bar should sit at the heading left edge (red leftmost=%1)").arg(red));
  require(black > 10, QStringLiteral("heading text should be inset past the bar (black leftmost=%1)").arg(black));
}

// An inline `::before` disc (h4) reserves left space, shifting the text right of
// the marker. A control theme without the marker has text at the left edge.
void testInlineBeforeDiscShiftsText() {
  const QString withCss = kBase + QStringLiteral(
      "#write h4::before { content:''; width:8px; height:8px; border-radius:50%;"
      "  background-color:#d00000; margin-right:10px; }");
  const RenderTheme withTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(withCss, QStringLiteral("disc"), QString()));
  const QImage withImg = renderHeadingImage(withTheme, QStringLiteral("#### Heading Four\n"));
  const int withRed = leftmostX(withImg, isRed);
  const int withBlack = leftmostX(withImg, isBlack);
  require(withRed >= 0 && withRed < 6, QStringLiteral("inline disc should sit at the left edge (red=%1)").arg(withRed));
  require(withBlack > 12, QStringLiteral("text should be shifted right of the disc (black=%1)").arg(withBlack));

  const QImage ctrlImg = renderHeadingImage(RenderTheme::github(), QStringLiteral("#### Heading Four\n"));
  const int ctrlBlack = leftmostX(ctrlImg, isBlack);
  require(ctrlBlack >= 0, QStringLiteral("control heading should paint text"));
  require(ctrlBlack < 8, QStringLiteral("control heading text should start at the left edge (black=%1)").arg(ctrlBlack));
  require(withBlack > ctrlBlack + 8, QStringLiteral("disc heading text should start well right of control text"));
}

// A text `::before` marker (h6 "-") also reserves space and shifts the text.
void testInlineBeforeDashShiftsText() {
  const QString css = kBase + QStringLiteral(
      "#write h6::before { content:'-'; color:#d00000; margin-right:7px; font-weight:700; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("dash"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("###### Heading Six\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0, QStringLiteral("text ::before '-' should paint red ink"));
  require(black > 10, QStringLiteral("text should be shifted right of the dash (black=%1)").arg(black));

  const QImage ctrlImg = renderHeadingImage(RenderTheme::github(), QStringLiteral("###### Heading Six\n"));
  require(black > leftmostX(ctrlImg, isBlack) + 8, QStringLiteral("dash heading text should start well right of control text"));
}

// The h5 hollow ring (border only, no fill) still paints and shifts text — this
// exercises the outline path (borderColor/borderWidth without backgroundColor).
void testInlineBeforeHollowRingShiftsText() {
  const QString css = kBase + QStringLiteral(
      "#write h5::before { content:''; width:8px; height:8px; border-radius:50%;"
      "  border:1.5px solid #d00000; margin-right:10px; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("ring"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("##### Heading Five\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0 && red < 6, QStringLiteral("hollow ring outline should paint at the left edge (red=%1)").arg(red));
  require(black > 12, QStringLiteral("text should be shifted right of the ring (black=%1)").arg(black));
}

// A `::after` mask icon paints to the RIGHT of the heading text (not overlapping
// it). Uses a tiny inline SVG mask so the icon is a solid red shape.
void testAfterIconPaintsRightOfText() {
  const QString svg = QStringLiteral("url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
                                     "<rect width='10' height='10' fill='%2300d000'/></svg>\")");
  const QString css = kBase + QStringLiteral(
      "#write h3.md-heading::after { content:''; width:10px; height:10px; margin-left:4px;"
      "  -webkit-mask:%1 center/contain; mask:%1 center/contain; background-color:#00d000; }").arg(svg);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("after"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("### Hi\n"));
  const std::function<bool(QRgb)> isGreen = [](QRgb p) {
    return qGreen(p) > 150 && qRed(p) < 90 && qBlue(p) < 90;
  };
  const int blackRight = rightmostX(img, isBlack);
  const int greenLeft = leftmostX(img, isGreen);
  require(greenLeft >= 0, QStringLiteral("::after mask icon should paint green ink"));
  require(blackRight >= 0, QStringLiteral("heading text should paint black ink"));
  require(greenLeft > blackRight, QStringLiteral("::after icon should sit right of the text (green=%1 > blackRight=%2)")
                                     .arg(greenLeft).arg(blackRight));
}

void testMaskDoesNotPaintAnUnmaskedRectangle() {
  const QString svg = QStringLiteral("url('data:image/svg+xml,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg>')");
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(kBase + QStringLiteral(
      "h4 { font-size:16px; line-height:24px; margin:0 }"
      "h4::after { content:''; display:inline-block; width:40px; height:40px; vertical-align:top;"
      " mask-image:%1; mask-size:24px 24px; mask-position:center; background-color:#00d000 }").arg(svg), "mask-coverage", {}));
  DocumentSession session;
  session.setMarkdownText("#### Hi\n", false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 300);
  const auto* text = layout.block(session.document().root().children()[0]->id())->inlineLayout();
  QImage image(300, 100, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  { QPainter painter(&image); text->paint(painter, {}); }
  const auto left = qCeil(text->cursorRect(2).left());
  require(qAlpha(image.pixel(left + 2, 2)) == 0 && qAlpha(image.pixel(left + 36, 36)) == 0,
          "mask leaves the atomic box outside its image transparent");
  require(qGreen(image.pixel(left + 20, 20)) > 150, "mask paints its image at the used box center");
}

// Generated inline content must consume the same line box as the heading text.
// A narrow heading therefore wraps after the marker, while the source-visible
// caret offset still starts at the first editable character (the marker is not
// an accidental source character).
void testInlinePseudoParticipatesInWrappingAndCaretMapping() {
  const QString css = kBase + QStringLiteral(
      "#write h4 { width:90px; font-size:20px; line-height:24px; }"
      "#write h4::before { content:''; width:12px; height:12px; margin-right:8px;"
      "  border-radius:50%; background:#d00000; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("wrap"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("#### Long heading that wraps\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 120.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("narrow pseudo fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block && block->inlineLayout(), QStringLiteral("narrow pseudo heading should be laid out"));
  const InlineLayout* inlineLayout = block->inlineLayout();
  require(inlineLayout->visualLineCount() >= 2, QStringLiteral("generated marker must participate in narrow wrapping"));
  require(inlineLayout->plainText().startsWith(QStringLiteral("Long heading")),
          QStringLiteral("generated marker must not enter source-visible text"));
  const QRectF caret = inlineLayout->cursorRect(0);
  require(caret.left() > 12.0, QStringLiteral("caret must start after marker: x=%1 y=%2 min=%3 max=%4")
      .arg(caret.left()).arg(caret.top()).arg(inlineLayout->intrinsicWidths().first).arg(inlineLayout->intrinsicWidths().second));
  require(inlineLayout->hitTestTextOffset(caret.center()) == 0,
          QStringLiteral("clicking the first text caret must map to visible offset zero"));
}

// CSS `content: none` (and `normal`) on ::before/::after means "no generated
// content", not the literal word "none". newsprint declares `blockquote:before
// { content:''; content:none }`; bestValue picks the later `none`, so without the
// guard the blockquote ::before content was stored as "none" and painted verbatim
// before every blockquote. Assert the rule's content is blanked.
void testContentNoneIsNotLiteralText() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "blockquote:before, blockquote:after { content:''; content:none; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("none"), QString()));
  const PseudoElementRule* rule = nullptr;
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == QStringLiteral("blockquote") && r.pseudo == QStringLiteral("before")) { rule = &r; break; }
  }
  require(rule != nullptr, QStringLiteral("blockquote::before rule should be extracted"));
  require(rule->content.isEmpty(),
          QStringLiteral("content:none must suppress the pseudo (got literal '%1')").arg(rule->content));

  // `content: normal` is the same no-content keyword.
  const QString css2 = QStringLiteral("#write { color:#000000; } h1::before { content: normal; }");
  const RenderTheme theme2 = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css2, QStringLiteral("normal"), QString()));
  const PseudoElementRule* r2 = nullptr;
  for (const PseudoElementRule& r : theme2.decorations().pseudos) {
    if (r.host == QStringLiteral("h1") && r.pseudo == QStringLiteral("before")) { r2 = &r; break; }
  }
  require(r2 != nullptr, QStringLiteral("h1::before rule should be extracted"));
  require(r2->content.isEmpty(), QStringLiteral("content:normal must suppress the pseudo"));
}

void testBrowserInlinePseudoGeometryAndEditing() {
  const auto reference = QJsonDocument::fromJson(readFixture(
      QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/inline-pseudos-browser.json")).toUtf8()).object();
  const auto aliases = browserLayoutFont(reference);
  const auto family = aliases.value(QStringLiteral("muffinfixturesans"));
  for (const auto value : reference["cases"].toArray()) {
    const auto entry = value.toObject(), expected = entry["expected"].toObject();
    const auto id = entry["id"].toString();
    auto css = entry["css"].toString(); css.replace("MuffinFixtureSans", family);
    for (const int zoom : {100, 125, 200}) {
      const qreal scale = zoom / 100.0, width = entry["width"].toDouble() * scale;
      const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, id, {}), zoom);
      DocumentSession session;
      session.setMarkdownText("#### " + entry["text"].toString() + '\n', false);
      DocumentLayout layout;
      layout.rebuild(session.document(), theme, width);
      const auto nodeId = session.document().root().children()[0]->id();
      const auto* text = layout.block(nodeId)->inlineLayout();
      const auto context = id + QString(" zoom %1").arg(zoom);
      const auto intrinsic = text->intrinsicWidths();
      require(qAbs(intrinsic.first - expected["minContent"].toDouble()*scale) < 1.1,
              context + QString(" browser min-content actual=%1 expected=%2").arg(intrinsic.first).arg(expected["minContent"].toDouble()*scale));
      require(qAbs(intrinsic.second - expected["maxContent"].toDouble()*scale) < 1.1,
              context + QString(" browser max-content actual=%1 expected=%2").arg(intrinsic.second).arg(expected["maxContent"].toDouble()*scale));
      require(qAbs(text->height() - expected["height"].toDouble() * scale) < .7,
              context + QString(" browser height actual=%1 expected=%2").arg(text->height()).arg(expected["height"].toDouble() * scale));
      const auto characters = expected["characters"].toArray();
      require(text->visibleText() == entry["text"].toString(), context + " generated text is not editable/copied");
      for (int i = 0; i < characters.size(); ++i) {
        if (text->visibleText()[i].isSpace()) continue;
        const auto caret = text->cursorRect(i);
        const auto character = characters[i].toObject();
        require(qAbs(caret.left() - character["x"].toDouble() * scale) < 1.1,
                context + QString(" browser character %1 x actual=%2 expected=%3").arg(i).arg(caret.left()).arg(character["x"].toDouble() * scale));
        require(qAbs(caret.top() - character["y"].toDouble() * scale) < 1.1,
                context + QString(" browser baseline position %1 y actual=%2 expected=%3").arg(i).arg(caret.top()).arg(character["y"].toDouble() * scale));
        require(text->hitTestTextOffset({caret.left(), caret.center().y()}) == i, context + " click/caret roundtrip");
        require(text->cursorRectForSourceOffset(i) == caret, context + " source/visible caret agree");
      }
      require(text->selectionRects(0, text->visibleText().size()) ==
                  text->selectionRectsForSourceOffsets(0, text->visibleText().size()), context + " selection excludes generated text");
      if (id.startsWith("marker-") || id == "generated-text")
        require(text->hitTestCursorRect({0, text->allocatedLineRect(0).center().y()}) == text->cursorRect(0),
                context + " generated prefix click snaps to the first source caret");
      const auto compare = [&] {
        const auto* current = layout.block(session.document().root().children()[0]->id());
        for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
          DocumentLayout fresh;
          fresh.rebuild(session.document(), theme, width, {}, {}, policy);
          fresh.buildAll(theme);
          const auto* other = fresh.block(current->nodeId());
          require(current->rect() == other->rect(), context + " full/lazy/incremental box");
          const auto* a = current->inlineLayout(); const auto* b = other->inlineLayout();
          require(a->selectionRects(0, a->visibleText().size()) == b->selectionRects(0, b->visibleText().size()),
                  context + " full/lazy/incremental selection");
          for (int i=0; i <= a->visibleText().size(); ++i)
            require(a->cursorRect(i) == b->cursorRect(i), context + " full/lazy/incremental caret");
          const auto raster = [&](const BlockLayout* block) {
            QImage image(qCeil(block->rect().width()+20), qCeil(block->height()+20), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image); painter.translate(-block->rect().topLeft());
            block->paint(painter, theme, 0, nullptr);
            return image;
          };
          raster(current); raster(other);
          require(raster(current) == raster(other), context + " full/lazy/incremental pixels");
        }
      };
      compare();
      require(session.applyTextDelta(5, 0, "Edited ", true, {{nodeId, 5, BlockType::Heading}}), context + " edit");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (range.isValid()) layout.rebuildTopLevelRange(range, session.document(), theme, {});
      else layout.rebuildBlock(nodeId, session.document(), theme, {});
      compare();
    }
  }
}

void testPseudoStructuralCascadeAndCache() {
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write > h4:first-child::before{content:'first';color:red}"
      "h4{font-size:20px}h4:nth-of-type(2)::after{content:'last';font-size:50%}", "live-pseudos", {}));
  DocumentSession session;
  session.setMarkdownText("#### One\n\n#### Two\n", false);
  const auto& nodes = session.document().root().children();
  const auto first = theme.pseudoForNode(*nodes[0], "before"), second = theme.pseudoForNode(*nodes[1], "before");
  require(first && first->content == "first" && (!second || second->content.isEmpty()), "pseudo selectors use live host topology");
  const auto after = theme.pseudoForNode(*nodes[1], "after");
  require(after && after->fontSizePx == 10, "pseudo percentage font-size inherits from the originating heading");
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 300);
  const auto* old = layout.block(nodes[0]->id())->inlineLayout();
  require(old->stylesMatch(theme, *nodes[0]), "unchanged pseudo style can reuse layout");
  const auto originalHeading = nodes[0]->id();
  require(session.applyTextDelta(0, 0, "Prose\n\n", true, {{originalHeading, 0, BlockType::Heading}}), "insert sibling before generated heading");
  const auto range = session.lastLocalTopLevelRangeChange();
  if (range.isValid()) layout.rebuildTopLevelRange(range, session.document(), theme, {});
  else layout.rebuild(session.document(), theme, 300);
  DocumentLayout fresh;
  fresh.rebuild(session.document(), theme, 300);
  const auto* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  const auto* rebuilt = layout.block(heading->id())->inlineLayout();
  require(rebuilt->cursorRect(0).left() == 0, "structural edit removes obsolete generated prefix geometry");
  require(rebuilt->cursorRect(0) == fresh.block(heading->id())->inlineLayout()->cursorRect(0),
          "structural pseudo invalidation agrees with a fresh layout");
}

void testSyntheticBoldPreservesCssAdvances() {
  // Only register the bundled light face: requesting bold must synthesize ink
  // instead of silently testing a real bold face installed by another fixture.
  const auto id = QFontDatabase::addApplicationFont(QStringLiteral(
      MUFFIN_BINARY_DIR "/theme-fonts/pixyll/lato-v14-latin-300.ttf"));
  const auto families = QFontDatabase::applicationFontFamilies(id);
  require(id >= 0 && !families.isEmpty(), "load the original single-face typography fixture");
  QFont normal(families.front()); normal.setPointSizeF(21.6); normal.setWeight(QFont::Light);
  QFont bold = normal; bold.setWeight(QFont::Bold);
  font_rendering::configureCssFont(normal, 1.1, 2);
  font_rendering::configureCssFont(bold, 1.1, 2);
  const QString sample = QStringLiteral("Muffin Markdown Example");
#if defined(Q_OS_WIN)
  require(qAbs(QFontMetricsF(normal).horizontalAdvance(sample) - QFontMetricsF(bold).horizontalAdvance(sample)) < .6,
          "synthetic emboldening preserves original advances including spaces");
  auto unspacedNormal = normal, unspacedBold = bold;
  font_rendering::configureCssFont(unspacedNormal, 0, 0);
  font_rendering::configureCssFont(unspacedBold, 0, 0);
  require(qAbs(QFontMetricsF(unspacedNormal).horizontalAdvance("office efficient") -
               QFontMetricsF(unspacedBold).horizontalAdvance("office efficient")) < .6,
          "synthetic metric correction preserves normal ligature shaping");
#endif
  require(bold.weight() == QFont::Bold, "metric correction keeps bold ink");
  const auto configured = bold;
  font_rendering::configureCssFont(bold, 1.1, 2);
  require(bold == configured, "CSS metric configuration is idempotent");
  // A real bold face keeps its authored spacing and independent design metrics.
  QFont real(QStringLiteral("Open Sans")); real.setPointSizeF(12); real.setWeight(QFont::Bold);
  const auto os2 = QRawFont::fromFont(real).fontTable("OS/2");
  if (os2.size() >= 6 && qFromBigEndian<quint16>(os2.constData()+4) >= 600) {
    font_rendering::configureCssFont(real, 1.25, 2.5);
    require(real.letterSpacing() == 1.25 && real.wordSpacing() == 2.5, "real bold faces retain authored CSS spacing");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
  const auto reference = QJsonDocument::fromJson(readFixture(
      QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/inline-pseudos-browser.json")).toUtf8()).object();
  kBase = QStringLiteral("#write { color:#000000; font-family:'%1'; }")
              .arg(browserLayoutFont(reference).value("muffinfixturesans"));
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testAbsoluteBeforeBarAtLeftEdge);
  RUN_TEST(testInlineBeforeDiscShiftsText);
  RUN_TEST(testInlineBeforeDashShiftsText);
  RUN_TEST(testInlineBeforeHollowRingShiftsText);
  RUN_TEST(testAfterIconPaintsRightOfText);
  RUN_TEST(testMaskDoesNotPaintAnUnmaskedRectangle);
  RUN_TEST(testInlinePseudoParticipatesInWrappingAndCaretMapping);
  RUN_TEST(testAbsolutePseudoUsesHostGeometryAndZoom);
  RUN_TEST(testMaskImageHasItsOwnSize);
  RUN_TEST(testShadowCoverageIgnoresFloatingPointNoise);
  RUN_TEST(testContentNoneIsNotLiteralText);
  RUN_TEST(testBrowserInlinePseudoGeometryAndEditing);
  RUN_TEST(testPseudoStructuralCascadeAndCache);
  RUN_TEST(testSyntheticBoldPreservesCssAdvances);
#undef RUN_TEST
  return 0;
}
