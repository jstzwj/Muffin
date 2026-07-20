// Level-3 category-driven pixel golden (milestone G3). For each case in the
// golden-pixel manifest: build the native scene, render it, and run FlowSceneCompare
// (INTERIOR exact / BOUNDARY RGBA / TEXT IoU / EMPTY) against the Chrome golden PNG.
// Replaces the old MermaidFlowchartPixelTest heuristic. Diagnostic PNGs are dumped
// to a temp dir on failure.

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/scene/FlowSceneCompare.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/theme/FlowTheme.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QPainter>

#include <cstdlib>
#include <algorithm>
#include <cmath>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

flowtheme::FlowThemeId themeId(const QString& name) {
  static const QHash<QString, flowtheme::FlowThemeId> map = {
      {QStringLiteral("default"), flowtheme::FlowThemeId::Default},
      {QStringLiteral("base"), flowtheme::FlowThemeId::Base},
      {QStringLiteral("dark"), flowtheme::FlowThemeId::Dark},
      {QStringLiteral("forest"), flowtheme::FlowThemeId::Forest},
      {QStringLiteral("neutral"), flowtheme::FlowThemeId::Neutral},
      {QStringLiteral("neo"), flowtheme::FlowThemeId::Neo},
      {QStringLiteral("neo-dark"), flowtheme::FlowThemeId::NeoDark},
      {QStringLiteral("redux"), flowtheme::FlowThemeId::Redux},
      {QStringLiteral("redux-dark"), flowtheme::FlowThemeId::ReduxDark},
      {QStringLiteral("redux-color"), flowtheme::FlowThemeId::ReduxColor},
      {QStringLiteral("redux-dark-color"), flowtheme::FlowThemeId::ReduxDarkColor},
  };
  const auto it = map.constFind(name);
  require(it != map.constEnd(), QStringLiteral("Unknown theme in manifest: %1").arg(name));
  return it.value();
}
qreal fontPixelSize(const QString& value) {
  static const QRegularExpression number(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const auto match = number.match(value);
  return match.hasMatch() ? match.captured(1).toDouble() : 16.0;
}
QColor cssRgb(const QString& value) {
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgb\((\d+),\s*(\d+),\s*(\d+)\)$)"));
  const auto match = rgb.match(value);
  require(match.hasMatch(), QStringLiteral("Invalid computed CSS color: %1").arg(value));
  return QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                match.captured(3).toInt());
}

QByteArray fileSha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(),
                                  QCryptographicHash::Sha256).toHex();
}

QRect alphaBounds(const QImage& image, int threshold = 32) {
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(image.pixel(x, y)) < threshold) continue;
      left = std::min(left, x); top = std::min(top, y);
      right = std::max(right, x); bottom = std::max(bottom, y);
    }
  }
  return right < left ? QRect() : QRect(left, top, right - left + 1,
                                        bottom - top + 1);
}

qreal alignedAlphaCoverage(const QImage& native, const QImage& browser,
                           qreal dpr) {
  constexpr int inkAlpha = 64;
  const int radius = static_cast<int>(std::ceil(4.0 * dpr));
  const int tolerance = std::max(1, static_cast<int>(std::ceil(dpr)));
  auto inkNear = [&](const QImage& image, int x, int y) {
    for (int oy = -tolerance; oy <= tolerance; ++oy)
      for (int ox = -tolerance; ox <= tolerance; ++ox) {
        const int sx = x + ox, sy = y + oy;
        if (sx >= 0 && sx < image.width() && sy >= 0 && sy < image.height() &&
            qAlpha(image.pixel(sx, sy)) >= inkAlpha)
          return true;
      }
    return false;
  };
  qreal best = 0.0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      qint64 nativeInk = 0, browserInk = 0;
      qint64 nativeMatched = 0, browserMatched = 0;
      for (int y = 0; y < native.height(); ++y) {
        for (int x = 0; x < native.width(); ++x) {
          const bool a = qAlpha(native.pixel(x, y)) >= inkAlpha;
          if (a) {
            ++nativeInk;
            if (inkNear(browser, x + dx, y + dy)) ++nativeMatched;
          }
        }
      }
      for (int y = 0; y < browser.height(); ++y) {
        for (int x = 0; x < browser.width(); ++x) {
          const bool b = qAlpha(browser.pixel(x, y)) >= inkAlpha;
          if (b) {
            ++browserInk;
            if (inkNear(native, x - dx, y - dy)) ++browserMatched;
          }
        }
      }
      if (nativeInk > 0 && browserInk > 0)
        best = std::max(best, std::min(qreal(nativeMatched) / nativeInk,
                                      qreal(browserMatched) / browserInk));
    }
  }
  return best;
}

QImage renderFlowMathLabelCrop(const flowscene::FlowSceneLabel& label,
                               const QSize& physicalSize,
                               const QString& fallbackFamily, qreal dpr) {
  QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  const QString family = label.fontFamily.isEmpty() ? fallbackFamily
                                                     : label.fontFamily;
  QFont font(family);
  MermaidFontRegistry::configureFont(font, family);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize(label.fontSize))));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr, dpr);
  flowchart::paintFlowLabel(
      painter, label.richText,
      QRectF(0.0, 0.0, physicalSize.width() / dpr,
             physicalSize.height() / dpr),
      font.family(), font.pixelSize(), font.pixelSize() * 1.5,
      Qt::black, true);
  painter.end();
  return image;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected golden-pixel manifest path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open golden-pixel manifest"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Golden-pixel manifest version drifted"));
  const QDir dir = QFileInfo(file.fileName()).absoluteDir();
  MermaidFontRegistry::ensureLoaded();
  require(root.value(QStringLiteral("font")).toObject().value(QStringLiteral("mode")).toString() ==
              QLatin1String("bundled"),
          QStringLiteral("Golden-pixel fixed font metadata is missing"));
  const QString failDir = QDir::tempPath() + QStringLiteral("/muffin-golden-pixel-fail");

  int failures = 0;
  int highDpiCases = 0;
  bool sawCjk = false;
  bool sawBidi = false;
  bool sawDeterministicAnimation = false;
  int neoLookCases = 0;
  int fixedNotoCases = 0;
  int systemFallbackCases = 0;
  QSet<QString> neoShapes;
  QSet<QString> neoDarkShapes;
  QSet<qreal> neoDarkShapeDprs;
  int neoDarkClusterCases = 0;
  QSet<QString> reduxStructureThemes;
  int handDrawnCases = 0;
  QSet<int> handDrawnSeeds;
  QSet<QString> handDrawnShapes;
  QSet<QString> handDrawnDirections;
  QSet<qreal> handDrawnDprs;
  bool sawHandDrawnComposite = false;
  QSet<QString> mathCropKinds;
  QSet<QString> caseIds;
  QSet<QString> coveredThemes;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!caseIds.contains(id), QStringLiteral("Duplicate golden-pixel case id: %1").arg(id));
    caseIds.insert(id);
    if (id.startsWith(QStringLiteral("theme-")))
      coveredThemes.insert(fixture.value(QStringLiteral("theme")).toString());
    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    if (dpr > 1.0) ++highDpiCases;
    sawCjk = sawCjk || id.contains(QStringLiteral("cjk"));
    sawBidi = sawBidi || id.contains(QStringLiteral("bidi"));
    if (id.contains(QStringLiteral("animated"))) {
      require(fixture.value(QStringLiteral("animationState")).toString() ==
                  QLatin1String("initial"),
              QStringLiteral("Case %1: animated golden must declare a deterministic state").arg(id));
      sawDeterministicAnimation = true;
    }
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool fixedNoto = fixture.value(QStringLiteral("fontMode")).toString() ==
                           QLatin1String("noto");
    const QString renderFamily = fixedNoto ? MermaidFontRegistry::primaryFamily()
                                            : QStringLiteral("Arial");
    if (fixedNoto) {
      ++fixedNotoCases;
      if (fixture.value(QStringLiteral("look")).toString() != QLatin1String("handDrawn"))
        require(fixture.value(QStringLiteral("enforceInterior")).toBool(true) &&
                    !fixture.contains(QStringLiteral("textGlyphIou")) &&
                    !fixture.contains(QStringLiteral("emptyMaxMismatchRatio")),
                QStringLiteral("Case %1: bundled Noto oracle must use uniform strict thresholds")
                    .arg(id));
    }
    if (id == QLatin1String("font-system-fallback-mixed")) {
      ++systemFallbackCases;
      require(!fixture.value(QStringLiteral("enforceInterior")).toBool(true),
              QStringLiteral("System fallback oracle must remain compatibility-only"));
    }
    if (id.startsWith(QStringLiteral("look-neo-shapes-"))) {
      static const QRegularExpression shapePattern(QStringLiteral(R"(shape:\s*([\w-]+))"));
      auto matches = shapePattern.globalMatch(source);
      while (matches.hasNext()) neoShapes.insert(matches.next().captured(1));
    }
    if (id.startsWith(QStringLiteral("look-neo-dark-shapes-"))) {
      static const QRegularExpression shapePattern(QStringLiteral(R"(shape:\s*([\w-]+))"));
      auto matches = shapePattern.globalMatch(source);
      while (matches.hasNext()) neoDarkShapes.insert(matches.next().captured(1));
      neoDarkShapeDprs.insert(dpr);
    }
    if (id.startsWith(QStringLiteral("look-neo-dark-cluster-"))) ++neoDarkClusterCases;
    if (id.startsWith(QStringLiteral("look-redux")))
      reduxStructureThemes.insert(fixture.value(QStringLiteral("theme")).toString());
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        fixture.value(QStringLiteral("look")).toString(QStringLiteral("classic")));
    if (look == flowchart::FlowLook::Neo) ++neoLookCases;
    if (id.startsWith(QStringLiteral("look-hand-drawn-"))) {
      ++handDrawnCases;
      handDrawnSeeds.insert(fixture.value(QStringLiteral("handDrawnSeed")).toInt());
      handDrawnDprs.insert(dpr);
      static const QRegularExpression directionPattern(
          QStringLiteral(R"(^flowchart\s+(TB|BT|LR|RL))"));
      const auto directionMatch = directionPattern.match(source);
      if (directionMatch.hasMatch()) handDrawnDirections.insert(directionMatch.captured(1));
      if (id.startsWith(QStringLiteral("look-hand-drawn-shapes-"))) {
        static const QRegularExpression shapePattern(QStringLiteral(R"(shape:\s*([\w-]+))"));
        auto matches = shapePattern.globalMatch(source);
        while (matches.hasNext()) handDrawnShapes.insert(matches.next().captured(1));
      }
      if (id == QLatin1String("look-hand-drawn-cluster-self-marker-cjk-bidi-2x"))
        sawHandDrawnComposite = source.contains(QStringLiteral("subgraph")) &&
                                 source.contains(QStringLiteral("--> A")) &&
                                 source.contains(QStringLiteral("o--o"));
      require(look == flowchart::FlowLook::HandDrawn &&
                  fixture.value(QStringLiteral("handDrawnSeed")).toInt() > 0,
              QStringLiteral("Case %1: handDrawn look/seed metadata drifted").arg(id));
    }
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(source);
    const flowtheme::FlowThemeVariables theme = flowtheme::resolveFlowTheme(
        themeId(fixture.value(QStringLiteral("theme")).toString()),
        {{QStringLiteral("fontFamily"), fixedNoto ? MermaidFontRegistry::cssFamilyStack()
                                                   : QStringLiteral("Arial")}});
    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = renderFamily;
    textOptions.fontPixelSize = fontPixelSize(theme.fontSize);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    textOptions.look = look;
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data(), textOptions);

    flowchart::FlowLayoutOptions options;
    options.look = look;
    for (const flowchart::FlowEdge& e : chart.data().edges)
      if (!e.text.isEmpty())
        options.measuredEdgeLabels.insert(e.id,
                                          flowchart::measureFlowchartEdgeLabel(e, textOptions));
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, options);
    const flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, theme, look,
        static_cast<quint32>(fixture.value(QStringLiteral("handDrawnSeed")).toInt()));
    if (fixture.contains(QStringLiteral("mathCropFile"))) {
      const auto node = std::find_if(
          scene.nodes.cbegin(), scene.nodes.cend(), [](const auto& candidate) {
            return !candidate.label.richText.math.isEmpty();
          });
      require(node != scene.nodes.cend(),
              QStringLiteral("Case %1: native Math label is missing").arg(id));
      require(std::all_of(
                  node->label.richText.math.cbegin(),
                  node->label.richText.math.cend(), [](const auto& span) {
                    return static_cast<bool>(span.prepared);
                  }),
              QStringLiteral("Case %1: flowchart Math was not prepared in the scene")
                  .arg(id));
      const QString cropPath = dir.filePath(
          fixture.value(QStringLiteral("mathCropFile")).toString());
      require(fileSha256(cropPath) ==
                  fixture.value(QStringLiteral("mathCropSha256"))
                      .toString().toLatin1(),
              QStringLiteral("Case %1: Math crop hash drifted").arg(id));
      const QImage browserCrop(cropPath);
      require(!browserCrop.isNull(),
              QStringLiteral("Case %1: Math crop is missing").arg(id));
      const QImage nativeCrop = renderFlowMathLabelCrop(
          node->label, browserCrop.size(), renderFamily, dpr);
      const auto nativeLabelLayout = flowchart::layoutFlowLabel(
          node->label.richText, renderFamily, textOptions.fontPixelSize,
          textOptions.lineHeight);
      const QJsonObject browserMathBox =
          fixture.value(QStringLiteral("content")).toObject()
              .value(QStringLiteral("mathBox")).toObject();
      require(!browserMathBox.isEmpty(),
              QStringLiteral("Case %1: browser Math box is missing").arg(id));
      const qreal browserMathWidth =
          browserMathBox.value(QStringLiteral("width")).toDouble();
      const qreal browserLineHeight = std::max(
          textOptions.lineHeight,
          browserMathBox.value(QStringLiteral("height")).toDouble());
      require(browserMathWidth > 0.0 && browserLineHeight > 0.0,
              QStringLiteral("Case %1: browser Math box is empty").arg(id));
      if (fixture.value(QStringLiteral("mathCropKind")).toString() ==
          QLatin1String("array")) {
        const qreal browserArrayHeight =
            browserMathBox.value(QStringLiteral("height")).toDouble();
        require(std::abs(nativeLabelLayout.size.width() - browserMathWidth) <= 0.25 &&
                    std::abs(nativeLabelLayout.size.height() - browserArrayHeight) <= 0.25,
                QStringLiteral("Case %1: Array DOM box drifted: %2x%3 vs %4x%5")
                    .arg(id)
                    .arg(nativeLabelLayout.size.width(), 0, 'f', 3)
                    .arg(nativeLabelLayout.size.height(), 0, 'f', 3)
                    .arg(browserMathWidth, 0, 'f', 3)
                    .arg(browserArrayHeight, 0, 'f', 3));
      }
      const QRect nativeInk = alphaBounds(nativeCrop);
      const QRect browserInk = alphaBounds(browserCrop);
      require(!nativeInk.isEmpty() && !browserInk.isEmpty(),
              QStringLiteral("Case %1: Math crop rendered blank").arg(id));
      const qreal widthDrift = qreal(std::abs(nativeInk.width() - browserInk.width())) /
                               browserInk.width();
      const qreal coverage = alignedAlphaCoverage(nativeCrop, browserCrop, dpr);
      qDebug().noquote() << id << "flow-math-crop" << nativeCrop.size()
                         << nativeInk << browserInk << "coverage" << coverage;
      require(widthDrift <= 0.15 &&
                  std::abs(nativeInk.height() - browserInk.height()) <=
                      std::ceil(4.0 * dpr),
              QStringLiteral("Case %1: Math crop ink bounds drifted: %2x%3 vs %4x%5")
                  .arg(id).arg(nativeInk.width()).arg(nativeInk.height())
                  .arg(browserInk.width()).arg(browserInk.height()));
      require(coverage >= 0.78,
              QStringLiteral("Case %1: Math crop coverage too low: %2")
                  .arg(id).arg(coverage));
      mathCropKinds.insert(
          fixture.value(QStringLiteral("mathCropKind")).toString());
    }
    if (look == flowchart::FlowLook::HandDrawn) {
      const QImage first = flowscene::renderFlowSceneToImage(scene, dpr, 8.0, renderFamily);
      const QImage repeat = flowscene::renderFlowSceneToImage(scene, dpr, 8.0, renderFamily);
      require(first == repeat,
              QStringLiteral("Case %1: handDrawn rendering is not deterministic").arg(id));
      flowscene::FlowScene alternate = scene;
      ++alternate.handDrawnSeed;
      require(first != flowscene::renderFlowSceneToImage(alternate, dpr, 8.0, renderFamily),
              QStringLiteral("Case %1: handDrawn seed does not affect generated paths").arg(id));
    }

    QImage golden;
    require(golden.load(dir.filePath(fixture.value(QStringLiteral("file")).toString())),
            QStringLiteral("Case %1: could not load golden PNG").arg(id));
    const QJsonObject content = fixture.value(QStringLiteral("content")).toObject();
    if (id.startsWith(QStringLiteral("look-redux"))) {
      const QJsonObject styles = content.value(QStringLiteral("computedStyles")).toObject();
      require(!scene.nodes.isEmpty() && !scene.clusters.isEmpty(),
              QStringLiteral("Case %1: Redux structure scene is incomplete").arg(id));
      require(QColor(scene.nodes.first().fill).rgb() == cssRgb(styles.value(QStringLiteral("nodeFill")).toString()).rgb() &&
                  QColor(scene.nodes.first().stroke).rgb() == cssRgb(styles.value(QStringLiteral("nodeStroke")).toString()).rgb() &&
                  QColor(scene.clusters.first().fill).rgb() == cssRgb(styles.value(QStringLiteral("clusterFill")).toString()).rgb() &&
                  QColor(scene.clusters.first().stroke).rgb() == cssRgb(styles.value(QStringLiteral("clusterStroke")).toString()).rgb(),
              QStringLiteral("Case %1: Redux computed node/cluster style drifted").arg(id));
    }
    if (id.startsWith(QStringLiteral("look-neo-shapes-")) ||
        id.startsWith(QStringLiteral("look-neo-dark-shapes-"))) {
      const QJsonArray boxes = content.value(QStringLiteral("nodeBoxes")).toArray();
      require(boxes.size() == scene.nodes.size(),
              QStringLiteral("Case %1: upstream/native node count differs").arg(id));
      for (qsizetype i = 0; i < scene.nodes.size(); ++i) {
        const QJsonObject expected = boxes.at(i).toObject();
        const auto& actual = scene.nodes.at(i);
        constexpr qreal kGeometryTolerance = 0.2;
        require(std::abs(actual.width - expected.value(QStringLiteral("width")).toDouble()) <=
                        kGeometryTolerance &&
                    std::abs(actual.height - expected.value(QStringLiteral("height")).toDouble()) <=
                        kGeometryTolerance,
                QStringLiteral("Case %1 shape %2: bbox %3x%4, upstream %5x%6")
                    .arg(id, actual.id)
                    .arg(actual.width, 0, 'f', 3)
                    .arg(actual.height, 0, 'f', 3)
                    .arg(expected.value(QStringLiteral("width")).toDouble(), 0, 'f', 3)
                    .arg(expected.value(QStringLiteral("height")).toDouble(), 0, 'f', 3));
      }
    }
    require(std::abs(golden.width() - std::ceil(content.value(QStringLiteral("width")).toDouble() * dpr)) <= 1.0 &&
                std::abs(golden.height() - std::ceil(content.value(QStringLiteral("height")).toDouble() * dpr)) <= 1.0,
            QStringLiteral("Case %1: golden physical size is not content size x DPR").arg(id));

    flowscene::CompareThresholds thresholds;
    if (fixture.contains(QStringLiteral("textGlyphIou")))
      thresholds.textGlyphIou = fixture.value(QStringLiteral("textGlyphIou")).toDouble();
    if (fixture.contains(QStringLiteral("emptyMaxMismatchRatio")))
      thresholds.emptyMaxMismatchRatio =
          fixture.value(QStringLiteral("emptyMaxMismatchRatio")).toDouble();
    const flowscene::CompareReport report = flowscene::compareLevel3(
        scene, golden, renderFamily, thresholds,
        failDir + QStringLiteral("/") + id,
        fixture.value(QStringLiteral("enforceInterior")).toBool(true),
        dpr);
    if (!report.passed) {
      ++failures;
      qCritical().noquote() << QStringLiteral("FAIL %1: %2").arg(id, report.summary);
      if (!report.actualPath.isEmpty())
        qCritical().noquote() << QStringLiteral("  diagnostics: %1").arg(report.actualPath);
    } else {
      qDebug().noquote() << QStringLiteral("ok   %1: %2").arg(id, report.summary);
    }
  }

  if (failures > 0) fail(QStringLiteral("%1 of %2 golden-pixel cases FAILED (diagnostics in %3)").arg(failures).arg(cases.size()).arg(failDir));
  require(highDpiCases >= 4 && sawCjk && sawBidi && sawDeterministicAnimation,
          QStringLiteral("Golden-pixel CJK/bidi/high-DPI coverage matrix regressed"));
  require(coveredThemes.size() == 11,
          QStringLiteral("Golden-pixel theme coverage regressed: %1/11").arg(coveredThemes.size()));
  require(neoLookCases >= 2,
          QStringLiteral("Golden-pixel neo look coverage regressed: %1/2").arg(neoLookCases));
  require(fixedNotoCases >= 5,
          QStringLiteral("Golden-pixel fixed Noto coverage regressed: %1/5").arg(fixedNotoCases));
  require(systemFallbackCases == 1,
          QStringLiteral("Golden-pixel system fallback coverage regressed: %1/1")
              .arg(systemFallbackCases));
  const QStringList bundledFamilies = MermaidFontRegistry::familyStack();
  for (const QString& family : {QStringLiteral("Noto Sans"),
                                QStringLiteral("Noto Sans CJK SC"),
                                QStringLiteral("Noto Sans Arabic"),
                                QStringLiteral("Noto Sans Hebrew")})
    require(bundledFamilies.contains(family),
            QStringLiteral("Bundled Mermaid font family is unavailable: %1").arg(family));
  require(neoShapes.size() == 48,
          QStringLiteral("Golden-pixel neo shape coverage regressed: %1/48").arg(neoShapes.size()));
  require(neoDarkShapes.size() == 48 && neoDarkShapeDprs.size() == 4,
          QStringLiteral("Golden-pixel neo-dark shape/DPR coverage regressed: %1/48 shapes, %2/4 DPRs")
              .arg(neoDarkShapes.size()).arg(neoDarkShapeDprs.size()));
  require(neoDarkClusterCases == 4,
          QStringLiteral("Golden-pixel neo-dark cluster/DPR coverage regressed: %1/4")
              .arg(neoDarkClusterCases));
  require(reduxStructureThemes.size() == 4,
          QStringLiteral("Golden-pixel Redux structure coverage regressed: %1/4")
              .arg(reduxStructureThemes.size()));
  require(handDrawnCases >= 10 && handDrawnSeeds.size() >= 9 &&
              handDrawnShapes.size() == 48 && handDrawnDirections.size() == 4 &&
              handDrawnDprs.size() == 3 && sawHandDrawnComposite,
          QStringLiteral("Golden-pixel handDrawn matrix regressed: %1 cases, %2/48 shapes, "
                         "%3/4 directions, %4/3 DPRs")
              .arg(handDrawnCases).arg(handDrawnShapes.size())
              .arg(handDrawnDirections.size()).arg(handDrawnDprs.size()));
  require(mathCropKinds.size() == 6,
          QStringLiteral("Flowchart Math crop coverage regressed: %1/6")
              .arg(mathCropKinds.size()));
  qDebug().noquote() << "MermaidGoldenPixelTest:" << cases.size() << "cases pass Level-3 (interior exact + boundary RGBA + text IoU + empty)";
  return 0;
}
