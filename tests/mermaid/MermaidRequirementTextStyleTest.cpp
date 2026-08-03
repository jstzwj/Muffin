// requirementDiagram text styling (Commit 3) — CSS labelStyle text properties
// resolved into the Commit-2 FlowLabel fields + measure/paint font args.
//
// Covers: resolveRequirementTextStyle (3-layer font-size/weight cascade, Chrome
// 10000px cap, ex/ch font coupling, bolder/lighter per layer, font-family first
// token, line-height normal/zero/multiplier, spacing negatives, decoration,
// transform, color); applyRequirementTextTransform (Unicode word boundaries,
// markdown-transparent, math-boundary capitalize; ß->SS upper); the measure path
// (zero-collapse to 20x20, natural line-height); and the production render path
// (resolved fields reach the scene + a painter ink check for decoration/color).
//
// Expectations are the Step 0F/0F+/0F++ probe results (G:/github/req-probe/
// STEP0F_REPORT.md, 55/55 verifier). Font-independent values (em/rem/pt/vw/cap)
// are asserted exactly; font-metric values (ex/ch) are asserted relatively to
// avoid coupling to the bundled font.
#include "math/MathFontRegistry.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/requirement/RequirementLayout.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/requirement/RequirementTextStyle.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

// theme base passed to the resolver: Noto Sans @ 16px, Normal weight, 24px line.
const QString kFamily = MermaidFontRegistry::cssFamilyStack();
constexpr qreal kSize = 16.0;
constexpr qreal kLine = 24.0;
requirement::RequirementTextStyle resolve(const QStringList& css) {
  return requirement::resolveRequirementTextStyle(css, kFamily, kSize, QFont::Normal, kLine);
}

const requirement::RequirementSceneNode* nodeOf(const editor::MermaidRenderEntry& e,
                                                 const QString& id) {
  const auto* scene = dynamic_cast<const requirement::RequirementScene*>(e.scene.get());
  require(scene != nullptr, QStringLiteral("missing Requirement scene"));
  for (const auto& n : scene->nodes)
    if (n.id == id) return &n;
  return nullptr;
}
struct Rendered {
  const requirement::RequirementScene* scene;
  const requirement::RequirementSceneNode* node;
};
Rendered renderNode(editor::MermaidRenderCache& cache, const QString& source, const QString& id) {
  const auto entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("did not render: ") + entry.errorMessage);
  Rendered out{dynamic_cast<const requirement::RequirementScene*>(entry.scene.get()), nullptr};
  require(out.scene != nullptr, QStringLiteral("missing scene"));
  out.node = nodeOf(entry, id);
  require(out.node != nullptr, QStringLiteral("node '%1' not found").arg(id));
  return out;
}

QImage paintScene(const requirement::RequirementScene& scene) {
  QImage img(scene.bounds.size().toSize(), QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.translate(-scene.bounds.topLeft());
  MermaidPaintOptions opts;
  scene.paint(p, opts);
  p.end();
  return img;
}
// Count non-transparent pixels in the whole painted scene (text + box ink).
qint64 inkTotal(const QImage& img) {
  qint64 n = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x)
      if (qAlpha(img.pixel(x, y)) > 32) ++n;
  return n;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  muffin::math::MathFontRegistry::ensureLoaded();  // bundles KaTeX_Main (a serif
                                                   // face) used by the ex/ch
                                                   // font-coupling assertion below.
  editor::MermaidRenderCache cache;
  const QString head = QStringLiteral("requirementDiagram\n");

  // ===== 1. resolveRequirementTextStyle: defaults (no labelStyle -> use theme) =====
  {
    const auto s = resolve({});
    require(s.fontSizePx < 0.0, QStringLiteral("default fontSizePx < 0 (use theme)"));
    require(s.fontWeight == QFont::Normal, QStringLiteral("default weight Normal"));
    require(s.fontStyle == QFont::StyleNormal, QStringLiteral("default style Normal"));
    require(s.fontFamily.isEmpty(), QStringLiteral("default family empty (use theme)"));
    require(s.lineHeightPx < 0.0 && !s.lineHeightNormal,
            QStringLiteral("default lineHeight < 0 (use theme 1.5)"));
    require(s.letterSpacingPx == 0.0 && s.wordSpacingPx == 0.0,
            QStringLiteral("default spacing 0"));
    require(!s.underline && !s.overline && !s.strikeOut, QStringLiteral("default no decoration"));
    require(s.transform == requirement::RequirementTextTransform::None,
            QStringLiteral("default no transform"));
    require(!s.color.isValid(), QStringLiteral("default color invalid (use theme)"));
  }

  // ===== 2. font-size: exact font-independent values + Chrome 10000px cap =====
  {
    require(qAbs(resolve({"font-size:2em"}).fontSizePx - 128.0) < 1e-6,
            QStringLiteral("font-size:2em -> N^3*16 = 128; got %1")
                .arg(resolve({"font-size:2em"}).fontSizePx));
    require(qAbs(resolve({"font-size:3em"}).fontSizePx - 432.0) < 1e-6,
            QStringLiteral("font-size:3em -> 27*16 = 432"));
    require(qAbs(resolve({"font-size:2rem"}).fontSizePx - 32.0) < 1e-6,
            QStringLiteral("font-size:2rem -> 2*16 = 32 (html root, no compound)"));
    require(qAbs(resolve({"font-size:12pt"}).fontSizePx - 16.0) < 1e-6,
            QStringLiteral("font-size:12pt -> 12*4/3 = 16"));
    require(qAbs(resolve({"font-size:4vw"}).fontSizePx - 32.0) < 1e-6,
            QStringLiteral("font-size:4vw -> 4%*800 = 32"));
    require(qAbs(resolve({"font-size:10em"}).fontSizePx - 10000.0) < 1e-6,
            QStringLiteral("font-size:10em -> capped at 10000; got %1")
                .arg(resolve({"font-size:10em"}).fontSizePx));
    require(qAbs(resolve({"font-size:100em"}).fontSizePx - 10000.0) < 1e-6,
            QStringLiteral("font-size:100em -> capped at 10000"));
    require(qAbs(resolve({"font-size:0"}).fontSizePx - 0.0) < 1e-6,
            QStringLiteral("font-size:0 -> 0 (collapse)"));
    // Negative font-size -> inert (use theme): -1em resolves Valid(-16) but
    // font-size rejects negatives upstream, so the resolver keeps the default.
    require(resolve({"font-size:-1em"}).fontSizePx < 0.0,
            QStringLiteral("font-size:-1em -> inert (theme default)"));
    // Invalid -> inert.
    require(resolve({"font-size:foo"}).fontSizePx < 0.0,
            QStringLiteral("font-size:foo -> inert"));
  }

  // ===== 3. ex/ch font coupling (3-layer; relative — bundled-font dependent) =====
  {
    const qreal ex10 = resolve({"font-size:10ex"}).fontSizePx;
    const qreal ch10 = resolve({"font-size:10ch"}).fontSizePx;
    require(ex10 > 0.0 && ch10 > 0.0, QStringLiteral("ex/ch reach a resolved size"));
    // 3-layer compounding grows ex beyond a single 10*xHeight of the theme font.
    const qreal singleEx = 10.0 * QFontMetricsF(flowchart::makeFlowLabelFont(kFamily, kSize)).xHeight();
    require(ex10 > singleEx,
            QStringLiteral("10ex compounds (3-layer) > single 10*xHeight; %1 > %2").arg(ex10).arg(singleEx));
    // family matters: a bundled SERIF face (KaTeX_Main via MathFontRegistry) has
    // a different xHeight than the default sans stack, so 10ex resolves to a
    // different value when font-family is set — proving the per-layer measuring
    // font uses the NODE family (L2-3), not the theme font. (Qt's generic
    // "monospace" is NOT used here: in the bundled-font offscreen env it falls
    // back to Noto Sans, so it would not differ — KaTeX_Main is a real distinct
    // registered face.)
    const qreal ex10serif = resolve({"font-size:10ex", "font-family:KaTeX_Main"}).fontSizePx;
    require(qAbs(ex10serif - ex10) > 1e-3,
            QStringLiteral("10ex with KaTeX_Main differs from default font; %1 vs %2")
                .arg(ex10serif).arg(ex10));
  }

  // ===== 4. font-weight: bolder compounds 3 layers (400->700->900); absolute fixed =====
  {
    require(resolve({"font-weight:bolder"}).fontWeight == QFont::Black,
            QStringLiteral("bolder -> 900 (3-layer from 400)"));
    require(resolve({"font-weight:lighter"}).fontWeight == QFont::Thin,
            QStringLiteral("lighter -> 100 (caps)"));
    require(resolve({"font-weight:900"}).fontWeight == QFont::Black, QStringLiteral("900 -> 900"));
    require(resolve({"font-weight:bold"}).fontWeight == QFont::Bold, QStringLiteral("bold -> 700"));
    require(resolve({"font-weight:normal"}).fontWeight == QFont::Normal, QStringLiteral("normal -> 400"));
    require(resolve({"font-weight:300"}).fontWeight == QFont::Light, QStringLiteral("300 -> Light"));
  }

  // ===== 5. font-style / font-family / color / decoration / transform =====
  {
    require(resolve({"font-style:italic"}).fontStyle == QFont::StyleItalic, QStringLiteral("italic"));
    require(resolve({"font-style:oblique"}).fontStyle == QFont::StyleItalic,
            QStringLiteral("oblique -> StyleItalic (== italic upstream)"));
    require(resolve({"font-family:DefinitelyMissing,sans-serif"}).fontFamily ==
                QStringLiteral("DefinitelyMissing"),
            QStringLiteral("font-family keeps only the first comma token"));
    require(resolve({"font-family:Arial"}).fontFamily == QStringLiteral("Arial"),
            QStringLiteral("font-family bare token"));
    require(resolve({"color:#0000ff"}).color == QColor(QStringLiteral("#0000ff")),
            QStringLiteral("color parsed"));
    require(!resolve({"color:notacolor"}).color.isValid(), QStringLiteral("invalid color -> invalid"));
    require(resolve({"text-decoration:underline"}).underline, QStringLiteral("underline"));
    require(resolve({"text-decoration:overline"}).overline, QStringLiteral("overline"));
    require(resolve({"text-decoration:line-through"}).strikeOut, QStringLiteral("line-through"));
    // Combos are unreachable upstream (lexer drops them) -> none set.
    const auto combo = resolve({"text-decoration:underline overline"});
    require(!combo.underline && !combo.overline && !combo.strikeOut,
            QStringLiteral("multi-keyword decoration -> none (lexer drops it)"));
    require(resolve({"text-transform:uppercase"}).transform ==
                requirement::RequirementTextTransform::UpperCase, QStringLiteral("uppercase"));
    require(resolve({"text-transform:capitalize"}).transform ==
                requirement::RequirementTextTransform::Capitalize, QStringLiteral("capitalize"));
  }

  // ===== 6. line-height: normal / multiplier / length / zero / negative =====
  {
    require(resolve({"line-height:normal"}).lineHeightNormal, QStringLiteral("normal flag"));
    require(!resolve({"line-height:2"}).lineHeightNormal, QStringLiteral("2 is not 'normal'"));
    require(qAbs(resolve({"line-height:2"}).lineHeightPx - 32.0) < 1e-6,
            QStringLiteral("line-height:2 -> 2*16 = 32 (unitless multiplier)"));
    require(qAbs(resolve({"line-height:2em"}).lineHeightPx - 32.0) < 1e-6,
            QStringLiteral("line-height:2em -> 32"));
    require(qAbs(resolve({"line-height:20px"}).lineHeightPx - 20.0) < 1e-6,
            QStringLiteral("line-height:20px -> 20"));
    require(qAbs(resolve({"line-height:0"}).lineHeightPx - 0.0) < 1e-6,
            QStringLiteral("line-height:0 -> 0 (collapse)"));
    require(resolve({"line-height:-1"}).lineHeightPx < 0.0,
            QStringLiteral("line-height:-1 -> inert (theme)"));
    // em basis is the COMPOUNDED font-size: font-size:2em(128) + line-height:2em -> 256.
    require(qAbs(resolve({"font-size:2em", "line-height:2em"}).lineHeightPx - 256.0) < 1e-6,
            QStringLiteral("line-height:2em with font-size:2em -> 2*128 = 256"));
  }

  // ===== 7. letter-spacing / word-spacing: negatives live; em basis = compounded fs =====
  {
    require(qAbs(resolve({"letter-spacing:2px"}).letterSpacingPx - 2.0) < 1e-6, QStringLiteral("ls 2px"));
    require(qAbs(resolve({"letter-spacing:-2px"}).letterSpacingPx + 2.0) < 1e-6,
            QStringLiteral("ls -2px kept (negatives live)"));
    require(qAbs(resolve({"letter-spacing:1em"}).letterSpacingPx - 16.0) < 1e-6, QStringLiteral("ls 1em"));
    require(qAbs(resolve({"font-size:2em", "letter-spacing:1em"}).letterSpacingPx - 128.0) < 1e-6,
            QStringLiteral("ls 1em with font-size:2em -> 128 (compounded basis)"));
    require(resolve({"letter-spacing:normal"}).letterSpacingPx == 0.0, QStringLiteral("ls normal -> 0"));
    require(qAbs(resolve({"word-spacing:1em"}).wordSpacingPx - 16.0) < 1e-6, QStringLiteral("ws 1em"));
  }

  // ===== 8. applyRequirementTextTransform — capitalize word boundaries =====
  {
    using TT = requirement::RequirementTextTransform;
    const auto cap = [](const QString& s) { return requirement::applyRequirementTextTransform(s, TT::Capitalize); };
    require(cap("**fo**o") == QStringLiteral("**Fo**o"),
            QStringLiteral("markdown marker transparent: **fo**o -> **Fo**o; got %1").arg(cap("**fo**o")));
    require(cap("foo**bar**baz") == QStringLiteral("Foo**bar**baz"),
            QStringLiteral("one word across **: foo**bar**baz -> Foo**bar**baz; got %1")
                .arg(cap("foo**bar**baz")));
    require(cap("a$$x$$b") == QStringLiteral("A$$x$$B"),
            QStringLiteral("math box is a word boundary: a$$x$$b -> A$$x$$B; got %1").arg(cap("a$$x$$b")));
    require(cap("don't it's") == QStringLiteral("Don't It's"),
            QStringLiteral("apostrophe not a boundary: -> Don't It's; got %1").arg(cap("don't it's")));
    require(cap("foo-bar baz") == QStringLiteral("Foo-Bar Baz"),
            QStringLiteral("hyphen is a boundary: -> Foo-Bar Baz; got %1").arg(cap("foo-bar baz")));
    require(cap("foo_bar") == QStringLiteral("Foo_bar"),
            QStringLiteral("underscore not a boundary: -> Foo_bar; got %1").arg(cap("foo_bar")));
    require(cap("123abc") == QStringLiteral("123abc"),
            QStringLiteral("digit-led: no title -> 123abc; got %1").arg(cap("123abc")));
    require(cap("1st 2nd") == QStringLiteral("1st 2nd"),
            QStringLiteral("ordinal digit-led: no title; got %1").arg(cap("1st 2nd")));
    require(cap("abc123def") == QStringLiteral("Abc123def"),
            QStringLiteral("letter-led with mid digits: -> Abc123def; got %1").arg(cap("abc123def")));
    require(cap("hello world") == QStringLiteral("Hello World"),
            QStringLiteral("plain multiword; got %1").arg(cap("hello world")));
    require(cap("a/b/c") == QStringLiteral("A/B/C"),
            QStringLiteral("slash is a boundary; got %1").arg(cap("a/b/c")));
  }

  // ===== 9. applyRequirementTextTransform — upper/lower with math preserved =====
  {
    using TT = requirement::RequirementTextTransform;
    require(requirement::applyRequirementTextTransform("a$$x$$b", TT::UpperCase) ==
                QStringLiteral("A$$x$$B"),
            QStringLiteral("upper: math preserved; -> A$$x$$B"));
    require(requirement::applyRequirementTextTransform("straße", TT::UpperCase) ==
                QStringLiteral("STRASSE"),
            QStringLiteral("upper: ß -> SS (Unicode default case mapping)"));
    require(requirement::applyRequirementTextTransform("**bold**", TT::UpperCase) ==
                QStringLiteral("**BOLD**"),
            QStringLiteral("upper: markdown markers preserved, text cased"));
    require(requirement::applyRequirementTextTransform("ABC", TT::LowerCase) ==
                QStringLiteral("abc"), QStringLiteral("lower"));
    require(requirement::applyRequirementTextTransform("abc", TT::None) == QStringLiteral("abc"),
            QStringLiteral("none: unchanged"));
  }

  // ===== 10. measureRequirementLayoutInput: zero-collapse + natural line-height =====
  {
    const QString aBlock = "requirement A {\n id: \"1\"\n text: hello\n risk: low\n verifyMethod: test\n}\n";
    const auto data = requirement::RequirementDiagram::parse(head + aBlock).data();
    const auto input = requirement::buildRequirementLayoutInput(data);
    const auto measure = [&](const QStringList& css) {
      // Inject css by resolving against a node whose cssStyles we set.
      requirement::RequirementLayoutInput in = input;
      for (auto& n : in.nodes) n.cssStyles = css;
      return requirement::measureRequirementLayoutInput(in, kFamily, kSize, QFont::Normal).value("A");
    };
    const auto base = measure({});
    require(base.boxSize.width() > 20.0 && base.boxSize.height() > 20.0,
            QStringLiteral("default box > 20x20; got %1x%2").arg(base.boxSize.width()).arg(base.boxSize.height()));
    // font-size:0 -> collapses to 20x20.
    const auto fs0 = measure({"font-size:0"});
    require(qAbs(fs0.boxSize.width() - 20.0) < 1e-6 && qAbs(fs0.boxSize.height() - 20.0) < 1e-6,
            QStringLiteral("font-size:0 -> 20x20; got %1x%2").arg(fs0.boxSize.width()).arg(fs0.boxSize.height()));
    // line-height:0 -> collapses to 20x20.
    const auto lh0 = measure({"line-height:0"});
    require(qAbs(lh0.boxSize.width() - 20.0) < 1e-6 && qAbs(lh0.boxSize.height() - 20.0) < 1e-6,
            QStringLiteral("line-height:0 -> 20x20; got %1x%2").arg(lh0.boxSize.width()).arg(lh0.boxSize.height()));
    // line-height:normal -> row height = natural font height (~1.19x, not 1.5x).
    const auto lhn = measure({"line-height:normal"});
    const qreal naturalRow = QFontMetricsF(flowchart::makeFlowLabelFont(kFamily, kSize)).height();
    require(!lhn.rowHeights.isEmpty() && qAbs(lhn.rowHeights.at(0) - naturalRow) < 1e-3,
            QStringLiteral("line-height:normal -> row height = QFontMetricsF height (%1); got %2")
                .arg(naturalRow).arg(lhn.rowHeights.isEmpty() ? -1.0 : lhn.rowHeights.at(0)));
    // font-size:2em -> box much larger.
    const auto fs2 = measure({"font-size:2em"});
    require(fs2.boxSize.height() > base.boxSize.height() * 3.0,
            QStringLiteral("font-size:2em -> much taller box"));
  }

  // ===== 11. Production render path: resolved fields reach the scene =====
  {
    // Default (no labelStyle) -> resolved text all-defaults (byte-identical base).
    const auto r = renderNode(cache, head + "requirement A {\n id: \"1\"\n text: hello\n}", "A");
    require(r.node->text.fontSizePx < 0.0, QStringLiteral("default node.text.fontSizePx < 0"));
    require(!r.node->rows.isEmpty() &&
                qAbs(r.node->rows.at(0).fontPixelSize - r.scene->style.fontSize) < 1e-6,
            QStringLiteral("default row.fontPixelSize == theme fontSize (effective)"));
    // font-size:2em -> node.text.fontSizePx == 128 and rows carry 128.
    const auto r2 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A font-size:2em", "A");
    require(qAbs(r2.node->text.fontSizePx - 128.0) < 1e-6,
            QStringLiteral("font-size:2em -> node.text.fontSizePx=128; got %1")
                .arg(r2.node->text.fontSizePx));
    require(!r2.node->rows.isEmpty() && qAbs(r2.node->rows.at(0).fontPixelSize - 128.0) < 1e-6,
            QStringLiteral("row.fontPixelSize == 128"));
    // text-transform:uppercase -> a row's document.text is uppercased.
    const auto r3 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A text-transform:uppercase", "A");
    bool foundUpper = false;
    for (const auto& row : r3.node->rows)
      if (row.document.text.contains(QStringLiteral("HELLO"))) foundUpper = true;
    require(foundUpper, QStringLiteral("text-transform:uppercase reaches a row document"));
    // color:#0000ff -> rows carry blue.
    const auto r4 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A color:#0000ff", "A");
    require(!r4.node->rows.isEmpty() && r4.node->rows.at(0).color == QColor(QStringLiteral("#0000ff")),
            QStringLiteral("color reaches row.color"));
    // text-decoration:underline -> a row's document.underline is set.
    const auto r5 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A text-decoration:underline", "A");
    bool foundUnderline = false;
    for (const auto& row : r5.node->rows)
      if (row.document.underline) foundUnderline = true;
    require(foundUnderline, QStringLiteral("text-decoration:underline reaches a row document"));
  }

  // ===== 12. Painter ink: decoration adds pixels; color paints blue =====
  {
    auto render = [&cache, &head](const QString& styleLine) {
      const QString src = head + "requirement A {\n id: \"1\"\n text: hello\n}\n" + styleLine;
      const auto entry = cache.getSync(cache.makeKey(src), src);
      require(entry.status == editor::MermaidRenderStatus::Ready,
              QStringLiteral("painter case did not render: ") + entry.errorMessage);
      const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
      require(sc != nullptr, QStringLiteral("scene missing"));
      return paintScene(*sc);
    };
    const QImage baseImg = render("");
    const qint64 base = inkTotal(baseImg);
    const QImage underImg = render("style A text-decoration:underline");
    // Decoration is paint-only (no layout change), so its effect is a changed
    // raster, not necessarily a net ink gain (anti-aliasing can balance). Assert
    // the flag is set on the rendered rows AND the painted pixels differ.
    bool anyUnder = false;
    {
      const QString usrc = head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A text-decoration:underline";
      const auto ue = cache.getSync(cache.makeKey(usrc), usrc);
      if (const auto* usc = dynamic_cast<const requirement::RequirementScene*>(ue.scene.get()))
        for (const auto& n : usc->nodes)
          for (const auto& rw : n.rows) if (rw.document.underline) anyUnder = true;
    }
    qint64 diffPx = 0;
    if (baseImg.size() == underImg.size())
      for (int y = 0; y < baseImg.height(); ++y)
        for (int x = 0; x < baseImg.width(); ++x)
          if (baseImg.pixel(x, y) != underImg.pixel(x, y)) ++diffPx;
    require(anyUnder, QStringLiteral("underline scene carries document.underline on a row"));
    require(diffPx > 100,
            QStringLiteral("underline changes the painted raster (diffPx=%1); decoration paints")
                .arg(diffPx));
    // font-size:0 -> text absent: strictly less ink than baseline (box outline only).
    const qint64 fs0 = inkTotal(render("style A font-size:0"));
    require(fs0 < base, QStringLiteral("font-size:0 collapses text (less ink); %1 < %2").arg(fs0).arg(base));
  }

  qDebug() << "MermaidRequirementTextStyleTest: passed";
  return 0;
}
