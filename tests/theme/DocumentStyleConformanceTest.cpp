#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include "theme/ThemeFontDecoder.h"
#include "theme/RenderTheme.h"
#include "theme/NodeCssElement.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "html/HtmlRenderer.h"
#include "html/HtmlLayoutResult.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontDatabase>
#include <QRawFont>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {
void require(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
void near(qreal actual, qreal expected, const char* message) {
  if (qAbs(actual - expected) > .01) {
    std::fprintf(stderr, "actual=%g expected=%g\n", actual, expected);
    require(false, message);
  }
}
void testCascadeAndInheritance() {
  const auto sheet =
      muffin::CssThemeParser::parse(QStringLiteral("html{font-size:16px} body{font-size:125%;color:#123456;line-height:1.5em}"
                                                   "h1{font-size:1.5em;margin:2em;padding-left:99px;padding:1em 2em;"
                                                   "border-bottom:1px solid;border-color:#c5c5c5}"
                                                   "h2{border-color:red;border-bottom:1px solid}"
                                                   "h3{padding:2px !important;padding-left:8px}"),
                                    {});
  muffin::CssComputedStyleEngine engine(sheet);
  muffin::CssElement html;
  html.tag = "html";
  muffin::CssElement body;
  body.tag = "body";
  body.parent = &html;
  muffin::CssElement h;
  h.tag = "h1";
  h.parent = &body;
  const auto style = engine.styleFor(h);
  near(style.fontSizePx, 30, "font em resolves from computed parent");
  require(style.resolvedValue("line-height") == QStringLiteral("30px"), "length line-height inherits computed length");
  require(style.resolvedValue("padding-left") == QStringLiteral("2em"), "later shorthand beats longhand");
  require(style.resolvedValue("border-bottom-color") == QStringLiteral("#c5c5c5"), "later global color updates side");
  h.tag = "h2";
  require(engine.styleFor(h).resolvedValue("border-bottom-color") == QStringLiteral("currentColor"),
          "later border shorthand resets omitted color");
  h.tag = "h3";
  require(engine.styleFor(h).resolvedValue("padding-left") == QStringLiteral("2px"), "important shorthand protects components");
  const auto inlineStyle = engine.styleFor(h, {}, muffin::CssThemeParser::parseDeclarations("padding-left:12px"));
  require(inlineStyle.resolvedValue("padding-left") == QStringLiteral("2px"), "stylesheet important beats normal inline longhand");
}
void testConditionalVariables() {
  const auto sheet = muffin::CssThemeParser::parse(QStringLiteral(":root{--ink:red}@media screen and (min-width:1400px){:root{--ink:blue}"
                                                                  "@media (max-width:1600px){p{padding:2px}}}p{color:var(--ink)}"),
                                                   {});
  muffin::CssElement html;
  html.tag = "html";
  muffin::CssElement p;
  p.tag = "p";
  p.parent = &html;
  muffin::CssEnvironment env;
  env.viewportWidth = 1399;
  require(muffin::CssComputedStyleEngine(sheet, env).styleFor(p).resolvedValue("color") == QStringLiteral("red"),
          "inactive media variable cannot leak");
  env.viewportWidth = 1400;
  const auto s = muffin::CssComputedStyleEngine(sheet, env).styleFor(p);
  require(s.resolvedValue("color") == QStringLiteral("blue") && s.resolvedValue("padding-left") == QStringLiteral("2px"),
          "nested media AND and scoped root variable");
  env.viewportWidth = 1601;
  require(!muffin::CssComputedStyleEngine(sheet, env).styleFor(p).hasProperty("padding-left"), "nested condition exits");
}
void testNewsprintGeometryAndFonts() {
  auto theme = muffin::RenderTheme::newsprint();
  muffin::DocumentSession session;
  session.setMarkdownText("# Title\n\nparagraph\n\n## Section\n", false);
  for (const int width : {1399, 1400, 1914, 1399}) {
    theme.updateForViewport(width, 1030);
    muffin::DocumentLayout layout;
    layout.rebuild(session.document(), theme, width);
    near(layout.pageWidth(), width < 1400 ? 580 : 854, "browser Newsprint content width at breakpoint");
  }
  const auto& heading = *session.document().root().children().front();
  const auto* style = theme.elementStyleForNode(heading, "h1");
  near(style->box.margin.top(), 60, "live heading em geometry matches browser");
  near(style->box.padding.bottom(), 24.375, "live heading padding uses own font");
  require(style->box.borderBottomColor == QColor("#c5c5c5"), "Newsprint heading border color survives shorthand");
  require(QFontDatabase::families().contains("PT Serif"), "bundled PT Serif loaded");
  near(theme.codeLineHeight(), 24, "code block line height comes from document CSS");
  for (const char* filename : {"regular", "italic", "700", "700italic"}) {
    QFile file(QString(":/themes/newsprint/pt-serif-v11-latin-%1.woff2").arg(filename));
    require(file.open(QIODevice::ReadOnly), "native font resource exists at authored URL");
    const auto raw = QRawFont(file.readAll(), 16);
    require(raw.isValid() && raw.familyName() == "PT Serif", "native backend accepts bundled native face");
  }
}
void testImportedWebFonts() {
  for (const QString& path : {QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/newsprint/pt-serif-v11-latin-regular.woff2"),
                              QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/pixyll/lato-v14-latin-300.woff")}) {
    QFile source(path);
    require(source.open(QIODevice::ReadOnly), "webfont fixture readable");
    const QByteArray native = muffin::decodeThemeWebFont(source.readAll());
    require(!native.isEmpty() && !native.startsWith("wOFF") && !native.startsWith("wOF2"), "webfont decoder reconstructs SFNT");
    require(QRawFont(native, 16).isValid(), "native backend loads reconstructed webfont");
  }
  require(muffin::decodeThemeWebFont(QByteArray("wOF2broken")).isEmpty(), "invalid webfont fails without registration");
}
void testHeadingBoxAndTextScale() {
  auto theme = muffin::RenderTheme::newsprint();
  theme.updateForViewport(1914, 1030);
  muffin::DocumentSession session;
  session.setMarkdownText("# Title\n\nText\n", false);
  muffin::DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1914);
  const auto& node = *session.document().root().children().front();
  const auto* block = layout.block(node.id());
  const auto box = block->cssBoxGeometry();
  near(box.flowRect.height() - block->inlineLayout()->height(), 25.375, "heading flow includes padding and border");
  near(box.inlineTextOrigin.y(), box.contentBox.top(), "paint and caret originate at content box");
  theme.setFontSizePx(18);
  theme.updateForViewport(1914, 1030);
  near(theme.elementStyleForNode(node, "h1")->box.margin.top(), 67.5, "user text scale changes font-relative margins");
  near(theme.elementStyleForNode(node, "h1")->box.borderBottomWidth, 1, "user text scale leaves absolute border width unchanged");
  theme.setZoomPercent(125);
  theme.updateForViewport(1914, 1030);
  near(theme.elementBoxStyle(QStringLiteral("h%1").arg(1)).padding.bottom(), 24.375 * 1.125 * 1.25,
       "font-relative padding receives text and view scales once");
  theme.setContentWidthPx(1000);
  theme.updateForViewport(1914, 1030);
  near(theme.pageWidth(), 1250, "explicit content width remains independent of text scale");
}
void testHtmlUsesDocumentCascade() {
  auto theme = muffin::RenderTheme::newsprint();
  theme.updateForViewport(1914, 1030);
  muffin::html::HtmlColorPalette palette = muffin::html::HtmlColorPalette::defaultLight();
  palette.documentStyleSheet = theme.documentStyleSheet();
  palette.fontAliases = theme.fontAliases();
  palette.cssEnvironment.viewportWidth = 1914;
  muffin::html::HtmlRenderer renderer;
  const auto result =
      renderer.render("<h1 style='padding-bottom:1em'>Title</h1><p style='font-size:20px'><span>Text</span></p>", 12, 854, {}, palette);
  require(result.valid(), "themed HTML block lays out");
  std::function<const muffin::html::HtmlBox*(const muffin::html::HtmlBox&, muffin::html::HtmlTag)> find;
  find = [&](const auto& box, auto tag) -> const muffin::html::HtmlBox* {
    if (box.tag() == tag) return &box;
    for (const auto& child : box.children()) {
      if (const auto* found = find(*child, tag)) return found;
    }
    return nullptr;
  };
  const auto* heading = find(*result.root(), muffin::html::HtmlTag::Heading1);
  require(heading != nullptr, "HTML heading exists");
  near(heading->style().fontSize, 22.5, "HTML heading uses document computed font");
  near(heading->style().padding.bottom(), 30, "HTML inline em uses computed heading size");
  require(heading->style().font.families().contains("PT Serif"), "HTML shares registered document family");
  require(heading->style().fontWeight == QFont::Normal, "HTML text measurement respects normal heading weight");
  const auto* span = find(*result.root(), muffin::html::HtmlTag::Span);
  require(span != nullptr, "HTML span exists");
  near(span->style().fontSize, 15, "HTML descendants inherit parent's inline font size");
}
void testCodeStylesRemainSeparate() {
  const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(
      "#write{font-size:16px}code{font-size:12px;font-family:Consolas}pre{font-size:20px;line-height:1.8;font-family:'Courier New'}",
      "code-styles", {}));
  near(theme.inlineCodeFont().pointSizeF(), 9, "inline code font follows code selector");
  near(theme.codeFont().pointSizeF(), 15, "code block font follows pre selector");
  near(theme.codeLineHeight(), 36, "code block measured and painted line height follows pre selector");
}
void testUsedBoxesAndBorderDefaults() {
  muffin::CssElement p;
  p.tag = "p";
  const auto sheet = muffin::CssThemeParser::parse("p{margin:10%;padding:calc(5% + 2px);border-left-width:8px;box-sizing:border-box}", {});
  const auto computed = muffin::CssComputedStyleEngine(sheet).styleFor(p);
  const auto box = muffin::CssThemeMapper::projectComputedStyle("p", computed.withContainingWidth(600)).box;
  near(box.margin.left(), 60, "margin uses containing width rather than viewport or em");
  near(box.padding.top(), 32, "calc percentage combines with absolute part at used-value stage");
  near(box.borderLeftWidth, 0, "initial border style none suppresses specified border width");
  require(box.borderBox, "box-sizing survives projection");
  const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss("h1{padding:10%}", "used-padding", {}));
  near(theme.elementBoxStyle(QStringLiteral("h%1").arg(1), nullptr, 600).padding.left(), 60,
       "Markdown heading resolves percentage at layout width");
  near(theme.elementBoxStyle(QStringLiteral("h%1").arg(1), nullptr, 300).padding.left(), 30,
       "same cached Markdown style responds to containing width");
  const auto border =
      muffin::CssThemeMapper::projectComputedStyle(
          "p", muffin::CssComputedStyleEngine(muffin::CssThemeParser::parse("p{color:red;border-style:solid}", {})).styleFor(p))
          .box;
  near(border.borderLeftWidth, 3, "border-style alone uses initial medium width");
  require(border.borderLeftColor == QColor("red"), "border initial color follows currentColor");
}

void testHtmlContainingBlockAndBoxSizing() {
  auto palette = muffin::html::HtmlColorPalette::defaultLight();
  palette.documentStyleSheet = std::make_shared<muffin::CssThemeSheet>(muffin::CssThemeParser::parse("", {}));
  muffin::html::HtmlRenderer renderer;
  for (bool borderBox : {false, true}) {
    const auto result = renderer.render(QString("<div style='width:300px;padding:20px;border:2px solid;box-sizing:%1'>"
                                                "<p style='margin:0;margin-left:10%;padding:calc(5% + 2px);width:50%'>Text</p></div>")
                                            .arg(borderBox ? "border-box" : "content-box"),
                                        12, 900, {}, palette);
    require(result.valid(), "HTML used-value fixture lays out");
    const auto* div = result.root()->children().front().get();
    const auto* p = div->children().front().get();
    const qreal content = borderBox ? 256 : 300;
    near(div->geometry().width, borderBox ? 300 : 344, "HTML width honors box-sizing");
    near(p->style().margin.left(), content * .1, "nested HTML percentage uses parent content width");
    near(p->style().padding.top(), content * .05 + 2, "HTML calc resolves after containing block exists");
  }
}

void testDuplicateNodeIdsDoNotAliasStyle() {
  const auto definition = muffin::CssThemeMapper::fromCss("p{color:red}h1{color:blue}p:first-child{color:green}", "cache-identity", {});
  const auto theme = muffin::RenderTheme::fromDefinition(definition);
  const auto id = muffin::NodeId::create();
  muffin::MarkdownNode p(muffin::BlockType::Paragraph, id);
  muffin::MarkdownNode h(muffin::BlockType::Heading, id);
  h.setHeadingLevel(1);
  require(theme.elementStyleForNode(p, "p")->paint.color != theme.elementStyleForNode(h, "h1")->paint.color,
          "different nodes sharing an ID must not alias computed styles");
}

void testPercentageFlowAndZeroFont() {
  const auto definition = muffin::CssThemeMapper::fromCss(
      "#write{max-width:600px;margin:0;padding:0}p{margin:10% 0 5%;margin-left:10%}h1{font-size:0em}", "percentage-flow", {});
  auto theme = muffin::RenderTheme::fromDefinition(definition);
  require(theme.elementStyle("h1")->text.fontSizeSet && theme.elementStyle("h1")->text.fontSizePx == 0,
          "projection distinguishes explicit font-size zero from missing font size");
  require(theme.textFontForElement("h1").pointSizeF() < .01, "native font-size zero must not select a large heading fallback");
  muffin::DocumentSession session;
  session.setMarkdownText("Text\n\nMore text\n", false);
  for (int width : {900, 450}) {
    theme.updateForViewport(width, 800);
    muffin::DocumentLayout layout;
    layout.rebuild(session.document(), theme, width);
    const auto& children = session.document().root().children();
    const auto* first = layout.block(children[0]->id());
    const auto* second = layout.block(children[1]->id());
    const auto content = layout.pageWidth();
    near(first->rect().width(), content * .9, "horizontal percentage margin narrows actual text layout");
    near(second->rect().top() - first->rect().bottom(), content * .1, "vertical percentage margins collapse using actual page width");
  }
}

void testUnifiedHeadingSnapshotAndLazyLayout() {
  muffin::DocumentSession document;
  document.setMarkdownText(
      "# A heading long enough to wrap when the content width is narrow\n\n"
      "Text with **strong `code`**.\n\n```cpp\nint x;\n```\n\n| H | V |\n| --- | --- |\n| one | two |\n",
      false);
  qreal previousHeight = 0;
  for (int size : {20, 30}) {
    const auto css = QString(
                         "#write{max-width:420px;padding:0;margin:0}body{color:black}"
                         "h1{font-size:%1px;line-height:2;padding:7px 13px 11px 17px;border:3px solid red;margin:0}"
                         "strong code{font-size:11px;padding:2px 9px 4px 5px;border:2px solid blue}"
                         "pre{width:260px;box-sizing:border-box;padding:9px 15px;border:4px solid green}"
                         "th{padding:12px 20px;font-size:18px}td{padding:3px 7px;font-size:12px}")
                         .arg(size);
    const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(css, "snapshots", {}));
    muffin::DocumentLayout eager, lazy;
    eager.rebuild(document.document(), theme, 800);
    lazy.rebuild(document.document(), theme, 800, {}, {}, muffin::DocumentLayout::BuildPolicy::Lazy);
    lazy.buildAll(theme);
    near(eager.totalHeight(), lazy.totalHeight(), "lazy promotion converges to eager document height");
    for (const auto* block : eager.promotedBlocks()) {
      const auto* promoted = lazy.block(block->nodeId());
      require(promoted && block->rect() == promoted->rect(), "lazy and eager flow rectangles match");
      require(block->cssBoxGeometry().contentBox == promoted->cssBoxGeometry().contentBox, "lazy and eager content boxes match");
    }
    const auto& node = *document.document().root().children()[0];
    const auto* heading = eager.block(node.id());
    const auto box = heading->cssBoxGeometry();
    near(box.font.pointSizeF() * 96 / 72, size, "heading font belongs to its layout snapshot");
    near(box.contentBox.left() - box.borderBox.left(), 20, "left border and padding define content origin");
    near(box.contentBox.top() - box.borderBox.top(), 10, "top border and padding enter layout");
    require(box.borderBox == heading->rect(), "paint border and flow use the same rectangle");
    const auto caret = heading->inlineLayout()->cursorRect(3).translated(box.inlineTextOrigin);
    const auto hit = eager.hitTest(QPointF(caret.left(), caret.center().y()), theme);
    require(hit.isValid() && hit.textOffset == 3, "heading caret and hit test use the saved content origin");
    const auto selection = heading->selectionRectsForOffsets(0, 3, theme);
    require(!selection.isEmpty() && selection.front().left() >= box.contentBox.left() - 1.01,
            "selection uses content coordinates with its 1px paint expansion");
    QImage image(500, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.translate(-box.borderBox.left(), -box.borderBox.top());
    heading->paint(painter, theme, 0);
    painter.end();
    require(image.pixelColor(1, 1) == QColor(Qt::red), "generic box painter draws the heading border at saved coordinates");
    if (previousHeight) require(heading->height() > previousHeight, "font CSS change updates wrapping and flow height together");
    previousHeight = heading->height();
    const auto* paragraph = eager.block(document.document().root().children()[1]->id());
    require(!paragraph->inlineLayout()->inlineBoxes().isEmpty(), "contextual code produces a saved inline box");
    const auto& code = paragraph->inlineLayout()->inlineBoxes().front();
    near(code.font.pointSizeF() * 96 / 72, 11, "inline code inherits through the real strong parent");
    near(code.usedBox.padding.left(), 5, "inline code keeps asymmetric left padding");
    near(code.usedBox.padding.right(), 9, "inline code keeps asymmetric right padding");
    const auto* pre = eager.block(document.document().root().children()[2]->id());
    near(pre->rect().width(), 260, "generic fixed border-box width also controls code fences");
    const auto* table = eager.block(document.document().root().children()[3]->id());
    near(table->tableRows()[0].cells[0].box.usedBox.padding.top(), 12, "header cell uses actual th padding");
    near(table->tableRows()[1].cells[0].box.usedBox.padding.top(), 3, "body cell uses actual td padding");
  }
}

void testLegacyThemeUsesDocumentEngine() {
  muffin::ThemeDefinition definition;
  definition.colors.text = QColor("#112233");
  definition.typography.headingSizePt[0] = 24;
  definition.typography.bodyFont = "Georgia";
  auto theme = muffin::RenderTheme::fromDefinition(definition);
  muffin::MarkdownNode heading(muffin::BlockType::Heading);
  heading.setHeadingLevel(1);
  require(theme.documentStyleSheet() && theme.elementStyleForNode(heading, "h1"), "legacy themes enter the shared style engine");
  near(theme.elementStyleForNode(heading, "h1")->text.fontSizePx, 32, "legacy heading size is lowered into CSS");
  require(theme.elementStyleForNode(heading, "h1")->paint.color == definition.colors.text,
          "legacy inherited color comes from computed style");
}

void testInlineHtmlUsesActualBoxAndFont() {
  muffin::DocumentSession document;
  document.setMarkdownText("Before <kbd style='padding:3px 7px;font-size:20px'><span>A</span>B</kbd> after", false);
  const auto theme = muffin::RenderTheme::fromDefinition(
      muffin::CssThemeMapper::fromCss("#write{padding:0}kbd{padding:1px;font-size:10px}p{letter-spacing:-.5px}", "inline-html-box", {}));
  muffin::DocumentLayout layout;
  layout.rebuild(document.document(), theme, 800);
  const auto* paragraph = layout.block(document.document().root().children().front()->id());
  const auto* text = paragraph->inlineLayout();
  require(text->inlineBoxes().size() == 1, "nested spans share one keyboard box and reserve its padding once");
  const auto& box = text->inlineBoxes().front();
  near(box.font.pointSizeF() * 96 / 72, 20, "HTML inline font wins over the theme keyboard prototype");
  near(box.usedBox.padding.left(), 7, "HTML inline padding controls the saved keyboard box");
  near(box.borderBox.width() - box.contentBox.width(), 16, "keyboard border and padding occupy actual text advance");
  near(theme.paragraphFont().letterSpacing(), -.5, "negative letter spacing reaches the common font projection");
  for (int offset = 0; offset <= text->visibleText().size(); ++offset) {
    const auto caret = text->cursorRect(offset);
    require(text->hitTestTextOffset(caret.center()) == offset, "HTML box spacers preserve visible-text caret round trips");
  }
  auto palette = muffin::html::HtmlColorPalette::defaultLight();
  palette.documentStyleSheet = theme.documentStyleSheet();
  const auto html =
      muffin::html::HtmlRenderer().render("<strong><span style='font-weight:normal'>Normal</span></strong>", 12, 800, {}, palette);
  const auto* span = html.root()->children().front()->children().front().get();
  require(span->style().font.weight() == QFont::Normal, "computed normal weight resets inherited strong styling");

  muffin::DocumentSession nested;
  nested.setMarkdownText(
      "**<kbd class='hot'>X</kbd>** and <strong style='font-weight:normal'>plain</strong> <u style='text-decoration:none'>clear</u>",
      false);
  const auto nestedTheme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(
      "p > strong > kbd{padding:4px 19px;font-size:23px}p:has(.hot){padding-top:5px}", "mixed-inline-tree", {}));
  muffin::DocumentLayout nestedLayout;
  nestedLayout.rebuild(nested.document(), nestedTheme, 800);
  const auto* nestedText = nestedLayout.block(nested.document().root().children().front()->id())->inlineLayout();
  require(nestedText->inlineBoxes().size() == 1, "Markdown and HTML share the same semantic ancestry");
  near(nestedText->inlineBoxes().front().usedBox.padding.left(), 19, "direct-child CSS crosses the Markdown/HTML boundary");
  near(nestedText->inlineBoxes().front().font.pointSizeF() * 96 / 72, 23, "contextual HTML font comes from its actual Markdown parent");
  near(nestedLayout.block(nested.document().root().children().front()->id())->cssBoxGeometry().usedBox.padding.top(), 5,
       "has selectors see embedded HTML classes");
  bool sawNormal = false, sawClear = false;
  const auto rendered = nestedText->displayText();
  for (const auto& format : nestedText->debugTextFormats(nestedTheme, nestedTheme.paragraphFont())) {
    const auto value = rendered.mid(format.start, format.length);
    if (value == "plain" && format.format.fontWeight() == QFont::Normal) sawNormal = true;
    if (value == "clear" && !format.format.fontUnderline()) sawClear = true;
  }
  require(sawNormal, "simple inline HTML uses computed font overrides while preserving Markdown projection");
  require(sawClear, "inline HTML decoration resets come from the common computed style");
}

void testStructuralEditInvalidatesExistingLayout() {
  for (bool lazy : {false, true}) {
    muffin::DocumentSession document;
    document.setMarkdownText("# Heading\n\nBody\n\nTail", false);
    const auto theme = muffin::RenderTheme::fromDefinition(
        muffin::CssThemeMapper::fromCss("#write{max-width:450px;padding:0;margin:0}body{color:black}p{margin:4px 0}h1 + "
                                        "p{padding:16px;border:2px solid red}p + p{font-size:28px;margin-top:40px}",
                                        "invalidation", {}));
    muffin::DocumentLayout incremental;
    incremental.rebuild(document.document(), theme, 800, {}, {},
                        lazy ? muffin::DocumentLayout::BuildPolicy::Lazy : muffin::DocumentLayout::BuildPolicy::Eager);
    incremental.buildAll(theme);
    require(document.applyTextDelta(0, 2, QString(), true), "heading removal applies as local edit");
    const auto range = document.lastLocalTopLevelRangeChange();
    require(range.isValid() && incremental.rebuildTopLevelRange(range, document.document(), theme, {}).rebuilt,
            "structural edit rebuilds changed range");
    muffin::DocumentLayout fresh;
    fresh.rebuild(document.document(), theme, 800);
    for (const auto& node : document.document().root().children()) {
      const auto* updated = incremental.block(node->id());
      const auto* expected = fresh.block(node->id());
      require(updated && expected && updated->rect() == expected->rect(),
              "unchanged siblings recompute fonts, padding and margins after edit");
      require(updated->cssBoxGeometry().contentBox == expected->cssBoxGeometry().contentBox,
              "incremental content coordinates match fresh layout");
    }
    near(incremental.totalHeight(), fresh.totalHeight(), "structural invalidation converges document height");
  }
}

void testHtmlBlockUsesSharedBoxPainter() {
  auto palette = muffin::html::HtmlColorPalette::defaultLight();
  palette.documentStyleSheet = std::make_shared<muffin::CssThemeSheet>(
      muffin::CssThemeParser::parse("p{margin:0;padding:7px 13px;border:3px solid red;line-height:40px}"
                                    "kbd{padding:2px 9px 4px 5px;border:2px solid blue;font-size:20px;border-radius:0}",
                                    {}));
  const auto result = muffin::html::HtmlRenderer().render("<p>Before <kbd><span>A</span>B</kbd> after</p>", 12, 500, {}, palette);
  require(result.valid(), "HTML block fixture lays out");
  const auto* paragraph = result.root()->children().front().get();
  const auto& box = paragraph->layoutBox;
  require(box.valid, "HTML saves the same box snapshot as Markdown");
  near(box.contentBox.left() - box.borderBox.left(), 16, "HTML content origin comes from shared padding and border geometry");
  muffin::html::HtmlTextMeasurer measurer;
  const auto text = measurer.buildInlineLayout(*paragraph, paragraph->style().fontSize, 450);
  require(text->inlineBoxes.size() == 1, "nested HTML keyboard spans share one inline box");
  near(text->inlineBoxes.front().usedBox.padding.left(), 5, "HTML inline layout uses contextual left padding");
  near(text->inlineBoxes.front().usedBox.padding.right(), 9, "HTML inline layout uses contextual right padding");
  near(text->lineHeight, 40, "HTML line height consumes shared computed pixels");
  require(text->layout->text().size() == text->text.size() + 2, "HTML horizontal box edges reserve actual wrapping advance");
  QImage image(600, 200, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  result.paint(painter, {});
  painter.end();
  require(image.pixelColor(qRound(box.borderBox.left()) + 1, qRound(box.borderBox.top()) + 1) == QColor(Qt::red),
          "HTML paints the saved border through the shared box painter");
}

void testBrowserBoxReference() {
  QFile fixture(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/document-box-browser.json"));
  require(fixture.open(QIODevice::ReadOnly), "browser box reference is readable");
  const auto reference = QJsonDocument::fromJson(fixture.readAll()).object();
  const auto viewport = reference["viewport"].toObject();
  require(reference["cases"].toArray().size() == 6, "browser reference covers all paragraph, heading and pre box-sizing cases");
  for (const auto& value : reference["cases"].toArray()) {
    const auto entry = value.toObject(), expected = entry["expected"].toObject();
    const auto tag = entry["tag"].toString();
    const auto theme =
        muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(entry["css"].toString(), "browser-reference", {}));
    muffin::DocumentSession document;
    document.setMarkdownText(tag == "h1" ? "# Text" : tag == "pre" ? "```\nText\n```" : "Text", false);
    muffin::DocumentLayout layout;
    layout.rebuild(document.document(), theme, viewport["width"].toDouble());
    const auto& box = layout.block(document.document().root().children().front()->id())->cssBoxGeometry();
    near(box.borderBox.width(), expected["width"].toDouble(), "border width matches Chrome used box");
    near(box.borderBox.height(), expected["height"].toDouble(), "border height matches Chrome used box");
    near(box.contentBox.left() - box.borderBox.left(), expected["left"].toDouble(), "horizontal content inset matches Chrome");
    near(box.contentBox.top() - box.borderBox.top(), expected["top"].toDouble(), "vertical content inset matches Chrome");
    near(box.contentBox.width(), expected["contentWidth"].toDouble(), "wrapping width matches Chrome content box");
    near(box.font.pointSizeF() * 96 / 72, expected["fontSize"].toDouble(), "saved font size matches Chrome computed font");
  }
}

void testLazyPromotionUsesLiveMargins() {
  muffin::DocumentSession document;
  document.setMarkdownText("First\n\nSecond\n\nThird", false);
  const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(
      "#write{padding:0;margin:0}p{margin:0}p:first-child{margin-top:37px}p + p{margin-top:23px}p:last-child{margin-bottom:51px}",
      "live-lazy-margins", {}));
  muffin::DocumentLayout eager, lazy;
  eager.rebuild(document.document(), theme, 800);
  lazy.rebuild(document.document(), theme, 800, {}, {}, muffin::DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  for (const auto& node : document.document().root().children()) {
    require(eager.block(node->id())->rect() == lazy.block(node->id())->rect(),
            "promotion replaces both leading and trailing prototype margins with live used values");
  }
  near(eager.totalHeight(), lazy.totalHeight(), "live lazy margins converge to eager height");
}

void testResponsiveProjectionReuse() {
  const auto definition = muffin::CssThemeMapper::fromCss(
      "#write{max-width:500px;padding:20px}p{font-size:16px}"
      "@media(min-width:900px){p{font-size:24px}}"
      "@media(orientation:portrait){h1{font-size:25px}}",
      "resize", {});
  auto theme = muffin::RenderTheme::fromDefinition(definition);
  require(theme.updateForViewport(700, 600), "first viewport projects the environment");
  auto original = theme;
  require(!theme.updateForViewport(850, 600), "same active rules reuse theme projections");
  near(theme.documentCssEnvironment().viewportWidth, 850, "reused projection exposes the current viewport");
  near(original.documentCssEnvironment().viewportWidth, 700, "theme copy retains its viewport");
  require(theme.updateForViewport(950, 600), "crossing width media condition invalidates projections");
  near(theme.elementStyle("p")->text.fontSizePx, 24, "media font size updates");
  require(theme.updateForViewport(950, 1000), "height/orientation change invalidates projections");
  near(theme.elementStyle("h1")->text.fontSizePx, 25, "orientation media applies");
  for (const auto& css :
       {QString(":root{--size:2vw}#write{max-width:500px}p{font-size:var(--size)}"),
        QString("#write{width:70%;padding:2% 3%;margin:1% auto}"), QString("#write{max-width:calc(80% - 10px);padding:0}")}) {
    const auto dynamicDefinition = muffin::CssThemeMapper::fromCss(css, "dynamic-resize", {});
    auto dynamic = muffin::RenderTheme::fromDefinition(dynamicDefinition);
    dynamic.updateForViewport(700, 600);
    require(dynamic.updateForViewport(850, 600), "viewport units and percentage page boxes recompute");
    auto fresh = muffin::RenderTheme::fromDefinition(dynamicDefinition);
    fresh.updateForViewport(850, 600);
    near(dynamic.pageWidth(), fresh.pageWidth(), "responsive width agrees with a fresh projection");
    require(dynamic.pagePadding() == fresh.pagePadding() && dynamic.pageMargin() == fresh.pageMargin(),
            "responsive page insets agree with a fresh projection");
    near(dynamic.elementStyle("p")->text.fontSizePx, fresh.elementStyle("p")->text.fontSizePx,
         "responsive font agrees with a fresh projection");
  }
  theme.setZoomPercent(125);
  require(theme.updateForViewport(1187.5, 1250), "zoom invalidates even when CSS viewport dimensions agree");
  theme.setFontSizePx(18);
  require(theme.updateForViewport(1187.5, 1250), "user font size invalidates reused projections");
}

}  // namespace
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  testCascadeAndInheritance();
  testConditionalVariables();
  testNewsprintGeometryAndFonts();
  testImportedWebFonts();
  testHeadingBoxAndTextScale();
  testHtmlUsesDocumentCascade();
  testCodeStylesRemainSeparate();
  testUsedBoxesAndBorderDefaults();
  testHtmlContainingBlockAndBoxSizing();
  testDuplicateNodeIdsDoNotAliasStyle();
  testPercentageFlowAndZeroFont();
  testUnifiedHeadingSnapshotAndLazyLayout();
  testLegacyThemeUsesDocumentEngine();
  testInlineHtmlUsesActualBoxAndFont();
  testStructuralEditInvalidatesExistingLayout();
  testHtmlBlockUsesSharedBoxPainter();
  testBrowserBoxReference();
  testLazyPromotionUsesLiveMargins();
  testResponsiveProjectionReuse();
  return 0;
}
