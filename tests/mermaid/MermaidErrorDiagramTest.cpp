// Native parity test for the upstream "error" diagram surface (11.16.0):
// the lightbulb fallback SVG mermaid.core renders for literal "error" and
// for every parse/detector failure, plus the "---" frontmatter guard.
//
// Oracle: tests/fixtures/mermaid/error-diagram.json (generator:
// scripts/generate_mermaid_error_diagram_fixtures.mjs).

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/error/ErrorScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;
using namespace muffin::mermaid::editor;
namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) { if (!value) fail(message); }

QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

QColor cssColor(const QString& value) {
  const auto paint = color::resolveSvgPaint(value, color::SvgPaintKind::Fill, Qt::black);
  return paint.none ? QColor() : paint.color;
}

bool colorNear(const QColor& actual, const QString& expected, int tolerance = 1) {
  const QColor reference = cssColor(expected);
  return std::abs(actual.red() - reference.red()) <= tolerance &&
         std::abs(actual.green() - reference.green()) <= tolerance &&
         std::abs(actual.blue() - reference.blue()) <= tolerance &&
         actual.alpha() == reference.alpha();
}

bool near(qreal a, qreal b, qreal tolerance) { return std::abs(a - b) <= tolerance; }

QImage nativePng(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1).dataUrl;
  QImage image;
  image.loadFromData(QByteArray::fromBase64(
      dataUrl.mid(dataUrl.indexOf(QLatin1Char(',')) + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}

qreal alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0;
  qint64 unionArea = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool lhs = actual.pixelColor(x, y).alpha() >= 32;
      const bool rhs = expected.pixelColor(x, y).alpha() >= 32;
      intersection += lhs && rhs;
      unionArea += lhs || rhs;
    }
  return unionArea ? qreal(intersection) / unionArea : 1.0;
}

qreal rgbaScore(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor lhs = actual.pixelColor(x, y);
      const QColor rhs = expected.pixelColor(x, y);
      if (lhs.alpha() < 16 && rhs.alpha() < 16) continue;
      difference += std::abs(lhs.red() * lhs.alpha() / 255 -
                             rhs.red() * rhs.alpha() / 255) +
                    std::abs(lhs.green() * lhs.alpha() / 255 -
                             rhs.green() * rhs.alpha() / 255) +
                    std::abs(lhs.blue() * lhs.alpha() / 255 -
                             rhs.blue() * rhs.alpha() / 255) +
                    std::abs(lhs.alpha() - rhs.alpha());
      ++count;
    }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}

const error::ErrorScene* errorScene(const MermaidRenderEntry& entry,
                                    const QString& label) {
  const auto scene = std::dynamic_pointer_cast<const error::ErrorScene>(entry.scene);
  require(scene != nullptr, label + QStringLiteral("/error-scene"));
  return scene.get();
}

MermaidRenderEntry render(const QString& source) {
  editor::MermaidRenderCache cache;
  return cache.getSync(cache.makeKey(source), source);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected error-diagram fixture"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Error fixture upstream version drifted"));
  const QDir directory = QFileInfo(manifestPath).dir();

  // ---- literal "error": Ready entry + geometry/style contract ----
  {
    const QJsonObject analysis =
        root.value(QStringLiteral("cases")).toObject()
            .value(QStringLiteral("literal-error")).toObject()
            .value(QStringLiteral("analysis")).toObject();
    const MermaidRenderEntry entry = render(QStringLiteral("error"));
    require(entry.status == MermaidRenderStatus::Ready,
            QStringLiteral("literal-error/status"));
    require(entry.naturalSize == QSize(512, 109),
            QStringLiteral("literal-error/natural-size"));
    require(entry.metadata.roleDescription == QLatin1String("error"),
            QStringLiteral("literal-error/role"));
    require(!entry.metadata.hasVisibleTitle(),
            QStringLiteral("literal-error/title-cleared"));
    const error::ErrorScene* scene = errorScene(entry, QStringLiteral("literal-error"));
    require(scene->iconPaths.size() == 6,
            QStringLiteral("literal-error/icon-count"));
    // Client box: 512 wide, height = the fixture's measured clientRect —
    // Chromium lays the replaced element out in LayoutUnits (1/64), so
    // 512*512/2412 = 108.68325 floors to 108.671875.
    require(near(scene->bounds.width(), 512.0, 1e-9) &&
                near(scene->bounds.height(),
                     analysis.value(QStringLiteral("clientRect")).toObject()
                         .value(QStringLiteral("height")).toDouble(), 1e-9) &&
                near(scene->bounds.height(), 108.671875, 1e-9),
            QStringLiteral("literal-error/client-box"));
    require(near(scene->viewBoxBounds.width(), 2412.0, 1e-9) &&
                near(scene->viewBoxBounds.height(), 512.0, 1e-9),
            QStringLiteral("literal-error/view-box"));
    const QJsonArray paths = analysis.value(QStringLiteral("paths")).toArray();
    require(paths.size() == scene->iconPaths.size(),
            QStringLiteral("literal-error/path-count-oracle"));
    for (int index = 0; index < paths.size(); ++index) {
      const QJsonObject expected = paths.at(index).toObject()
                                       .value(QStringLiteral("bbox")).toObject();
      const QRectF actual = scene->iconPaths.at(index).boundingRect();
      require(near(actual.x(), expected.value(QStringLiteral("x")).toDouble(), 0.01) &&
                  near(actual.y(), expected.value(QStringLiteral("y")).toDouble(), 0.01) &&
                  near(actual.width(), expected.value(QStringLiteral("width")).toDouble(), 0.01) &&
                  near(actual.height(), expected.value(QStringLiteral("height")).toDouble(), 0.01),
              QStringLiteral("literal-error/path%1-bbox").arg(index));
    }
    require(scene->headline.text == QLatin1String("Syntax error in text") &&
                scene->headline.anchor == QPointF(1440.0, 250.0) &&
                scene->headline.fontSize == 150.0,
            QStringLiteral("literal-error/headline"));
    require(scene->version.text == QLatin1String("mermaid version 11.16.0") &&
                scene->version.anchor == QPointF(1250.0, 400.0) &&
                scene->version.fontSize == 100.0,
            QStringLiteral("literal-error/version"));
    // Path data must be the upstream strings verbatim.
    const QStringList nativeData = error::errorIconPathData();
    require(nativeData.size() == paths.size(),
            QStringLiteral("literal-error/path-data-count"));
    for (int index = 0; index < nativeData.size(); ++index)
      require(nativeData.at(index) ==
                  paths.at(index).toObject().value(QStringLiteral("d")).toString(),
              QStringLiteral("literal-error/path%1-data").arg(index));
    // Default theme: #552222 both channels.
    require(colorNear(cssColor(scene->style.errorBkgColor), QStringLiteral("rgb(85, 34, 34)")) &&
                colorNear(cssColor(scene->style.errorTextColor), QStringLiteral("rgb(85, 34, 34)")),
            QStringLiteral("literal-error/theme-colors"));
  }

  // ---- 11 themes: resolved errorBkgColor/errorTextColor vs the oracle ----
  {
    const QJsonObject themes = root.value(QStringLiteral("themes")).toObject();
    require(themes.size() == 11, QStringLiteral("themes/count"));
    for (auto it = themes.constBegin(); it != themes.constEnd(); ++it) {
      const QString theme = it.key();
      const QJsonObject expected = it.value().toObject();
      const QString source = QStringLiteral(
          "%%{init: {\"theme\": \"%1\"}}%%\nerror").arg(theme);
      const MermaidRenderEntry entry = render(source);
      require(entry.status == MermaidRenderStatus::Ready,
              theme + QStringLiteral("/status"));
      const error::ErrorScene* scene = errorScene(entry, theme);
      require(colorNear(cssColor(scene->style.errorBkgColor),
                        expected.value(QStringLiteral("iconFill")).toString()),
              theme + QStringLiteral("/error-bkg"));
      require(colorNear(cssColor(scene->style.errorTextColor),
                        expected.value(QStringLiteral("textFill")).toString()),
              theme + QStringLiteral("/error-text"));
    }
  }

  // ---- failure semantics: fallback scene presence + message parity ----
  {
    const QJsonObject failures = root.value(QStringLiteral("failures")).toObject();
    struct Case {
      const char* key;
      QString source;
    };
    const Case cases[] = {
        {"dash-only", QStringLiteral("---")},
        {"unclosed-frontmatter",
         QStringLiteral("---\ntitle: x\nflowchart TB\nA --> B")},
        {"invalid-flowchart",
         QStringLiteral("flowchart TB\nsubgraph S[Group]\nA --> B")},
        {"bad-yaml",
         QStringLiteral("---\nconfig: [unclosed\n---\nflowchart TB\nA --> B")},
        {"unknown-diagram", QStringLiteral("this is not a diagram")},
    };
    for (const Case& item : cases) {
      const QJsonObject expected =
          failures.value(QLatin1String(item.key)).toObject();
      const MermaidRenderEntry entry = render(item.source);
      require(entry.status == MermaidRenderStatus::Error,
              QLatin1String(item.key) + QStringLiteral("/status"));
      // Message parity is the exact upstream exception text where upstream
      // throws one through the same path (dash/unknown); the native parser
      // messages for parse errors are category-locked elsewhere.
      if (QLatin1String(item.key) == QLatin1String("dash-only") ||
          QLatin1String(item.key) == QLatin1String("unknown-diagram")) {
        require(entry.errorMessage.contains(expected.value(QStringLiteral("message")).toString()),
                QLatin1String(item.key) + QStringLiteral("/message"));
      }
      const bool expectScene = expected.value(QStringLiteral("fallbackSvg")).toBool();
      require((entry.scene != nullptr) == expectScene,
              QString::fromLatin1(item.key) + QStringLiteral("/fallback-scene"));
      if (expectScene) errorScene(entry, QLatin1String(item.key));
    }
    // suppressErrorRendering is stripped by the secure-source sanitizer in
    // both renderers (see the config matrix row) — through the Markdown API
    // the fallback is unconditional; the fixture's suppressed case is the
    // initialize()-only behavior, outside Muffin's source API.
  }

  // ---- themeCSS cascade: user sheet competes with the base rules ----
  {
    const QJsonObject themeCss = root.value(QStringLiteral("themeCss")).toObject();
    const QString source = QStringLiteral(
        "%%{init: {\"themeCSS\": \"%1\"}}%%\nerror")
        .arg(themeCss.value(QStringLiteral("themeCss")).toString());
    const MermaidRenderEntry entry = render(source);
    require(entry.status == MermaidRenderStatus::Ready,
            QStringLiteral("theme-css/status"));
    const error::ErrorScene* scene = errorScene(entry, QStringLiteral("theme-css"));
    require(scene->css.icons.size() == 6,
            QStringLiteral("theme-css/icon-css-count"));
    require(colorNear(cssColor(scene->css.icons.first().fill),
                      themeCss.value(QStringLiteral("iconFill")).toString(), 0),
            QStringLiteral("theme-css/icon-fill"));
    require(near(scene->css.headline.fontSize, 90.0, 0.01) &&
                near(scene->css.version.fontSize, 90.0, 0.01),
            QStringLiteral("theme-css/font-size"));
  }

  // ---- themeCSS structural + stroke channels ----
  // `g:nth-of-type(2) path { fill }` only reaches the icons when the content
  // group is the second g child of the svg root (empty scaffold = g#1; the
  // <style> element is another tag and does not shift nth-of-type) — the
  // browser oracle pins the same selector result. The bare `path` rule proves
  // icons have a live stroke channel, and the split fill/stroke on the texts
  // proves both paint channels are modeled independently. Icon styles are
  // asserted PER PATH against the six-entry oracle array.
  {
    const QJsonObject structure =
        root.value(QStringLiteral("themeCssStructure")).toObject();
    const QString source = QStringLiteral(
        "%%{init: {\"themeCSS\": \"%1\"}}%%\nerror")
        .arg(structure.value(QStringLiteral("themeCss")).toString());
    const MermaidRenderEntry entry = render(source);
    const error::ErrorScene* scene = errorScene(entry, QStringLiteral("theme-css-structure"));
    const QJsonArray icons = structure.value(QStringLiteral("icons")).toArray();
    require(icons.size() == 6 && scene->css.icons.size() == 6,
            QStringLiteral("theme-css-structure/icon-count"));
    for (int index = 0; index < icons.size(); ++index) {
      const QJsonObject expected = icons.at(index).toObject();
      const error::ErrorIconCss& icon = scene->css.icons.at(index);
      require(colorNear(cssColor(icon.fill),
                        expected.value(QStringLiteral("fill")).toString(), 0),
              QStringLiteral("theme-css-structure/icon%1-fill").arg(index));
      require(colorNear(cssColor(icon.stroke),
                        expected.value(QStringLiteral("stroke")).toString(), 0),
              QStringLiteral("theme-css-structure/icon%1-stroke").arg(index));
      require(near(icon.strokeWidthPx,
                   expected.value(QStringLiteral("strokeWidth")).toDouble(), 0.01),
              QStringLiteral("theme-css-structure/icon%1-stroke-width").arg(index));
    }
    require(colorNear(cssColor(scene->css.headline.fill),
                      structure.value(QStringLiteral("textFill")).toString(), 0) &&
                colorNear(cssColor(scene->css.version.fill),
                          structure.value(QStringLiteral("textFill")).toString(), 0),
            QStringLiteral("theme-css-structure/text-fill"));
    require(colorNear(cssColor(scene->css.headline.stroke),
                      structure.value(QStringLiteral("textStroke")).toString(), 0) &&
                colorNear(cssColor(scene->css.version.stroke),
                          structure.value(QStringLiteral("textStroke")).toString(), 0),
            QStringLiteral("theme-css-structure/text-stroke"));
    // The tag rule only matches `path`; the texts keep the 1px initial width.
    require(near(scene->css.headline.strokeWidthPx, 1.0, 0.01) &&
                near(scene->css.version.strokeWidthPx, 1.0, 0.01),
            QStringLiteral("theme-css-structure/text-stroke-width"));
  }

  // ---- themeCSS per-path differential ----
  // The six icons are sibling <path> elements, so structural selectors style
  // them INDIVIDUALLY: :nth-of-type(2) recolors only the second path, the
  // adjacent-sibling combinator strokes every path except the first, and
  // opacity / stroke-width / display each diverge on exactly one path. The
  // oracle array locks every path's computed style; a renderer that folds
  // the icons into one shared style fails icon0-vs-icon1 immediately.
  {
    const QJsonObject perPath =
        root.value(QStringLiteral("themeCssPerPath")).toObject();
    const QString source = QStringLiteral(
        "%%{init: {\"themeCSS\": \"%1\"}}%%\nerror")
        .arg(perPath.value(QStringLiteral("themeCss")).toString());
    const MermaidRenderEntry entry = render(source);
    require(entry.status == MermaidRenderStatus::Ready,
            QStringLiteral("theme-css-per-path/status"));
    const error::ErrorScene* scene = errorScene(entry, QStringLiteral("theme-css-per-path"));
    const QJsonArray icons = perPath.value(QStringLiteral("icons")).toArray();
    require(icons.size() == 6 && scene->css.icons.size() == 6,
            QStringLiteral("theme-css-per-path/icon-count"));
    for (int index = 0; index < icons.size(); ++index) {
      const QJsonObject expected = icons.at(index).toObject();
      const error::ErrorIconCss& icon = scene->css.icons.at(index);
      require(colorNear(cssColor(icon.fill),
                        expected.value(QStringLiteral("fill")).toString(), 0),
              QStringLiteral("theme-css-per-path/icon%1-fill").arg(index));
      require(colorNear(cssColor(icon.stroke),
                        expected.value(QStringLiteral("stroke")).toString(), 0),
              QStringLiteral("theme-css-per-path/icon%1-stroke").arg(index));
      require(near(icon.strokeWidthPx,
                   expected.value(QStringLiteral("strokeWidth")).toDouble(), 0.01),
              QStringLiteral("theme-css-per-path/icon%1-stroke-width").arg(index));
      require(near(icon.opacity,
                   expected.value(QStringLiteral("opacity")).toDouble(), 1e-6),
              QStringLiteral("theme-css-per-path/icon%1-opacity").arg(index));
      require(icon.visible == (expected.value(QStringLiteral("display")).toString() !=
                               QLatin1String("none")),
              QStringLiteral("theme-css-per-path/icon%1-visible").arg(index));
    }
    // Differential locks: the second path diverges from the first in fill;
    // only paths 1..5 carry the adjacent-sibling stroke (the undeclared
    // initial stroke computes to the literal "none", so compare resolved
    // paints rather than string emptiness).
    require(scene->css.icons.at(1).fill != scene->css.icons.at(0).fill,
            QStringLiteral("theme-css-per-path/fill-differential"));
    require(!cssColor(scene->css.icons.at(0).stroke).isValid() &&
                cssColor(scene->css.icons.at(1).stroke).isValid(),
            QStringLiteral("theme-css-per-path/stroke-differential"));
  }

  // ---- pixel oracle: default-theme lightbulb at the client box ----
  {
    const QJsonObject png = root.value(QStringLiteral("png")).toObject();
    const QString referencePath =
        directory.filePath(png.value(QStringLiteral("file")).toString());
    require(sha256(referencePath) ==
                png.value(QStringLiteral("sha256")).toString().toLatin1(),
            QStringLiteral("png-sha"));
    const QImage expected(referencePath);
    const QImage actual = nativePng(QStringLiteral("error"));
    require(!actual.isNull(), QStringLiteral("png/native"));
    require(actual.size() == expected.size(),
            QStringLiteral("png/size %1x%2 != %3x%4")
                .arg(actual.width()).arg(actual.height())
                .arg(expected.width()).arg(expected.height()));
    const qreal iou = alphaIou(actual, expected);
    const qreal rgba = rgbaScore(actual, expected);
    std::fprintf(stderr, "error-diagram IoU %.5f RGBA %.5f\n", iou, rgba);
    require(iou >= .88, QStringLiteral("png/alpha-IoU"));
    require(rgba >= .90, QStringLiteral("png/RGBA"));
    // The parse-failure fallback produces the identical visual.
    const QImage fallback =
        nativePng(QStringLiteral("flowchart TB\nsubgraph S[Group]\nA --> B"));
    require(!fallback.isNull() && fallback.size() == expected.size(),
            QStringLiteral("png/fallback"));
  }

  // ---- SVG export: client-box viewBox, max-width 512, no class suffix ----
  {
    const QByteArray svg = editor::MermaidRenderCache::renderMermaidSourceToSvg(
                               QStringLiteral("error")).svg;
    require(!svg.isEmpty(), QStringLiteral("svg/empty"));
    require(svg.contains("max-width: 512px;"),
            QStringLiteral("svg/max-width"));
    // Fractional client box: the exported intrinsic height is the browser's
    // LayoutUnit clientRect (108.671875), not the raster-rounded 109; the
    // useMaxWidth path writes no height attribute, exactly like upstream.
    require(svg.contains("viewBox=\"0 0 512 108.671875\""),
            QStringLiteral("svg/viewBox-client-box"));
    require(!svg.contains("height=\""),
            QStringLiteral("svg/no-height-attribute"));
    require(svg.contains("aria-roledescription=\"error\""),
            QStringLiteral("svg/role"));
    require(!svg.contains("class=\"mfn-mermaid \""),
            QStringLiteral("svg/class-trim"));
    // The invalid-source export also serializes the fallback scene.
    const QByteArray invalidSvg = editor::MermaidRenderCache::renderMermaidSourceToSvg(
        QStringLiteral("flowchart TB\nsubgraph S[Group]\nA --> B")).svg;
    require(invalidSvg.contains("aria-roledescription=\"error\""),
            QStringLiteral("svg/fallback"));
  }

  std::puts("MermaidErrorDiagramTest: all cases passed");
  return 0;
}
