#include "document/DocumentSession.h"
#include "diagnostics/ThemePerformance.h"
#include "document/MarkdownDocument.h"
#include "render/CssPaintAnimation.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "render/Blur.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssValueParser.h"
#include "../TestUtils.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <cmath>

using namespace muffin;
namespace {
bool approximately(qreal a, qreal b, qreal tolerance = .01) {
  return qAbs(a - b) < tolerance;
}
std::shared_ptr<const CssPaintRecipe> recipe(const QString& css, const QString& tag = "em") {
  const auto sheet = CssThemeParser::parse(css, {});
  CssComputedStyleEngine engine(sheet);
  CssElement element;
  element.tag = tag;
  return compileCssPaintRecipe(
      tag,
      [&](CssElementState state) {
        state.subject = &element;
        return engine.styleFor(element, state);
      },
      sheet);
}
ThemeElementStyle projected(const QString& css) {
  CssElement element;
  element.tag = "em";
  const auto sheet = CssThemeParser::parse(css, {});
  return CssThemeMapper::projectComputedStyle("em", CssComputedStyleEngine(sheet).styleFor(element));
}
QImage boxImage(const ThemeElementStyle& style, QRectF rect = {10, 10, 84, 30}) {
  QImage image(110, 60, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QFont font;
  font.setPointSizeF(12);
  const auto box = LayoutBox::place("em", style, {}, rect, font);
  QPainter painter(&image);
  paintLayoutBox(painter, box);
  return image;
}
void testBackgroundUsedValues() {
  const auto style = projected("em{background-image:linear-gradient(90deg,red 50%,transparent 50%);"
                               "background-repeat:repeat-x;background-position:0 100%;background-size:6px 3px}");
  const auto image = boxImage(style);
  for (int y = 10; y < 37; ++y)
    require(image.pixelColor(20, y) == QColor(Qt::white), "stitches must occupy only the bottom three pixels");
  int red = 0;
  for (int x = 10; x < 94; ++x)
    if (image.pixelColor(x, 38).red() > 240 && image.pixelColor(x, 38).green() < 20)
      ++red;
  require(red >= 40 && red <= 44, "repeat-x must retain a six pixel period, including transparent gaps");
  const auto zero = projected("em{background:linear-gradient(red,red) 0 bottom / 0 3px no-repeat}");
  require(boxImage(zero).pixelColor(30, 38) == QColor(Qt::white), "zero background dimensions must paint no image");
  const auto shorthand = projected("em{background-position-x:99px;background-size:9px 9px;"
                                   "background:linear-gradient(red,red) right bottom / 10px 4px no-repeat}");
  const auto positioned = boxImage(shorthand);
  require(positioned.pixelColor(92, 38) == QColor(Qt::red) && positioned.pixelColor(80, 38) == QColor(Qt::white),
          "background shorthand must reset old axes, parse its position/size and avoid repetition");
  const auto edge = parseCssPosition("right 4px bottom 2px");
  require(approximately(edge.first.used(50), 46) && approximately(edge.second.used(20), 18),
          "edge offsets must remain relative to the available space");
}
void testPerPropertyTransitionsAndReversal() {
  const auto motion = recipe(
      "em{color:black;background-position:0 0;transition:color 100ms linear,background-position 400ms linear 100ms}"
      "em:hover{color:white;background-position:100% 0}");
  require(bool(motion), "interactive element must compile a recipe");
  CssPaintContext context;
  context.timeMs = 0;
  context.sample(*motion, "one", 0);
  context.sample(*motion, "one", 1);
  context.timeMs = 50;
  auto mid = context.sample(*motion, "one", 1);
  require(mid.paint.color.red() >= 126 && mid.paint.color.red() <= 129, "color should have its own duration");
  require(approximately(mid.paint.backgroundLayers.front().positionX.fraction, 0),
          "background transition should honor its own delay");
  const auto reversed = context.sample(*motion, "one", 0);
  require(reversed.paint.color == mid.paint.color, "interrupting a transition must start from its current value");
  context.timeMs = 100;
  const auto returning = context.sample(*motion, "one", 0);
  require(returning.paint.color.red() >= 62 && returning.paint.color.red() <= 66,
          "outgoing transition must continue smoothly");
  context.timeMs = 1000;
  context.needsFrame = false;
  context.sample(*motion, "one", 0);
  require(!context.needsFrame, "completed transitions must stop requesting frames");
  context.timeMs = 1100;
  context.sample(*motion, "one", 1);
  context.timeMs = 1300;
  mid = context.sample(*motion, "one", 1);
  require(approximately(mid.paint.backgroundLayers.front().positionX.fraction, .25),
          "position should advance independently after its delay");
  require(approximately(cssTimingProgress(.5, "ease"), .8024, .001),
          "ease must solve CSS cubic-bezier rather than approximate it with a power curve");
  require(approximately(cssTimingProgress(.4, "steps(4,end)"), .25), "step timing must use discrete intervals");
}
void testKeyframesAndIdle() {
  const auto motion = recipe("@keyframes stitchFlow{from{background-position-x:0}to{background-position-x:6px}}"
                             "em{background-position:0 100%}em:hover{animation:stitchFlow .5s linear infinite}");
  CssPaintContext context;
  context.timeMs = 0;
  context.sample(*motion, "stitch", 1);
  context.timeMs = 125;
  context.needsFrame = false;
  const auto frame = context.sample(*motion, "stitch", 1);
  require(approximately(frame.paint.backgroundLayers.front().positionX.px, 1.5) &&
              approximately(frame.paint.backgroundLayers.front().positionY.fraction, 1),
          "keyframes should animate one background axis without destroying the other");
  require(context.needsFrame, "visible infinite animations should request another frame");
  context.needsFrame = false;
  context.sample(*motion, "stitch", 0);
  require(!context.needsFrame, "leaving hover must remove its animation and let the driver become idle");
  const auto finite = recipe("@keyframes fade{from{opacity:1}to{opacity:0}}em{animation:fade 200ms linear 2 forwards}");
  context.clear();
  context.timeMs = 0;
  context.sample(*finite, "fade", 0);
  context.timeMs = 100;
  require(approximately(context.sample(*finite, "fade", 0).paint.opacity, .5),
          "finite animations must play rather than being filtered out");
  context.timeMs = 450;
  context.needsFrame = false;
  require(approximately(context.sample(*finite, "fade", 0).paint.opacity, 0) && !context.needsFrame,
          "forwards fill should retain the final finite value without a running timer");
}
void testPausedAnimationAndSparseTracks() {
  const auto motion = recipe("@keyframes wave{0%{opacity:0}50%{background-color:red}100%{opacity:1}}"
                             "em{animation:wave 1s linear infinite}em:hover{animation-play-state:paused}");
  CssPaintContext context;
  context.timeMs = 0;
  context.sample(*motion, "wave", 0);
  context.timeMs = 250;
  require(approximately(context.sample(*motion, "wave", 0).paint.opacity, .25),
          "properties omitted at a keyframe must use their own track");
  context.sample(*motion, "wave", 1);
  context.timeMs = 1000;
  context.needsFrame = false;
  require(approximately(context.sample(*motion, "wave", 1).paint.opacity, .25) && !context.needsFrame,
          "paused animation must freeze without driving repaint");
  context.sample(*motion, "wave", 0);
  context.timeMs = 1250;
  require(approximately(context.sample(*motion, "wave", 0).paint.opacity, .5),
          "resuming an animation must retain elapsed progress");
}
void testScopedSelectorState() {
  const auto sheet = CssThemeParser::parse("p{color:black}p:hover{color:red}em:hover + strong{background:blue}", {});
  CssComputedStyleEngine engine(sheet);
  CssElement p;
  p.tag = "p";
  CssElement em;
  em.tag = "em";
  em.parent = &p;
  CssElement strong;
  strong.tag = "strong";
  strong.parent = &p;
  strong.previousSibling = &em;
  CssElementState state;
  state.hover = true;
  state.subject = &strong;
  const auto css = engine.styleFor(strong, state);
  require(css.resolvedValue("color") == "red", "hovered ancestors must supply inherited values");
  require(css.resolvedValue("background-color").isEmpty(),
          "hovering a subject must not falsely hover its sibling in a selector");
}
void testInlineFragmentsAndPreciseHover() {
  DocumentSession session;
  session.setMarkdownText("*first* and *second*", false);
  const auto& node = *session.document().root().children().front();
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "p{font-size:16px;color:black}em{background:blue;padding:2px;box-decoration-break:clone}em:hover{background:red}",
      "test", {}));
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.styleNode = &node;
  layout.build(node.inlines(), theme, 400, theme.paragraphFont(), options);
  QVector<const LayoutBox*> emphasis;
  for (const auto& box : layout.inlineBoxes())
    if (box.hostKey == "em")
      emphasis.push_back(&box);
  require(emphasis.size() == 2, "separate emphasis elements must have separate semantic boxes");
  const auto first = emphasis[0]->borderBox.translated(10, 10), second = emphasis[1]->borderBox.translated(10, 10);
  CssPaintContext context;
  context.pointer = first.center();
  context.timeMs = 0;
  const auto render = [&] {
    QImage image(450, 90, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    layout.paint(painter, {10, 10}, 0, 0, &context, "paragraph");
    return image;
  };
  const auto image = render();
  require(image.pixelColor(qFloor(first.left() + 1), qFloor(first.top() + 1)) == QColor(Qt::red),
          "hover should paint the first inline element");
  require(image.pixelColor(qFloor(second.left() + 1), qFloor(second.top() + 1)) == QColor(Qt::blue),
          "hover must not affect the other emphasis on the same line");
  context.pointer.reset();
  context.timeMs = 20;
  const auto left = render();
  require(left.pixelColor(qFloor(first.left() + 1), qFloor(first.top() + 1)) == QColor(Qt::blue),
          "pointer leave must restore the base inline background");
  const auto cursor = layout.cursorRectForSourceOffset(3);
  render();
  require(layout.cursorRectForSourceOffset(3) == cursor,
          "paint state changes must never move the caret or reshape text");
}
void testNestedWrappedInlineBackgrounds() {
  DocumentSession session;
  session.setMarkdownText("*one **two** three four five six seven eight nine*", false);
  const auto& node = *session.document().root().children().front();
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "p{font-size:16px}em{background:yellow;padding:2px;box-decoration-break:clone}strong{background:red}", "test",
      {}));
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.styleNode = &node;
  layout.build(node.inlines(), theme, 120, theme.paragraphFont(), options);
  int emphasis = 0, strong = 0;
  for (const auto& box : layout.inlineBoxes()) {
    if (box.hostKey == "em") {
      ++emphasis;
      require(box.usedBox.padding.left() > 0 && box.usedBox.padding.right() > 0,
              "clone decoration must retain both edges on every wrapped fragment");
    }
    if (box.hostKey == "strong")
      ++strong;
  }
  require(emphasis >= 2 && strong >= 1, "nested inline backgrounds must retain outer and inner boxes across wrapping");
}
void testInlineCompositeAndInteractionStates() {
  DocumentSession session;
  session.setMarkdownText("*first **nested** end* and *second*", false);
  const auto& node = *session.document().root().children().front();
  const auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("p{font-size:16px;color:black}em{color:red;background:blue;padding:3px;opacity:.5}"
                              "strong{color:lime}em:hover{filter:grayscale(1);transform:translateY(6px)}"
                              "em:focus{background:yellow}em:active{background:lime}",
                              "test", {}));
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.styleNode = &node;
  layout.build(node.inlines(), theme, 500, theme.paragraphFont(), options);
  QVector<QRectF> emphasis;
  for (const auto& box : layout.inlineBoxes())
    if (box.hostKey == "em")
      emphasis << box.borderBox.translated(10, 10);
  require(emphasis.size() == 2, "composite fixture needs two independent emphasis elements");
  CssPaintContext context;
  context.timeMs = 0;
  const auto render = [&](qsizetype focused = -1) {
    QImage image(550, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    layout.paint(painter, {10, 10}, 0, 0, &context, "paragraph", focused);
    return image;
  };
  const auto initial = render();
  const auto padding = emphasis.front().topLeft() + QPointF(1, 1);
  const auto blue = initial.pixelColor(padding.toPoint());
  require(blue.blue() > 250 && blue.red() >= 126 && blue.red() <= 129,
          "inline opacity must composite the complete element exactly once");
  context.pointer = emphasis.front().center();
  const auto hovered = render();
  const auto gray = hovered.pixelColor((padding + QPointF(0, 6)).toPoint());
  require(qAbs(gray.red() - gray.green()) <= 1 && qAbs(gray.green() - gray.blue()) <= 1 && gray.red() < 240,
          "hover transform and grayscale must move and filter the inline background");
  int chromatic = 0;
  QRect chromaticBounds;
  const auto area = emphasis.front().translated(0, 6).toAlignedRect().intersected(hovered.rect());
  for (int y = area.top(); y <= area.bottom(); ++y)
    for (int x = area.left(); x <= area.right(); ++x) {
      const auto color = hovered.pixelColor(x, y);
      if (qAbs(color.red() - color.green()) > 2 || qAbs(color.green() - color.blue()) > 2) {
        ++chromatic;
        chromaticBounds = chromaticBounds.united(QRect(x, y, 1, 1));
      }
    }
  require(chromatic == 0,
          QString("inline grayscale must include its own and nested glyphs: %1 at %2,%3,%4,%5 (area %6,%7,%8,%9)")
              .arg(chromatic)
              .arg(chromaticBounds.x())
              .arg(chromaticBounds.y())
              .arg(chromaticBounds.width())
              .arg(chromaticBounds.height())
              .arg(area.x())
              .arg(area.y())
              .arg(area.width())
              .arg(area.height()));
  require(hovered.pixelColor((emphasis[1].topLeft() + QPointF(1, 1)).toPoint()) == blue,
          "compositing the hovered element must leave its sibling untouched");
  context.pointer.reset();
  const auto focused = render(3);
  const auto yellow = focused.pixelColor(padding.toPoint());
  require(yellow.red() > 250 && yellow.green() > 250 && yellow.blue() < 150,
          "caret focus must target its semantic inline element");
  context.pointer = emphasis[1].center();
  context.pressed = true;
  const auto pressed = render();
  const auto active = pressed.pixelColor((emphasis[1].topLeft() + QPointF(1, 7)).toPoint());
  require(qAbs(active.red() - active.green()) <= 1 && active.red() < 240,
          "active state must compose with hover on the pressed element");
}
QRect blueBounds(const QImage& image) {
  QRect result;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (color.blue() > 220 && color.red() < 40 && color.green() < 40)
        result = result.united(QRect(x, y, 1, 1));
    }
  return result;
}
void testHostAndPseudoPropertyTimelines() {
  DocumentSession session;
  session.setMarkdownText("# Heading", false);
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{margin:0;padding:0}h1{margin:0;padding:10px;position:relative;color:black;transition:color 100ms linear,transform 400ms linear}"
      "h1:hover{color:red;transform:translateY(-2px)}"
      "h1::after{content:'';position:absolute;left:50%;bottom:0;width:40px;height:4px;background:blue;transform:translateX(-50%);transition:width 800ms linear}"
      "h1:hover::after{width:100%}",
      "test", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 500);
  const auto* block = layout.block(session.document().root().children().front()->id());
  require(block, "heading should be laid out");
  CssPaintContext context;
  context.timeMs = 0;
  const auto render = [&] {
    QImage image(550, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    BlockLayout::BlockPaintState state;
    state.css = &context;
    block->paint(painter, theme, 0, nullptr, state);
    return image;
  };
  const auto initial = blueBounds(render());
  require(initial.width() >= 39 && initial.width() <= 41, "base positioned pseudo should retain its authored width");
  context.pointer = block->cssBorderBox().center();
  render();
  context.timeMs = 200;
  const auto mid = blueBounds(render());
  const qreal expected = 40 + (block->cssBoxGeometry().paddingBox.width() - 40) * .25;
  require(approximately(mid.width(), expected, 2),
          "pseudo width must use its own 800ms transition rather than the host duration");
  require(approximately(mid.center().x(), initial.center().x(), 1.1),
          QStringLiteral("percentage translation should preserve centering: initial %1,%2 mid %3,%4")
              .arg(initial.x())
              .arg(initial.width())
              .arg(mid.x())
              .arg(mid.width()));
  context.timeMs = 1000;
  context.needsFrame = false;
  render();
  require(!context.needsFrame, "host and pseudo timelines must both become idle after completion");
}
void testMotionShorthandCascadeAndKeyframeExpansion() {
  const auto motion =
      recipe("em{color:black;transition-duration:8s;transition:color 100ms linear}em:hover{color:white}");
  CssPaintContext context;
  context.timeMs = 0;
  context.sample(*motion, "one", 1);
  context.timeMs = 50;
  require(context.sample(*motion, "one", 1).paint.color.red() >= 126,
          "later transition shorthand must reset a previously authored duration longhand");
  const auto expanded =
      recipe("@keyframes slide{from{background-position:0% 0%}to{background-position:100% 100%}}"
             "em{background-position-x:10px;background-position-y:20px;animation:slide 1s linear forwards}");
  context.clear();
  context.timeMs = 0;
  context.sample(*expanded, "slide", 0);
  context.timeMs = 500;
  const auto mid = context.sample(*expanded, "slide", 0);
  require(approximately(mid.paint.backgroundLayers.front().positionX.fraction, .5) &&
              approximately(mid.paint.backgroundLayers.front().positionY.fraction, .5),
          "keyframe shorthand must expand both axes through the common declaration pipeline");
}
void testElementCompositingAndPointerCoordinates() {
  QImage image(100, 60, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(4, 2);
  ThemeElementPaintStyle style;
  style.opacity = .5;
  CssPaintContext context;
  context.pointer = QPointF(20, 20);
  paintCssElement(painter, {10, 10, 40, 30}, {10, 10, 40, 30}, style, 1, &context,
                  [&](QPainter& target, const ThemeElementPaintStyle& paint) {
                    require(paint.opacity == 1, "layer opacity must be applied once to the composite");
                    require(target.worldTransform().inverted().map(*context.pointer) == QPointF(16, 18),
                            "offscreen layers must retain semantic pointer coordinates");
                    target.fillRect(QRectF(10, 10, 40, 30), Qt::blue);
                    target.fillRect(QRectF(12, 12, 20, 20), Qt::red);
                  });
  require(context.pointer == QPointF(20, 20), "layer rendering must restore the view's pointer");
  const auto pixel = image.pixelColor(20, 20);
  require(pixel.red() > 250 && pixel.green() >= 126 && pixel.blue() >= 126,
          "opacity should composite the red content with the page, without leaking the blue background through it");
}
void testPseudoFilterAffectsGlyphs() {
  DocumentSession session;
  session.setMarkdownText("> quote", false);
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{padding:20px;margin:0}blockquote{position:relative;margin:0;padding:25px}"
      "blockquote::before{content:'A';position:absolute;top:0;left:0;color:red;font-size:20px;filter:grayscale(1);transition:filter 200ms linear}"
      "blockquote:hover::before{filter:grayscale(0)}",
      "test", {}));
  const auto* element = theme.cssElementForNode(*session.document().root().children().front());
  const auto motion = theme.paintRecipeForElement(*element, "before");
  require(motion && approximately(motion->states[0].paint.filterGrayscale, 1),
          "pseudo base filter must survive computed-style projection");
  require(approximately(motion->states[1].paint.filterGrayscale, 0), "pseudo hover filter must have its own state");
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 500);
  const auto* block = layout.block(session.document().root().children().front()->id());
  const auto pseudoRects = block->positionedPseudoRects();
  require(!pseudoRects.isEmpty(), "quote should expose a positioned pseudo glyph");
  const auto pseudoArea = pseudoRects.front().adjusted(-2, -2, 2, 2).toAlignedRect();
  CssPaintContext context;
  context.timeMs = 0;
  const auto redPixels = [&] {
    QImage image(550, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    BlockLayout::BlockPaintState state;
    state.css = &context;
    block->paint(painter, theme, 0, nullptr, state);
    painter.end();
    int red = 0;
    const auto area = pseudoArea.intersected(image.rect());
    for (int y = area.top(); y <= area.bottom(); ++y)
      for (int x = area.left(); x <= area.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() > color.green() + 40 && color.red() > color.blue() + 40)
          ++red;
      }
    return red;
  };
  require(redPixels() == 0, "pseudo filter should affect its glyphs as well as its background");
  context.pointer = block->cssBorderBox().center();
  redPixels();
  context.timeMs = 250;
  require(redPixels() > 10, "hover filter animation should restore the pseudo glyph's color");
}
void testFlowPseudoEffectsAndOrderedAnimation() {
  DocumentSession session;
  session.setMarkdownText("Body", false);
  QRectF pseudoBounds;
  const auto render = [&](const QString& effect) {
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write{margin:0;padding:10px}p{color:black}p::before{content:'MMMM';font-size:24px;color:red;background:blue;" +
            effect + "}",
        "flow-effects", {}));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 500);
    const auto* block = layout.block(session.document().root().children().front()->id());
    pseudoBounds = {};
    for (const auto& rect : block->inlineLayout()->generatedPseudoRects())
      pseudoBounds = pseudoBounds.united(rect.translated(block->inlineTextOrigin()));
    QImage image(500, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    layout.block(session.document().root().children().front()->id())->paint(painter, theme, 0);
    painter.end();
    return image;
  };
  const auto normal = render({}), gray = render("filter:grayscale(1)"),
             masked = render("mask-image:linear-gradient(transparent,transparent)");
  int red = 0, grayRed = 0, maskColor = 0;
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 500; ++x) {
      if (!pseudoBounds.contains(QPointF(x, y)))
        continue;
      const auto count = [&](const QImage& image) {
        const auto c = image.pixelColor(x, y);
        return c.red() > c.green() + 30 || c.blue() > c.green() + 30;
      };
      red += count(normal);
      grayRed += count(gray);
      maskColor += count(masked);
    }
  if (!(red > 100 && grayRed == 0 && maskColor == 0)) {
    std::fprintf(stderr, "pseudo effect pixels: normal=%d grayscale=%d mask=%d\n", red, grayRed, maskColor);
    normal.save("pseudo-normal.png");
    gray.save("pseudo-gray.png");
    masked.save("pseudo-mask.png");
  }
  require(red > 100 && grayRed == 0 && maskColor == 0,
          "normal-flow text pseudos composite glyphs/backgrounds through filters and masks");
  auto from = projected("em{filter:brightness(.5) contrast(2)}"), to = projected("em{filter:brightness(1)}"), mid = to;
  interpolateCssPaintProperty(mid, from, to, "filter", .5);
  require(mid.paint.filters.size() == 2 && approximately(mid.paint.filters[0].value, .75) &&
              approximately(mid.paint.filters[1].value, 1.5),
          "filter interpolation pads omitted tail functions with their identity");
  to = projected("em{filter:contrast(2) brightness(.5)}");
  interpolateCssPaintProperty(mid, from, to, "filter", .25);
  require(mid.paint.filters == from.paint.filters, "incompatible filter function ordering interpolates discretely");
}
void testScopedStateCacheAndFunctionalSelectors() {
  const auto sheet =
      CssThemeParser::parse("html{--ink:black}p:hover{--ink:red}strong{color:var(--ink)}"
                            "p:hover > strong{background:blue}strong:not(:hover){border-top-color:green}"
                            "strong:is(:hover,:focus){color:yellow}strong:where(:active){opacity:.5}"
                            "strong:hover + strong{outline-color:purple}strong:visited{text-decoration-color:orange}",
                            {});
  CssComputedStyleEngine engine(sheet);
  CssElement html;
  html.tag = "html";
  html.cacheId = nextCssElementIdentity();
  CssElement p;
  p.tag = "p";
  p.parent = &html;
  p.cacheId = nextCssElementIdentity();
  CssElement first;
  first.tag = "strong";
  first.parent = &p;
  first.cacheId = nextCssElementIdentity();
  CssElement second = first;
  second.cacheId = nextCssElementIdentity();
  second.previousSibling = &first;
  first.nextSibling = &second;
  CssElementState state;
  state.subject = &first;
  state.hover = true;
  const auto hovered = engine.styleFor(first, state);
  require(hovered.rawValue("color") == "yellow" && hovered.rawValue("background-color") == "blue",
          "functional and ancestor state selectors must match the actual hovered subject");
  const auto sibling = engine.styleFor(second, state);
  require(sibling.rawValue("color") == "red" && sibling.rawValue("border-top-color") == "green" &&
              sibling.rawValue("outline-color") == "purple",
          "a scoped sibling combinator must see the hovered sibling and inherit its hovered parent");
  state.subject = &second;
  require(engine.styleFor(second, state).rawValue("outline-color").isEmpty(), "a cached sibling state must not leak");
  state.hover = false;
  state.focus = true;
  require(engine.styleFor(second, state).rawValue("color") == "yellow" &&
              engine.styleFor(first, state).rawValue("color") == "black",
          "nested focus selectors and inherited custom properties must stay subject-specific");
  state.focus = false;
  state.active = true;
  require(engine.styleFor(second, state).rawValue("opacity") == ".5", "nested active selectors must remain dynamic");
  state.active = false;
  state.visited = true;
  require(engine.styleFor(second, state).rawValue("text-decoration-color") == "orange",
          "visited must have a distinct cache result");
  state.visited = false;
  state.hover = true;
  const auto expected = engine.styleFor(second, state);
  enableThemePerformance(true);
  resetThemePerformance();
  for (int i = 0; i < 50; ++i)
    require(engine.styleFor(second, state) == expected, "warm state queries must be stable");
  const auto work = themePerformanceSamples()[size_t(ThemePerfStage::Css)].work;
  enableThemePerformance(false);
  require(work == 0, "warm scoped states must reuse both ancestor and subject computed values");

  CssElement third = second;
  third.cacheId = nextCssElementIdentity();
  third.previousSibling = &second;
  state.subject = &third;
  enableThemePerformance(true);
  resetThemePerformance();
  engine.styleFor(third, state);
  const auto sharedWork = themePerformanceSamples()[size_t(ThemePerfStage::Css)].work;
  enableThemePerformance(false);
  require(sharedWork <= 1, "changing the subject must share equivalent ancestor states");
  engine.clearCache();
  require(engine.styleFor(second, CssElementState{}).rawValue("color") == "black",
          "clearing snapshots must restore base state");
  CssElement temporary;
  temporary.tag = "strong";
  temporary.parent = &p;
  const auto plain = engine.styleFor(temporary);
  temporary.inlineDeclarations.push_back({"color", "purple"});
  require(plain.rawValue("color") == "black" && engine.styleFor(temporary).rawValue("color") == "purple",
          "temporary elements and caller declarations must bypass identity caches");
}

void testStructuralInteractionDependencies() {
  const auto sheet =
      CssThemeParser::parse(".panel:hover strong{color:red}strong.special:focus{color:blue}"
                            "strong[data-kind=hot]:active{color:green}strong:not(.excluded):not(:hover){opacity:.8}"
                            "strong:is(.special:focus,.hot:active){background:yellow}",
                            {});
  CssComputedStyleEngine engine(sheet);
  CssElement p;
  p.tag = "p";
  p.cacheId = nextCssElementIdentity();
  CssElement strong;
  strong.tag = "strong";
  strong.classes << "excluded";
  strong.parent = &p;
  strong.cacheId = nextCssElementIdentity();
  require(engine.interactiveStateMask(strong) == 0,
          "unrelated classes, ancestors and attributes must not create state work");
  strong.classes.clear();
  engine.clearCache();
  require(engine.interactiveStateMask(strong) == 1, "dynamic exclusions must survive structural candidate filtering");
  strong.classes << "special";
  strong.attributes.insert("data-kind", "hot");
  engine.clearCache();
  require(engine.interactiveStateMask(strong) == 7,
          "nested alternatives and attribute states must contribute their dependencies");
  const auto inheritedSheet = CssThemeParser::parse("p:hover{color:red}strong::before{content:'x'}", {});
  CssComputedStyleEngine inherited(inheritedSheet);
  CssElement pseudo = strong;
  pseudo.cacheId = nextCssElementIdentity();
  pseudo.pseudoElement = "before";
  pseudo.originatingElement = &strong;
  require(inherited.interactiveStateMask(pseudo) == 1, "pseudos must include inherited ancestor state dependencies");
  CssElementState hover;
  hover.hover = true;
  hover.subject = &strong;
  require(inherited.styleFor(pseudo, hover).rawValue("color") == "red",
          "pseudo inherited state must use the host identity");
  const auto responsiveSheet =
      CssThemeParser::parse("strong:hover{color:red}@media(min-width:700px){strong:focus{color:blue}}", {});
  CssEnvironment environment;
  environment.viewportWidth = 600;
  CssComputedStyleEngine responsive(responsiveSheet, environment);
  require(responsive.interactiveStateMask(strong) == 1, "inactive media rules must not contribute dependencies");
  environment.viewportWidth = 900;
  require(responsive.withEnvironment(environment)->interactiveStateMask(strong) == 3 &&
              responsive.interactiveStateMask(strong) == 1,
          "new media snapshots must isolate dependency caches");
}

void testReducedRecipeStateCombinations() {
  const QStringList examples{"em:hover{color:red}",
                             "em:focus{color:red}",
                             "em:active{opacity:.5}",
                             "em:hover:focus{color:red}em:active{opacity:.5}",
                             "em:not(:hover){color:red}",
                             "em:is(:hover,:focus){color:red}",
                             "@keyframes pulse{to{opacity:.5}}em{animation:pulse 1s linear infinite}"};
  for (const auto& css : examples) {
    const auto sheet = CssThemeParser::parse(css, {});
    CssComputedStyleEngine engine(sheet);
    CssElement element;
    element.tag = "em";
    element.cacheId = nextCssElementIdentity();
    auto resolve = [&](CssElementState state) {
      state.subject = &element;
      return engine.styleFor(element, state);
    };
    const auto full = compileCssPaintRecipe("em", resolve, sheet);
    const auto reduced = compileCssPaintRecipe("em", resolve, sheet, false, engine.interactiveStateMask(element));
    require(full && reduced && full->fingerprint == reduced->fingerprint,
            "state pruning must preserve recipe identity");
    for (int bits = 0; bits < 8; ++bits) {
      require(full->states[bits].paint.color == reduced->states[bits].paint.color &&
                  full->states[bits].paint.opacity == reduced->states[bits].paint.opacity &&
                  full->animations[bits].size() == reduced->animations[bits].size(),
              "all eight runtime states must retain the same paint and animation values");
    }
  }
}
void testUnchangedHostColorUsesShapedFormats() {
  DocumentSession session;
  const QString markdown =
      QStringLiteral("Plain 中文 **bold** *italic* ~~strike~~ `code` and [Muffin](https://example.com). ").repeated(3);
  session.setMarkdownText(markdown, false);
  const auto& node = *session.document().root().children().front();
  const auto theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss("p{font-size:18px;color:#245678}p:hover{color:#c82438}"
                                                          "a{color:#109040}code{color:#8040b0}del{color:#603020}",
                                                          "paint-delta", {}));
  auto host = *theme.elementStyleForNode(node, "p");
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.styleNode = &node;
  options.baseTextColor = host.paint.color;
  options.hoverTextColor = QColor("#c82438");
  layout.build(node.inlines(), theme, 420, theme.paragraphFont(), options);
  const auto caret = layout.cursorRectForSourceOffset(12);
  const auto render = [&](qreal hover, const ThemeElementStyle* sampled) {
    QImage image(460, 260, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    layout.paint(painter, {10, 10}, hover, 0, nullptr, "paragraph", -1, sampled);
    return image;
  };
  const auto reference = render(0, nullptr);
  enableThemePerformance(true);
  resetThemePerformance();
  const auto idle = render(0, &host);
  const auto work = themeTextPaintStats();
  enableThemePerformance(false);
  require(work.calls > 0 && work.selectionRanges == 0,
          "unchanged host color must reuse shaped formats without dynamic foreground passes");
  require(idle == reference, "idle host sampling must retain exact static text pixels");
  for (const qreal phase : {.25, .5, 1.0}) {
    const auto from = options.baseTextColor, to = options.hoverTextColor;
    host.paint.color = QColor(qRound(from.red() + (to.red() - from.red()) * phase),
                              qRound(from.green() + (to.green() - from.green()) * phase),
                              qRound(from.blue() + (to.blue() - from.blue()) * phase));
    const auto expected = render(phase, nullptr);
    require(render(0, &host) == expected && expected != idle,
            "actual host animation must retain legacy recoloring, explicit inline colors and wrapping");
  }
  host.paint.color = options.baseTextColor;
  require(render(0, &host) == idle && layout.cursorRectForSourceOffset(12) == caret,
          "returning to the base color must restore exact pixels without changing caret geometry");
}

void checkSparseTextShadowMatchesFullCanvas(bool outlined) {
  QFont font("Arial");
  font.setPointSizeF(16);
  font.setItalic(true);
  TextLayout text("Glow text on two lines with enough words to wrap around", font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  text.setTextOption(option);
  QTextCharFormat decoration;
  decoration.setForeground(QColor(30, 80, 130, 180));
  decoration.setFontUnderline(true);
  if (outlined)
    decoration.setTextOutline(QPen(QColor(40, 20, 60, 180), 1));
  text.setFormats({{0, int(text.text().size()), decoration}});
  text.beginLayout();
  qreal y = 0;
  for (auto line = text.createLine(); line.isValid(); line = text.createLine()) {
    line.setLineWidth(180);
    line.setPosition({0, y});
    y += 66;
  }
  text.endLayout();
  QVector<QRectF> clips;
  for (int i = 0; i < text.lineCount(); ++i) {
    const auto line = text.lineAt(i);
    clips.append(QRectF(line.x() + 4, line.y(), qMin<qreal>(80, line.naturalTextWidth()), line.height()));
  }
  TextShadowPainter shadows;
  const auto equivalent = [outlined](const QImage& a, const QImage& b) {
    if (a == b)
      return true;
    if (a.size() != b.size())
      return false;
    int maximum = 0, different = 0;
    QRect differences;
    // Qt's path stroker can differ by one premultiplied component after an
    // integer translation. Unoutlined glyphs still require exact equality.
    for (int y = 0; y < a.height(); ++y)
      for (int x = 0; x < a.width(); ++x) {
        const auto left = a.pixel(x, y), right = b.pixel(x, y);
        if (left != right) {
          ++different;
          differences = differences.united(QRect(x, y, 1, 1));
        }
        maximum = qMax(maximum, qMax(qMax(qAbs(qAlpha(left) - qAlpha(right)), qAbs(qRed(left) - qRed(right))),
                                     qMax(qAbs(qGreen(left) - qGreen(right)), qAbs(qBlue(left) - qBlue(right)))));
      }
    if (outlined && maximum <= 1)
      return true;
    a.save("resource-pixels-text-shadow-actual.png");
    b.save("resource-pixels-text-shadow-expected.png");
    qWarning() << "Shadow comparison differing pixels/max component/extent" << different << maximum << differences;
    return false;
  };
  const auto render = [&](bool reference, qreal dpr, QVector<QRectF> rects, TextShadow shadow,
                          const QList<TextLayout::FormatRange>& overrides, QPointF origin = QPointF(60, 40),
                          QRectF painterClip = {}, int pad = 48) {
    QImage image(QSize(400, 350) * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter target(&image);
    if (!painterClip.isEmpty())
      target.setClipRect(painterClip);
    if (!reference)
      shadows.paint(target, text, origin, shadow, 1, rects, overrides);
    else {
      // Independent full-surface oracle: no sparse bounds or cache, generous
      // transparent margins, same convolution and source clipping semantics.
      QImage mask(QSize(350, 350) * dpr, QImage::Format_ARGB32_Premultiplied);
      mask.setDevicePixelRatio(dpr);
      mask.fill(Qt::transparent);
      {
        QPainter source(&mask);
        source.setRenderHint(QPainter::Antialiasing);
        source.setRenderHint(QPainter::TextAntialiasing);
        source.translate(pad, pad);
        if (overrides.isEmpty())
          text.draw(&source, {});
        else {
          // Independent full-canvas reference with overrides resolved as
          // character formats. Qt selection painting itself clips the final
          // decoration to boundingRect(), which is not the CSS text mask.
          TextLayout formatted(text.text(), text.font(), text.backend());
          formatted.setTextOption(text.textOption());
          auto formats = text.formats();
          formats.append(overrides);
          formatted.setFormats(formats);
          formatted.beginLayout();
          for (int i = 0; i < text.lineCount(); ++i) {
            auto line = formatted.createLine();
            require(line.isValid(), "paint-only mask formats retain lines");
            line.setLineWidth(text.lineAt(i).width());
            line.setPosition(text.lineAt(i).position());
            require(line.textStart() == text.lineAt(i).textStart() && line.textLength() == text.lineAt(i).textLength(),
                    "paint-only mask formats retain shaping and source coordinates");
          }
          formatted.endLayout();
          formatted.draw(&source, {});
        }
      }
      if (!rects.isEmpty()) {
        QImage coverage(mask.size(), QImage::Format_ARGB32_Premultiplied);
        coverage.fill(Qt::transparent);
        {
          QPainter clipPainter(&coverage);
          clipPainter.setRenderHint(QPainter::Antialiasing);
          QPainterPath clip;
          for (auto rect : rects)
            clip.addRect(rect.adjusted(-1, 0, 1, 0).translated(pad, pad));
          clipPainter.fillPath(QTransform::fromScale(dpr, dpr).map(clip), Qt::white);
        }
        coverage.setDevicePixelRatio(dpr);
        QPainter clipPainter(&mask);
        clipPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        clipPainter.drawImage(QPointF(), coverage);
      }
      if (!overrides.isEmpty())
        mask.save(QString("resource-pixels-oracle-%1-mask.png").arg(pad));
      QImage glow(mask.size(), QImage::Format_ARGB32_Premultiplied);
      glow.setDevicePixelRatio(dpr);
      glow.fill(Qt::transparent);
      {
        QPainter tint(&glow);
        tint.fillRect(QRectF({}, QSizeF(glow.size()) / dpr), shadow.color);
        tint.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        tint.drawImage(QPointF(), mask);
      }
      if (!overrides.isEmpty())
        glow.save(QString("resource-pixels-oracle-%1-tint.png").arg(pad));
      if (shadow.blur > 0)
        boxBlur(glow, qMax(1, qRound(shadow.blur * dpr)));
      // Anchor once on the device grid, independent of canvas padding. This
      // avoids Qt's asymmetric half-pixel image rounding on either side of 0.
      const auto position = (origin + shadow.offset) * dpr;
      const QPointF anchor(std::floor(position.x() + .5) / dpr, std::floor(position.y() + .5) / dpr);
      target.drawImage(anchor - QPointF(pad, pad), glow);
    }
    return image;
  };
  for (qreal dpr : {1., 1.5, 2.})
    for (qreal blur : {0., 2., 5.})
      for (bool host : {false, true}) {
        TextShadow shadow;
        shadow.present = true;
        shadow.color = QColor(0, 190, 230, 120);
        shadow.offset = {3.5, -2.25};
        shadow.blur = blur;
        const auto rects = host ? QVector<QRectF>{} : clips;
        const auto expected = render(true, dpr, rects, shadow, {});
        const auto actual = render(false, dpr, rects, shadow, {});
        if (!equivalent(actual, expected)) {
          actual.save("resource-pixels-text-shadow-actual.png");
          expected.save("resource-pixels-text-shadow-expected.png");
          int maximum = 0, different = 0;
          for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
              const auto a = actual.pixel(x, y), b = expected.pixel(x, y);
              if (a != b)
                ++different;
              maximum = qMax(maximum, qMax(qMax(qAbs(qAlpha(a) - qAlpha(b)), qAbs(qRed(a) - qRed(b))),
                                           qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b)))));
            }
          qWarning() << "Text shadow differing pixels/component delta" << different << maximum << "font"
                     << QRawFont::fromFont(font).familyName();
        }
        require(equivalent(actual, expected),
                QString("sparse glow preserves full-canvas pixels: dpr=%1 blur=%2 host=%3 outlined=%4")
                    .arg(dpr)
                    .arg(blur)
                    .arg(host)
                    .arg(outlined));
        resetTextShadowRasterStats();
        require(render(false, dpr, rects, shadow, {}) == actual && textShadowRasterStats().hits > 0 &&
                    textShadowRasterStats().rasterizedPixels == 0,
                "static glow reuses raster while retaining exact pixels");
      }
  TextShadow shadow;
  shadow.present = true;
  shadow.color = QColor(210, 30, 70, 180);
  shadow.blur = 3;
  QTextCharFormat hidden;
  hidden.setForeground(Qt::transparent);
  hidden.setFontUnderline(false);
  const QList<TextLayout::FormatRange> overrides{{0, 5, hidden}};
  const auto oldTrace = qgetenv("MUFFIN_SHADOW_RASTER_TRACE");
  qputenv("MUFFIN_SHADOW_RASTER_TRACE", "1");
  const auto overrideActual = render(false, 1, clips, shadow, overrides);
  if (oldTrace.isNull())
    qunsetenv("MUFFIN_SHADOW_RASTER_TRACE");
  else
    qputenv("MUFFIN_SHADOW_RASTER_TRACE", oldTrace);
  const auto overrideExpected = render(true, 1, clips, shadow, overrides);
  if (!equivalent(overrideActual, overrideExpected)) {
    const auto maxAlphaDifference = [](const QImage& a, const QImage& b) {
      int delta = 0;
      for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
          delta = qMax(delta, qAbs(qAlpha(a.pixel(x, y)) - qAlpha(b.pixel(x, y))));
      return delta;
    };
    for (const int padding : {32, 64, 96}) {
      const auto alternate = render(true, 1, clips, shadow, overrides, {60, 40}, {}, padding);
      alternate.save(QString("resource-pixels-shadow-oracle-%1.png").arg(padding));
      qWarning() << "Full-canvas oracle padding/reference alpha delta/sparse alpha delta" << padding
                 << maxAlphaDifference(alternate, overrideExpected) << maxAlphaDifference(alternate, overrideActual);
    }
  }
  require(equivalent(overrideActual, overrideExpected), "foreground/alpha overrides isolate cached shadow masks");
  auto overlapping = clips;
  overlapping.append(clips.front().adjusted(20, 0, 20, 0));
  require(equivalent(render(false, 1, overlapping, shadow, {}), render(true, 1, overlapping, shadow, {})),
          "overlapping source clips blur once without doubling translucent glow");
  for (qreal dpr : {1., 1.5, 2.}) {
    shadow.offset = {3.5, -2.25};
    require(equivalent(render(false, dpr, {}, shadow, {}, {-10, 40}), render(true, dpr, {}, shadow, {}, {-10, 40})),
            "scrolling across the canvas origin retains full-canvas pixels");
    shadow.offset = {-210, 0};
    require(
        equivalent(render(false, dpr, clips, shadow, {}, {250, 40}), render(true, dpr, clips, shadow, {}, {250, 40})),
        "offset glow entering the viewport is not culled with its source");
  }
  const QVector<QRectF> hugeClip{{0, 0, 2000, 2000}};
  const QRectF dirty{90, 60, 100, 90};
  shadow.offset = {};
  const auto expectedClipped = render(true, 2, hugeClip, shadow, {}, {60, 40}, dirty);
  resetTextShadowRasterStats();
  require(equivalent(render(false, 2, hugeClip, shadow, {}, {60, 40}, dirty), expectedClipped) &&
              textShadowRasterStats().largestSurfacePixels < 100000,
          "large masks retain only the dirty area and complete convolution support");
  resetTextShadowRasterStats();
  render(false, 1, clips, shadow, {}, {2000, 2000});
  require(textShadowRasterStats().culled > 0 && textShadowRasterStats().misses == 0,
          "offscreen glow skips rasterization using its complete offset and halo");
}

void testSparseTextShadowMatchesFullCanvas() {
  checkSparseTextShadowMatchesFullCanvas(false);
  checkSparseTextShadowMatchesFullCanvas(true);
}

void testRightAlignedTextShadow() {
  QFont font("Arial");
  font.setPointSizeF(20);
  TextLayout text("Heading", font);
  QTextOption option;
  option.setAlignment(Qt::AlignRight);
  text.setTextOption(option);
  text.beginLayout();
  auto line = text.createLine();
  line.setLineWidth(835);
  line.setPosition({0, 0});
  text.endLayout();
  require(line.naturalTextRect().left() > 600 && line.naturalTextWidth() < 200,
          "fixture separates actual glyph position from intrinsic width");
  QImage expected(960, 120, QImage::Format_ARGB32_Premultiplied);
  expected.fill(Qt::transparent);
  {
    QPainter painter(&expected);
    painter.setRenderHint(QPainter::TextAntialiasing);
    text.draw(&painter, {41, 41});
  }
  TextShadow shadow;
  shadow.present = true;
  shadow.offset = {1, 1};
  shadow.color = Qt::black;
  QImage actual(expected.size(), expected.format());
  actual.fill(Qt::transparent);
  {
    QPainter painter(&actual);
    TextShadowPainter shadows;
    shadows.paint(painter, text, {40, 40}, shadow, 1);
  }
  require(actual == expected, "right-aligned host shadow must cover the glyph position, not just intrinsic width");
}

void testTextShadowLifetimeAndBudget() {
  const auto before = textShadowRasterStats().cacheBytes;
  {
    QVector<std::shared_ptr<TextShadowPainter>> owners;
    QFont font("Arial");
    font.setPointSizeF(14);
    TextLayout text("cached glow", font);
    text.beginLayout();
    auto line = text.createLine();
    line.setLineWidth(300);
    line.setPosition({0, 0});
    text.endLayout();
    QImage image(440, 260, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    TextShadow shadow;
    shadow.present = true;
    shadow.color = Qt::cyan;
    shadow.blur = 4;
    const QVector<QRectF> clip{{0, 0, 300, 120}};
    for (int i = 0; i < 120; ++i) {
      auto owner = std::make_shared<TextShadowPainter>();
      owner->paint(painter, text, {20, 20}, shadow, 1, clip);
      owners.append(owner);
    }
    require(textShadowRasterStats().cacheBytes <= 16 * 1024 * 1024 && textShadowRasterStats().cacheEntries <= 512,
            "all layouts share a bounded raster memory budget");
    resetTextShadowRasterStats();
    owners.back()->reset();
    owners.back()->paint(painter, text, {20, 20}, shadow, 1, clip);
    require(textShadowRasterStats().misses == 1, "layout reset retires its previous raster entries");
  }
  require(textShadowRasterStats().cacheBytes == before,
          "destroyed layouts immediately release their remaining rasters");
}

void testInlineShadowInvalidation() {
  InlineLayout retained;
  DocumentSession session;
  const QString markdown = "**glowing *nested* words** and **a second long glowing phrase**";
  const QString css = "p{font-family:Arial;font-size:18px;color:#345678}"
                      "strong{text-shadow:0 0 2px rgba(0,190,230,.6)}"
                      "strong:hover{text-shadow:3px -1px 4px rgba(230,30,70,.7);transition:text-shadow 100ms linear}";
  const auto check = [&](QString source, QString rules, qreal width, int zoom, bool markers) {
    session.setMarkdownText(source, false);
    const auto& node = *session.document().root().children().front();
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(rules, "shadow-invalidation", {}), zoom);
    InlineLayout::BuildOptions options;
    options.styleNode = &node;
    options.projectionState.revealMarkdownMarkers = markers;
    retained.build(node.inlines(), theme, width, theme.paragraphFont(), options);
    const auto render = [&](const InlineLayout& layout, CssPaintContext* context = nullptr) {
      QImage image(600, 500, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::white);
      QPainter painter(&image);
      layout.paint(painter, {40, 40}, 0, 0, context, "paragraph");
      return image;
    };
    resetTextShadowRasterStats();
    const auto cold = render(retained);
    require(textShadowRasterStats().misses > 0, "rebuilt projection must produce new shadow rasters");
    resetTextShadowRasterStats();
    require(render(retained) == cold && textShadowRasterStats().hits > 0 &&
                textShadowRasterStats().rasterizedPixels == 0,
            "retained inline shadow reuses static rasters");
    InlineLayout fresh;
    fresh.build(node.inlines(), theme, width, theme.paragraphFont(), options);
    require(render(fresh) == cold && retained.cursorRectForSourceOffset(8) == fresh.cursorRectForSourceOffset(8) &&
                retained.hitTestSourceOffset({40, 10}) == fresh.hitTestSourceOffset({40, 10}),
            "edit, font, theme, zoom, wrap and marker changes match a fresh layout and caret");
    CssPaintContext context;
    context.timeMs = 0;
    render(retained, &context);
    for (const auto& box : retained.inlineBoxes())
      if (box.hostKey == "strong") {
        context.pointer = box.borderBox.center() + QPointF(40, 40);
        break;
      }
    context.timeMs = 100;
    render(retained, &context);
    context.timeMs = 150;
    const auto animated = render(retained, &context);
    resetTextShadowRasterStats();
    require(render(retained, &context) == animated && textShadowRasterStats().rasterizedPixels == 0,
            "identical animation samples reuse their raster without stale pixels");
    context.timeMs = 200;
    require(render(retained, &context) != cold &&
                retained.cursorRectForSourceOffset(8) == fresh.cursorRectForSourceOffset(8),
            "hover glow remains visible and does not affect caret geometry");
    return cold;
  };
  const auto initial = check(markdown, css, 320, 100, false);
  require(check(markdown, css, 90, 100, false) != initial, "wrapping changes shadow placement");
  require(check(markdown, css, 320, 125, false) != initial, "zoom changes the shaped shadow source");
  require(check(markdown, css, 320, 100, true) != initial, "revealed Markdown changes the shaped shadow source");
  require(check("prefix " + markdown, css, 320, 100, false) != initial, "text edits change shadow placement");
  require(check(markdown, css + "strong{font-size:26px;text-shadow:1px 2px 3px magenta}", 320, 100, false) != initial,
          "theme replacement retires old fonts and glow colors");
  require(check(markdown, css, 320, 100, false) == initial, "switching back restores exact original glow");
}

void testCrossElementFinalStylesAndRecipes() {
  const auto sheet =
      CssThemeParser::parse("html{--ink:black}p:hover{--ink:red}strong{color:var(--ink)}strong:focus{color:blue}"
                            "strong:nth-child(2){background:yellow}strong[data-kind=hot]{border-color:green}",
                            {});
  CssComputedStyleEngine engine(sheet);
  CssElement html;
  html.tag = "html";
  html.cacheId = nextCssElementIdentity();
  CssElement parent;
  parent.tag = "p";
  parent.parent = &html;
  parent.cacheId = nextCssElementIdentity();
  CssElement first;
  first.tag = "strong";
  first.parent = &parent;
  first.cacheId = nextCssElementIdentity();
  std::array<CssComputedStyle, 8> expected;
  for (int bits = 0; bits < 8; ++bits) {
    CssElementState state;
    state.subject = &first;
    state.hover = bits & 1;
    state.focus = bits & 2;
    state.active = bits & 4;
    expected[bits] = engine.styleFor(first, state);
  }
  enableThemePerformance(true);
  resetThemePerformance();
  for (int i = 0; i < 80; ++i) {
    CssElement next = first;
    next.cacheId = nextCssElementIdentity();
    for (int bits = 0; bits < 8; ++bits) {
      CssElementState state;
      state.subject = &next;
      state.hover = bits & 1;
      state.focus = bits & 2;
      state.active = bits & 4;
      require(engine.styleFor(next, state) == expected[bits], "equivalent subjects share complete computed values");
    }
  }
  const auto work = themePerformanceSamples()[size_t(ThemePerfStage::Css)].work;
  enableThemePerformance(false);
  require(work == 0, "new element identities must not repeat equivalent final CSS calculations");
  CssElement different = first;
  different.cacheId = nextCssElementIdentity();
  different.childIndex = 1;
  different.attributes.insert("data-kind", "hot");
  require(engine.styleFor(different).rawValue("background-color") == "yellow" &&
              engine.styleFor(different).rawValue("border-top-color") == "green",
          "structural and attribute match differences must isolate final results");

  DocumentSession session;
  session.setMarkdownText("**one**\n\n**two**", false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("strong:hover{color:red;transition:color .2s}", "shared", {}));
  const auto* a = theme.cssInlineElement(*session.document().root().children()[0], 2, "strong");
  const auto* b = theme.cssInlineElement(*session.document().root().children()[1], 2, "strong");
  const auto left = theme.paintRecipeForElement(*a), right = theme.paintRecipeForElement(*b);
  require(left && left == right, "equivalent inline elements share one immutable paint recipe");
  CssPaintContext context;
  context.timeMs = 0;
  context.sample(*left, "first", 0);
  context.sample(*right, "second", 0);
  context.sample(*left, "first", 1);
  context.timeMs = 100;
  require(context.sample(*left, "first", 1).paint.color != context.sample(*right, "second", 0).paint.color,
          "shared recipe values must retain independent element timelines");
}

void testInlineStyleLifetimeAndThemeChanges() {
  const QString css = "p{color:#123456;text-decoration:underline}strong{font-size:21px}"
                      "strong:hover{color:red;transition:color .2s}span.hot{color:red}span.cold{color:blue}"
                      "@media(max-width:600px){strong{font-size:11px;color:green}}";
  const auto definition = CssThemeMapper::fromCss(css, "inline-lifetime", {});
  auto theme = RenderTheme::fromDefinition(definition);
  theme.updateForViewport(800, 600);
  DocumentSession session;
  session.setMarkdownText("plain **bold** and *italic*\n\n", false);
  theme.resolveDocumentCounters(session.document());
  const auto query = [&](qsizetype offset) {
    return theme.inlineStyleForNode(*session.document().root().children()[0], offset);
  };
  const auto initial = query(9);
  require(initial.text.fontSizePx == 21 && initial.text.decorationLines && initial.paintRecipe,
          "inline snapshot retains inherited decoration, typography and interactive recipe");
  require(query(10).fingerprint == initial.fingerprint && query(10).paintRecipe == initial.paintRecipe,
          "different offsets in the same inline retain complete values");
  theme.releaseInlineStyles({session.document().root().children()[0]->id()});
  require(query(9).fingerprint == initial.fingerprint,
          "retired inline views rebuild equivalent styles without stale identities");
  require(session.applyTextEdit(0, 0, "X") != muffin::TextEditResult::Rejected, "plain replacement applies");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange(), &session.document());
  require(theme.hasLocalStyleChange(), "text-only replacement preserves CSS topology");
  theme.resolveDocumentCounters(session.document());
  require(query(10).fingerprint == initial.fingerprint,
          "rebinding text-only replacement preserves the correct inline snapshot");
  theme.updateForViewport(500, 600);
  const auto narrow = query(10);
  require(narrow.text.fontSizePx == 11 && narrow.paint.color == QColor("green") &&
              narrow.fingerprint != initial.fingerprint,
          "media changes retire cached colors, font metrics and recipes");
  theme.updateForViewport(800, 600);
  require(query(10).fingerprint == initial.fingerprint, "returning to a media bucket restores the original style");

  session.setMarkdownText("<span class='hot'>value</span>\n", false);
  theme.invalidateDocumentStyles();
  theme.resolveDocumentCounters(session.document());
  require(query(19).paint.color == QColor("red"), "authored HTML starts with its current class style");
  require(session.applyTextEdit(13, 3, "cold") != muffin::TextEditResult::Rejected, "HTML class edit applies");
  theme.invalidateDocumentStyles(session.lastLocalTopLevelRangeChange(), &session.document());
  theme.resolveDocumentCounters(session.document());
  require(!theme.hasLocalStyleChange() && query(20).paint.color == QColor("blue"),
          "attribute edits invalidate final inline values even with the same source position");
  theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss("span{font-size:25px;color:purple}", "other", {}));
  theme.updateForViewport(800, 600);
  require(query(20).text.fontSizePx == 25 && query(20).paint.color == QColor("purple"),
          "theme replacement cannot retain the old theme's color or font");
}

void testSharedSelectorPrefixesKeepContext() {
  const auto sheet =
      CssThemeParser::parse(".wrap > p > strong{color:red}.wrap p[data-mode=hot] > strong{color:blue}"
                            ".wrap p:nth-child(2) strong{background:yellow}.wrap p:has(em) strong{border-color:green}"
                            ".wrap > h1 + p strong{outline-color:purple}"
                            "p > :is(strong,em):not([data-skip]){letter-spacing:2px}"
                            ".wrap:hover p > strong{background:cyan}p > strong:focus{color:orange}",
                            {});
  CssComputedStyleEngine engine(sheet);
  CssElement root;
  root.tag = "div";
  root.classes << "wrap";
  root.cacheId = nextCssElementIdentity();
  CssElement heading;
  heading.tag = "h1";
  heading.parent = &root;
  heading.cacheId = nextCssElementIdentity();
  CssElement paragraph;
  paragraph.tag = "p";
  paragraph.parent = &root;
  paragraph.cacheId = nextCssElementIdentity();
  paragraph.childIndex = 1;
  paragraph.previousSibling = &heading;
  paragraph.hasDescendantTags << "em";
  paragraph.attributes["data-mode"] = "cold";
  CssElement leaf;
  leaf.tag = "strong";
  leaf.parent = &paragraph;
  const auto verify = [&] {
    for (int i = 0; i < 32; ++i) {
      leaf.cacheId = nextCssElementIdentity();
      leaf.attributes.clear();
      if (i % 2)
        leaf.attributes["data-skip"] = "yes";
      CssComputedStyleEngine reference(sheet);
      for (int bits = 0; bits < 4; ++bits) {
        CssElementState state;
        state.subject = bits & 1 ? &root : &leaf;
        state.hover = bits & 1;
        state.focus = bits & 2;
        require(engine.styleFor(leaf, state) == reference.styleFor(leaf, state),
                "shared prefixes retain ancestor attributes, positions, descendants, sibling and scoped states");
      }
    }
  };
  verify();
  auto style = engine.styleFor(leaf);
  require(style.rawValue("color") == "red" && style.rawValue("background-color") == "yellow" &&
              style.rawValue("border-top-color") == "green" && style.rawValue("outline-color") == "purple",
          "mixed ancestor and sibling prefix predicates apply");
  paragraph.attributes["data-mode"] = "hot";
  heading.tag = "h2";
  paragraph.hasDescendantTags.clear();
  engine.clearCache();
  verify();
  style = engine.styleFor(leaf);
  require(style.rawValue("color") == "blue" && style.rawValue("border-top-color").isEmpty() &&
              style.rawValue("outline-color").isEmpty(),
          "structural invalidation retires all dependent prefix results");
  root.classes.clear();
  engine.clearCache();
  verify();
  require(engine.styleFor(leaf).rawValue("color").isEmpty(), "removing an ancestor class cannot reuse its old prefix");
}

void testSubjectCandidateIndexKeepsContext() {
  const auto sheet =
      CssThemeParser::parse("strong{color:black}strong[data-kind=hot]{color:red}strong[data-kind=cold]{color:blue}"
                            ":is(p:hover > strong,.absent):not(:focus){background:yellow}"
                            "strong:focus{background:green}strong:nth-child(2){opacity:.5}"
                            "strong + :where(em,strong){border-color:purple}"
                            "[id=chosen][class~=special]{outline-color:orange}"
                            ":is(strong,em)::before{content:'before'}",
                            {});
  CssComputedStyleEngine engine(sheet);
  CssElement p;
  p.tag = "p";
  CssElement first;
  first.tag = "strong";
  first.parent = &p;
  first.childIndex = 0;
  first.attributes.insert("DATA-KIND", "hot");
  CssElement next = first;
  next.previousSibling = &first;
  next.childIndex = 1;
  next.attributes["DATA-KIND"] = "cold";
  require(engine.styleFor(first).rawValue("color") == "red" && engine.styleFor(next).rawValue("color") == "blue",
          "metadata groups must defer attribute values and preserve case-insensitive names");
  require(engine.styleFor(next).rawValue("opacity") == ".5" &&
              engine.styleFor(next).rawValue("border-top-color") == "purple",
          "index candidates must preserve position and sibling predicates");
  CssElementState state;
  state.subject = &next;
  state.hover = true;
  require(engine.styleFor(next, state).rawValue("background-color") == "yellow",
          "nested functional alternatives must retain ancestor interaction states");
  state.focus = true;
  require(engine.styleFor(next, state).rawValue("background-color") == "green",
          "dynamic negations must remain in the complete state matcher");
  next.id = "chosen";
  next.classes << "special";
  require(engine.styleFor(next).rawValue("outline-color") == "orange",
          "virtual ID and class attributes must survive subject filtering");
  CssElement pseudo = next;
  pseudo.pseudoElement = "before";
  pseudo.originatingElement = &next;
  require(engine.styleFor(pseudo).rawValue("content") == "'before'",
          "functional pseudo subjects must match the originating element");
}

} // namespace
int main(int argc, char** argv) {
  QApplication::setAttribute(Qt::AA_Use96Dpi);
  QApplication app(argc, argv);
  runTest("background used values", testBackgroundUsedValues);
  runTest("per-property transitions and reversal", testPerPropertyTransitionsAndReversal);
  runTest("keyframes and idle", testKeyframesAndIdle);
  runTest("paused animation and sparse tracks", testPausedAnimationAndSparseTracks);
  runTest("scoped selector state", testScopedSelectorState);
  runTest("cross-element final styles and recipes", testCrossElementFinalStylesAndRecipes);
  runTest("inline style lifetime and theme changes", testInlineStyleLifetimeAndThemeChanges);
  runTest("shared selector prefixes keep context", testSharedSelectorPrefixesKeepContext);
  runTest("unchanged host color uses shaped formats", testUnchangedHostColorUsesShapedFormats);
  runTest("sparse text shadow matches full canvas", testSparseTextShadowMatchesFullCanvas);
  runTest("right-aligned text shadow", testRightAlignedTextShadow);
  runTest("text shadow lifetime and budget", testTextShadowLifetimeAndBudget);
  runTest("inline shadow invalidation", testInlineShadowInvalidation);
  runTest("subject candidate index preserves context", testSubjectCandidateIndexKeepsContext);
  runTest("scoped state cache and functional selectors", testScopedStateCacheAndFunctionalSelectors);
  runTest("structural interaction dependencies", testStructuralInteractionDependencies);
  runTest("reduced recipe state combinations", testReducedRecipeStateCombinations);
  runTest("inline fragments and precise hover", testInlineFragmentsAndPreciseHover);
  runTest("nested wrapped backgrounds", testNestedWrappedInlineBackgrounds);
  runTest("inline compositing and interaction states", testInlineCompositeAndInteractionStates);
  runTest("host and pseudo property timelines", testHostAndPseudoPropertyTimelines);
  runTest("motion shorthand cascade and keyframe expansion", testMotionShorthandCascadeAndKeyframeExpansion);
  runTest("element compositing and pointer coordinates", testElementCompositingAndPointerCoordinates);
  runTest("pseudo filter affects glyphs", testPseudoFilterAffectsGlyphs);
  runTest("flow pseudo effects and ordered animation", testFlowPseudoEffectsAndOrderedAnimation);
  return 0;
}
