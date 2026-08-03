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
#include <QMessageLogContext>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
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

// Paint a single FlowLabel — the production decoration path
// (RequirementScenePainter::paintRow -> flowchart::paintFlowLabel) — into a
// transparent image at a KNOWN rect, for geometric inspection of the painted
// text-decoration band (Y, thickness, horizontal span).
QImage paintLabelImage(const flowchart::FlowLabelDocument& doc, const QRectF& rect,
                       const QString& family, qreal size, qreal lineH) {
  QImage img(QSize(static_cast<int>(std::ceil(rect.width())) + 2,
                   static_cast<int>(std::ceil(rect.height())) + 2),
             QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  flowchart::paintFlowLabel(p, doc, rect, family, size, lineH, QColor(Qt::black), true);
  p.end();
  return img;
}

inline bool isInk(QRgb pixel) { return qAlpha(pixel) > 32; }

// A detected horizontal decoration band: the rows where `decorated` has ink that
// `base` does not. Because the text glyphs are identical between the two, the
// diff isolates the decoration-only pixels (the parts of the line not hidden
// under existing glyph ink), exposing its Y, thickness and horizontal extent so
// a wrong Y, wrong thickness, or a line drawn on the wrong visual line fails.
struct DecoBand {
  bool found = false;
  int yMin = 0, yMax = 0;  // band row range (thickness = yMax - yMin + 1)
  int xMin = 0, xMax = 0;  // horizontal extent across the band rows
  qreal centerY() const { return (yMin + yMax) / 2.0; }
};
DecoBand decorationBand(const QImage& base, const QImage& decorated) {
  DecoBand band;
  const int h = std::min(base.height(), decorated.height());
  const int w = std::min(base.width(), decorated.width());
  QVector<qint64> perRow(h, 0);
  for (int y = 0; y < h; ++y) {
    const QRgb* b = reinterpret_cast<const QRgb*>(base.constScanLine(y));
    const QRgb* d = reinterpret_cast<const QRgb*>(decorated.constScanLine(y));
    qint64 c = 0;
    for (int x = 0; x < w; ++x)
      if (isInk(d[x]) && !isInk(b[x])) ++c;
    perRow[y] = c;
  }
  qint64 maxCount = 0;
  for (qint64 c : perRow) maxCount = std::max(maxCount, c);
  if (maxCount < 8) return band;  // no substantial decoration ink
  band.found = true;
  band.yMin = h;
  band.yMax = 0;
  band.xMin = w;
  band.xMax = 0;
  for (int y = 0; y < h; ++y) {
    if (perRow[y] * 2 < maxCount) continue;  // not a dense band row
    band.yMin = std::min(band.yMin, y);
    band.yMax = std::max(band.yMax, y);
    const QRgb* b = reinterpret_cast<const QRgb*>(base.constScanLine(y));
    const QRgb* d = reinterpret_cast<const QRgb*>(decorated.constScanLine(y));
    for (int x = 0; x < w; ++x)
      if (isInk(d[x]) && !isInk(b[x])) {
        band.xMin = std::min(band.xMin, x);
        band.xMax = std::max(band.xMax, x);
      }
  }
  return band;
}

// Longest continuous inked run in a single image row (used to prove a painted
// decoration fillRect is one solid line across the full advance, including an
// inline Math region — the diff-based band can't see decoration painted OVER
// glyphs, so continuity is checked on the painted row directly).
int longestInkRun(const QImage& img, int y) {
  if (y < 0 || y >= img.height()) return 0;
  const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
  int best = 0, run = 0;
  for (int x = 0; x < img.width(); ++x) {
    if (isInk(row[x])) {
      ++run;
      best = std::max(best, run);
    } else {
      run = 0;
    }
  }
  return best;
}

// Vertical extent of the text ink in an image (first/last row holding any ink).
// Used to anchor a decoration's Y to the TEXT INK center: the same glyphs render
// in both Muffin and the Chrome oracle, so the ink center is a common reference
// that cancels per-renderer vertical centering. Only referenced by the Windows
// Chrome-oracle section, hence [[maybe_unused]] (compiled out elsewhere).
struct InkExtent { int yMin; int yMax; bool any; };
[[maybe_unused]] InkExtent inkExtentOf(const QImage& img) {
  InkExtent e{img.height(), -1, false};
  for (int y = 0; y < img.height(); ++y) {
    const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x)
      if (isInk(row[x])) {
        e.any = true;
        e.yMin = std::min(e.yMin, y);
        e.yMax = std::max(e.yMax, y);
        break;
      }
  }
  return e;
}

// Captures Qt "Pixel size <= 0" warnings while installed: a zero-font resolution
// path must never reach QFont::setPixelSize(0) (Qt rejects non-positive pixel
// sizes; upstream likewise skips font build/measure/paint for font-size:0).
QVector<QString> g_pixelSizeWarnings;
void pixelSizeCatcher(QtMsgType type, const QMessageLogContext&, const QString& msg) {
  if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) &&
      msg.contains(QStringLiteral("Pixel size")))
    g_pixelSizeWarnings.append(msg);
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
    // fontWeightResolved distinguishes a VALID declaration (suppresses the name
    // row's default bold) from unset/invalid. A present-but-invalid value (e.g.
    // "foo") is inert upstream -> inherit the reqTitle default bold, so it is NOT
    // "resolved" even though the key is present.
    require(resolve({"font-weight:100"}).fontWeightResolved, QStringLiteral("100 -> resolved"));
    require(resolve({"font-weight:bolder"}).fontWeightResolved, QStringLiteral("bolder -> resolved"));
    require(resolve({"font-weight:normal"}).fontWeightResolved, QStringLiteral("normal -> resolved"));
    require(!resolve({"font-weight:foo"}).fontWeightResolved,
            QStringLiteral("invalid font-weight -> not resolved (inert)"));
    require(!resolve({}).fontWeightResolved, QStringLiteral("no font-weight -> not resolved"));
    // CSS-wide keywords are VALID declarations (probed vs mermaid 11.16.0:
    // inherit/initial/unset/revert/revert-layer all resolve to 400 in the default
    // theme and suppress the name row's default bold). Only a garbage value stays
    // unresolved. resolveWeightOne: inherit/unset take the parent (Normal here);
    // initial/revert/revert-layer -> Normal. 1e2 == 100 (scientific numeric).
    require(resolve({"font-weight:inherit"}).fontWeightResolved, QStringLiteral("inherit -> resolved"));
    require(resolve({"font-weight:initial"}).fontWeightResolved, QStringLiteral("initial -> resolved"));
    require(resolve({"font-weight:unset"}).fontWeightResolved, QStringLiteral("unset -> resolved"));
    require(resolve({"font-weight:revert"}).fontWeightResolved, QStringLiteral("revert -> resolved"));
    require(resolve({"font-weight:revert-layer"}).fontWeightResolved,
            QStringLiteral("revert-layer -> resolved"));
    require(resolve({"font-weight:inherit"}).fontWeight == QFont::Normal,
            QStringLiteral("inherit -> Normal (parent is Normal)"));
    require(resolve({"font-weight:initial"}).fontWeight == QFont::Normal,
            QStringLiteral("initial -> Normal"));
    require(resolve({"font-weight:revert-layer"}).fontWeight == QFont::Normal,
            QStringLiteral("revert-layer -> Normal"));
    require(resolve({"font-weight:1e2"}).fontWeight == QFont::Thin,
            QStringLiteral("1e2 -> 100 (Thin)"));
    require(resolve({"font-weight:1e2"}).fontWeightResolved, QStringLiteral("1e2 -> resolved"));
  }

  // ===== 4b. name-row default bold vs declared font-weight (probe: font-weight
  //       wins on BOTH name and body rows; default bold only when unset/invalid) =====
  {
    using Doc = flowchart::FlowLabelDocument;
    const auto hasBoldRange = [](const Doc& d) {
      for (const auto& f : d.formats)
        if (f.format.fontWeight() >= QFont::Bold) return true;
      return false;
    };
    // Default (no font-weight): name row (bold=true) gets the reqTitle bold range.
    const Doc nameDefault =
        requirement::requirementRowDocument(QStringLiteral("A"), true,
                                            requirement::RequirementTextStyle{}, kSize);
    require(nameDefault.baseWeight == QFont::Normal,
            QStringLiteral("default name row baseWeight Normal"));
    require(hasBoldRange(nameDefault),
            QStringLiteral("default name row applies the reqTitle bold range"));
    // Declared valid font-weight (100): the name row uses it; NO default-bold range.
    requirement::RequirementTextStyle s100;
    s100.fontWeight = QFont::Thin;
    s100.fontWeightResolved = true;
    const Doc name100 =
        requirement::requirementRowDocument(QStringLiteral("A"), true, s100, kSize);
    require(name100.baseWeight == QFont::Thin,
            QStringLiteral("font-weight:100 -> name row baseWeight Thin(100)"));
    require(!hasBoldRange(name100),
            QStringLiteral("font-weight:100 suppresses the name-row default bold"));
    // bolder (->900): same — the resolved weight stands, no bold override to 700.
    requirement::RequirementTextStyle sBolder;
    sBolder.fontWeight = QFont::Black;
    sBolder.fontWeightResolved = true;
    const Doc nameBolder =
        requirement::requirementRowDocument(QStringLiteral("A"), true, sBolder, kSize);
    require(nameBolder.baseWeight == QFont::Black && !hasBoldRange(nameBolder),
            QStringLiteral("font-weight:bolder -> name row Black, no default-bold override"));
    // A body row (bold=false) never gets the range regardless.
    const Doc bodyDefault =
        requirement::requirementRowDocument(QStringLiteral("Text: hi"), false,
                                            requirement::RequirementTextStyle{}, kSize);
    require(!hasBoldRange(bodyDefault), QStringLiteral("body row never bold by default"));
    // Production path (resolveRequirementTextStyle -> requirementRowDocument), not
    // hand-built styles: a VALID font-weight declaration — including the CSS-wide
    // keywords (probe: inherit/initial/unset/revert/revert-layer all -> 400) and a
    // numeric — suppresses the name row's default bold on BOTH rows; only a truly
    // invalid value ("foo") or an unset keeps the reqTitle default bold.
    const auto checkNameBold = [&](const QStringList& css, bool expectBoldRange,
                                   QFont::Weight expectWeight, const QString& label) {
      const requirement::RequirementTextStyle s = requirement::resolveRequirementTextStyle(
          css, kFamily, kSize, QFont::Normal, kLine);
      require(s.fontWeight == expectWeight,
              QStringLiteral("%1: resolved weight %2 == %3").arg(label).arg(s.fontWeight).arg(expectWeight));
      require(s.fontWeightResolved == !expectBoldRange,
              QStringLiteral("%1: fontWeightResolved matches bold expectation").arg(label));
      const Doc nameDoc = requirement::requirementRowDocument(QStringLiteral("A"), true, s, kSize);
      require(nameDoc.baseWeight == expectWeight,
              QStringLiteral("%1: name row baseWeight %2 == %3").arg(label).arg(nameDoc.baseWeight).arg(expectWeight));
      if (expectBoldRange)
        require(hasBoldRange(nameDoc), QStringLiteral("%1: name row keeps default bold").arg(label));
      else
        require(!hasBoldRange(nameDoc), QStringLiteral("%1: name row suppresses default bold").arg(label));
      const Doc bodyDoc = requirement::requirementRowDocument(QStringLiteral("x"), false, s, kSize);
      require(!hasBoldRange(bodyDoc), QStringLiteral("%1: body row never bold").arg(label));
    };
    checkNameBold({}, true, QFont::Normal, QStringLiteral("unset"));
    checkNameBold({"font-weight:foo"}, true, QFont::Normal, QStringLiteral("foo"));
    checkNameBold({"font-weight:inherit"}, false, QFont::Normal, QStringLiteral("inherit"));
    checkNameBold({"font-weight:initial"}, false, QFont::Normal, QStringLiteral("initial"));
    checkNameBold({"font-weight:revert-layer"}, false, QFont::Normal, QStringLiteral("revert-layer"));
    checkNameBold({"font-weight:100"}, false, QFont::Thin, QStringLiteral("100"));
    checkNameBold({"font-weight:1e2"}, false, QFont::Thin, QStringLiteral("1e2"));
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
    // Scientific-notation unitless values are MULTIPLIERS (the exponent 'e' is
    // NOT a unit): 1e1 == 10 x fs, 1e-1 == 0.1 x fs; with a unit they are lengths.
    // (A naive "contains no letter" check mistook the 'e' for a unit and parsed
    // both 1e1 and 1e1px as 10px.)
    require(qAbs(resolve({"line-height:1e1"}).lineHeightPx - 160.0) < 1e-6,
            QStringLiteral("line-height:1e1 -> 10*16 = 160 (unitless multiplier); got %1")
                .arg(resolve({"line-height:1e1"}).lineHeightPx));
    require(qAbs(resolve({"line-height:1e-1"}).lineHeightPx - 1.6) < 1e-6,
            QStringLiteral("line-height:1e-1 -> 0.1*16 = 1.6; got %1")
                .arg(resolve({"line-height:1e-1"}).lineHeightPx));
    require(qAbs(resolve({"line-height:2e0"}).lineHeightPx - 32.0) < 1e-6,
            QStringLiteral("line-height:2e0 -> 2*16 = 32"));
    require(qAbs(resolve({"line-height:1e1px"}).lineHeightPx - 10.0) < 1e-6,
            QStringLiteral("line-height:1e1px -> 10 (length, NOT multiplier); got %1")
                .arg(resolve({"line-height:1e1px"}).lineHeightPx));
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
    // font-size:0 / line-height:0 -> the node collapses to the 20x20 min box and
    // every scene row is ZERO-size with NO document (a 0px QFont is never built,
    // matching upstream's skip-font/measure/paint — STEP0F §2).
    const auto rFs0 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A font-size:0", "A");
    require(qAbs(rFs0.node->size.width() - 20.0) < 1e-6 &&
                qAbs(rFs0.node->size.height() - 20.0) < 1e-6,
            QStringLiteral("font-size:0 -> 20x20 box; got %1x%2")
                .arg(rFs0.node->size.width()).arg(rFs0.node->size.height()));
    require(!rFs0.node->rows.isEmpty(), QStringLiteral("font-size:0 node still has rows"));
    for (const auto& row : rFs0.node->rows) {
      require(row.size.width() == 0.0 && row.size.height() == 0.0,
              QStringLiteral("font-size:0 -> row zero-size; got %1x%2")
                  .arg(row.size.width()).arg(row.size.height()));
      require(row.document.text.isEmpty(),
              QStringLiteral("font-size:0 -> no document built (no 0px QFont)"));
    }
    const auto rLh0 = renderNode(cache,
        head + "requirement A {\n id: \"1\"\n text: hello\n}\nstyle A line-height:0", "A");
    require(!rLh0.node->rows.isEmpty(), QStringLiteral("line-height:0 node still has rows"));
    for (const auto& row : rLh0.node->rows)
      require(row.size.width() == 0.0 && row.size.height() == 0.0,
              QStringLiteral("line-height:0 -> row zero-size; got %1x%2")
                  .arg(row.size.width()).arg(row.size.height()));
  }

  // ===== 12. Painter ink: font-size:0 collapses text end-to-end =====
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
    const qint64 base = inkTotal(render(""));
    // font-size:0 -> text absent: strictly less ink than baseline (box outline only).
    const qint64 fs0 = inkTotal(render("style A font-size:0"));
    require(fs0 < base, QStringLiteral("font-size:0 collapses text (less ink); %1 < %2").arg(fs0).arg(base));
  }

  // ===== 13. Decoration geometry: Y (font-metric), thickness, span, Math continuity =====
  //   Replaces a weak "decoration changes >100 pixels" check. Each decoration is
  //   painted via the production path (paintFlowLabel) at a known rect; diffing
  //   against the no-decoration render isolates the decoration-only ink, then we
  //   constrain its Y (== QFontMetricsF underlinePos/overlinePos/strikeOutPos),
  //   thickness (== lineWidth), horizontal span (== text advance, centered), and
  //   prove the three decorations sit at distinct, ordered Y bands. A wrong Y, a
  //   wrong thickness, or a line drawn on the wrong visual line fails here.
  {
    using flowchart::FlowLabelDocument;
    const QString family = kFamily;
    constexpr qreal size = 16.0;
    constexpr qreal lineH = 24.0;
    const QRectF rect(0.0, 0.0, 160.0, lineH);

    auto makeDoc = [&](const QString& text) {
      FlowLabelDocument d = flowchart::parseFlowLabel(text, QStringLiteral("markdown"), true);
      d.formattingContext = flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
      return d;
    };
    auto paint = [&](const FlowLabelDocument& d) {
      return paintLabelImage(d, rect, family, size, lineH);
    };

    const QFont font = flowchart::makeFlowLabelFont(family, size);
    const QFontMetricsF fm(font);
    const QString word = QStringLiteral("hello hello");  // no descenders -> clean bands
    const FlowLabelDocument baseDoc = makeDoc(word);
    const QImage baseImg = paint(baseDoc);
    // Predicted baseline from the SAME layout paintFlowLabel uses internally; the
    // single centered line has lineTop == rect.top() (== 0), so each decoration's
    // center Y == baseline +/- the QFontMetricsF position.
    const qreal baseline =
        flowchart::layoutFlowLabel(baseDoc, family, size, lineH).lines.at(0).baseline;
    const qreal textWidth =
        flowchart::layoutFlowLabel(baseDoc, family, size, lineH).lines.at(0).width;
    const int expectedThick = std::max(1, static_cast<int>(std::round(fm.lineWidth())));

    struct Kind {
      QString name;
      bool FlowLabelDocument::* flag;
      qreal predictedY;
    };
    const Kind kinds[] = {
        {QStringLiteral("underline"), &FlowLabelDocument::underline, baseline + fm.underlinePos()},
        {QStringLiteral("overline"), &FlowLabelDocument::overline, baseline - fm.overlinePos()},
        {QStringLiteral("strikeOut"), &FlowLabelDocument::strikeOut, baseline - fm.strikeOutPos()},
    };

    DecoBand bands[3];
    int thicknesses[3];
    for (int k = 0; k < 3; ++k) {
      FlowLabelDocument d = makeDoc(word);
      (d.*kinds[k].flag) = true;
      const DecoBand band = decorationBand(baseImg, paint(d));
      require(band.found,
              QStringLiteral("%1: decoration paints a band").arg(kinds[k].name));
      const int thickness = band.yMax - band.yMin + 1;
      require(thickness >= expectedThick && thickness <= expectedThick + 1,
              QStringLiteral("%1: thickness %2 ~ lineWidth(%3); got [%4,%5]")
                  .arg(kinds[k].name).arg(thickness).arg(expectedThick).arg(band.yMin).arg(band.yMax));
      require(std::abs(band.centerY() - kinds[k].predictedY) < 2.0,
              QStringLiteral("%1: center Y %2 ~ predicted %3").arg(kinds[k].name)
                  .arg(band.centerY()).arg(kinds[k].predictedY));
      bands[k] = band;
      thicknesses[k] = thickness;
    }
    // Distinct, ordered bands: overline (top) < strikeOut (mid) < underline (bottom).
    require(bands[1].centerY() < bands[2].centerY() && bands[2].centerY() < bands[0].centerY(),
            QStringLiteral("overline(%1) < strikeOut(%2) < underline(%3) Y")
                .arg(bands[1].centerY()).arg(bands[2].centerY()).arg(bands[0].centerY()));
    // Horizontal span ~= the full text advance (the underline is one line over
    // the whole row, not a truncated/wrong width), centered in the rect.
    const qreal span = bands[0].xMax - bands[0].xMin + 1;
    require(span >= textWidth - 14.0 && span <= textWidth + 2.0,
            QStringLiteral("underline span %1 ~ text advance %2").arg(span).arg(textWidth));
    const qreal mid = (bands[0].xMin + bands[0].xMax) / 2.0;
    require(std::abs(mid - (rect.left() + rect.width() / 2.0)) < 6.0,
            QStringLiteral("underline centered in rect (mid %1 ~ %2)")
                .arg(mid).arg(rect.left() + rect.width() / 2.0));

    // Math continuity: the underline is one continuous fillRect across the inline
    // Math region. The decoration is painted AFTER the glyphs (on top of them), so
    // a diff can't see the portion over a Math glyph — instead check the PAINTED
    // row directly: at the decoration's Y the fillRect makes the whole advance
    // (text + Math) one solid inked run. A per-run decoration (broken at the Math
    // span) would fragment that run.
    {
      const QString math = QStringLiteral("a $$x$$ b");
      const FlowLabelDocument mathBase = makeDoc(math);
      const QImage mathBaseImg = paint(mathBase);
      FlowLabelDocument d = makeDoc(math);
      d.underline = true;
      const QImage painted = paint(d);
      const DecoBand band = decorationBand(mathBaseImg, painted);
      require(band.found, QStringLiteral("underline paints over the Math line"));
      const qreal mathAdvance =
          flowchart::layoutFlowLabel(mathBase, family, size, lineH).lines.at(0).width;
      const int bestRun = longestInkRun(painted, (band.yMin + band.yMax) / 2);
      require(bestRun >= mathAdvance - 6.0,
              QStringLiteral("underline continuous across Math (run %1 >= advance %2)")
                  .arg(bestRun).arg(mathAdvance));
    }
  }

  // ===== 14. Zero-font never builds a 0px QFont (Qt rejects Pixel size <= 0) =====
  //   font-size:0 / line-height:0 collapse the node (STEP0F §2). The resolver and
  //   the measure path must short-circuit BEFORE any QFont construction, so Qt never
  //   warns "Pixel size <= 0 (0)" — Codex observed 12 such warnings leaking through
  //   the prior code. Covers font-size:0 alone / with line-height:normal / with a
  //   length line-height / with spacing / with font-weight, plus the measure path.
  {
    g_pixelSizeWarnings.clear();
    QtMessageHandler prev = qInstallMessageHandler(pixelSizeCatcher);
    resolve({"font-size:0"});
    resolve({"font-size:0", "line-height:normal"});
    resolve({"font-size:0", "line-height:1em"});
    resolve({"font-size:0", "line-height:2em", "letter-spacing:1em", "word-spacing:1em"});
    resolve({"font-size:0", "font-weight:bolder"});
    resolve({"line-height:0"});
    // The measure path builds a natural-height QFont for line-height:normal; it
    // must short-circuit on font-size:0 before that, and collapse on line-height:0.
    const QString src = head + "requirement A {\n id: \"1\"\n text: hello\n}\n";
    const auto baseInput = requirement::buildRequirementLayoutInput(
        requirement::RequirementDiagram::parse(src).data());
    auto measureWith = [&](const QStringList& css) {
      auto in = baseInput;
      for (auto& n : in.nodes) n.cssStyles = css;
      requirement::measureRequirementLayoutInput(in, kFamily, kSize, QFont::Normal);
    };
    measureWith({QStringLiteral("font-size:0"), QStringLiteral("line-height:normal")});
    measureWith({QStringLiteral("line-height:0")});
    qInstallMessageHandler(prev);
    require(g_pixelSizeWarnings.isEmpty(),
            QStringLiteral("zero-font path emitted a Pixel-size warning: %1")
                .arg(g_pixelSizeWarnings.isEmpty() ? QString() : g_pixelSizeWarnings.first()));
  }

  // ===== 15. Decoration geometry vs a CHROME-derived oracle (not QFontMetricsF) =====
  //   The prior geometric check (section 13) predicted Y from QFontMetricsF — the
  //   same API the painter uses — so it was self-proving (Codex review #3). These
  //   expectations come instead from headless Chrome rendering the SAME "hello
  //   hello" in the SAME bundled Noto Sans at 16px, no hinting
  //   (G:/github/req-probe/step0f-decoration-geometry.mjs ->
  //   step0f-decoration-geometry-report.json):
  //     underline   thickness 1.00px,  +7.50px from the text ink center
  //     overline    thickness 1.00px, -11.50px from the text ink center
  //     line-through thickness 1.00px,  +0.50px from the text ink center
  //   Y is anchored to the TEXT INK center (identical glyphs in both renderers) so
  //   vertical-centering differences cancel, making Muffin's image-px delta directly
  //   comparable to Chrome's CSS-px delta.
  //
  //   Platform scope: Muffin paints the decoration at QFontMetricsF positions, which
  //   Qt derives per font backend (DirectWrite on Windows, FreeType on Linux,
  //   CoreText on macOS). The probe ran Chrome on Windows, so both renderers share
  //   the DirectWrite backend there and agree to <=0.5px. On Linux/macOS the font
  //   backend differs, so the Windows-Chrome constants are not asserted there;
  //   cross-platform self-consistency (painter == QFontMetricsF) is still covered by
  //   section 13. Extending the Chrome oracle to CI's Linux leg needs a Linux-Chrome
  //   probe (future work). Verified on Windows: diffs 0.50/0.50/0.00px.
#if defined(Q_OS_WIN)
  {
    using flowchart::FlowLabelDocument;
    const QString family = kFamily;
    constexpr qreal size = 16.0;
    constexpr qreal lineH = 24.0;
    const QRectF rect(0.0, 0.0, 200.0, lineH);
    auto makeDoc = [&](const QString& text) {
      FlowLabelDocument d = flowchart::parseFlowLabel(text, QStringLiteral("markdown"), true);
      d.formattingContext = flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
      return d;
    };
    auto paint = [&](const FlowLabelDocument& d) {
      return paintLabelImage(d, rect, family, size, lineH);
    };
    const QString word = QStringLiteral("hello hello");
    const QImage baseImg = paint(makeDoc(word));
    const InkExtent ink = inkExtentOf(baseImg);
    require(ink.any, QStringLiteral("baseline 'hello hello' has text ink"));
    const qreal inkCenter = (ink.yMin + ink.yMax) / 2.0;

    struct ChromeOracle { QString name; bool FlowLabelDocument::* flag; qreal thickness; qreal delta; };
    const ChromeOracle oracle[] = {
        {QStringLiteral("underline"), &FlowLabelDocument::underline, 1.0, 7.5},
        {QStringLiteral("overline"), &FlowLabelDocument::overline, 1.0, -11.5},
        {QStringLiteral("line-through"), &FlowLabelDocument::strikeOut, 1.0, 0.5},
    };
    for (const ChromeOracle& o : oracle) {
      FlowLabelDocument d = makeDoc(word);
      (d.*o.flag) = true;
      const DecoBand band = decorationBand(baseImg, paint(d));
      require(band.found, QStringLiteral("%1: Chrome-oracle decoration band present").arg(o.name));
      const qreal thickness = band.yMax - band.yMin + 1;
      const qreal delta = band.centerY() - inkCenter;
      // Thickness: Chrome draws a 1px line; Muffin's dense band is 1 (exact) to 2
      // (anti-aliasing fuzz) image-px at 1:1.
      require(thickness >= o.thickness && thickness <= o.thickness + 1.0,
              QStringLiteral("%1: thickness %2 ~ Chrome %3").arg(o.name).arg(thickness).arg(o.thickness));
      // Y offset from the text ink center, within an anti-aliasing/sub-pixel budget.
      require(std::abs(delta - o.delta) <= 2.5,
              QStringLiteral("%1: delta-from-ink-center %2 ~ Chrome %3 (diff %4)")
                  .arg(o.name).arg(delta).arg(o.delta).arg(delta - o.delta));
    }
  }
#endif  // Q_OS_WIN (Chrome-oracle section 15)

  qDebug() << "MermaidRequirementTextStyleTest: passed";
  return 0;
}
