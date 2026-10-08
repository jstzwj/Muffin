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
#include <QFontDatabase>
#include <QRawFont>
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
  const auto box = block->cssBoxGeometry(theme);
  near(box.flowRect.height() - block->inlineLayout()->height(), 25.375, "heading flow includes padding and border");
  near(box.inlineTextOrigin.y(), box.contentBox.top(), "paint and caret originate at content box");
  theme.setFontSizePx(18);
  theme.updateForViewport(1914, 1030);
  near(theme.elementStyleForNode(node, "h1")->box.margin.top(), 67.5, "user text scale changes font-relative margins");
  near(theme.elementStyleForNode(node, "h1")->box.borderBottomWidth, 1, "user text scale leaves absolute border width unchanged");
  theme.setZoomPercent(125);
  theme.updateForViewport(1914, 1030);
  near(theme.headingPadding(1).bottom(), 24.375 * 1.125 * 1.25, "font-relative padding receives text and view scales once");
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
  return 0;
}
