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
#include "mermaid/theme/FlowTheme.h"

#include <QDebug>
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

#include <cstdlib>
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
  QSet<QString> neoShapes;
  int neoDarkClusterCases = 0;
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
    if (fixedNoto) ++fixedNotoCases;
    if (id.startsWith(QStringLiteral("look-neo-shapes-"))) {
      static const QRegularExpression shapePattern(QStringLiteral(R"(shape:\s*([\w-]+))"));
      auto matches = shapePattern.globalMatch(source);
      while (matches.hasNext()) neoShapes.insert(matches.next().captured(1));
    }
    if (id.startsWith(QStringLiteral("look-neo-dark-cluster-"))) ++neoDarkClusterCases;
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        fixture.value(QStringLiteral("look")).toString(QStringLiteral("classic")));
    if (look == flowchart::FlowLook::Neo) ++neoLookCases;
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
    const flowscene::FlowScene scene = flowscene::buildFlowScene(chart.data(), layout, theme, look);

    QImage golden;
    require(golden.load(dir.filePath(fixture.value(QStringLiteral("file")).toString())),
            QStringLiteral("Case %1: could not load golden PNG").arg(id));
    const QJsonObject content = fixture.value(QStringLiteral("content")).toObject();
    if (id.startsWith(QStringLiteral("look-neo-shapes-"))) {
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
  require(neoShapes.size() == 48,
          QStringLiteral("Golden-pixel neo shape coverage regressed: %1/48").arg(neoShapes.size()));
  require(neoDarkClusterCases == 3,
          QStringLiteral("Golden-pixel neo-dark cluster/DPR coverage regressed: %1/3")
              .arg(neoDarkClusterCases));
  qDebug().noquote() << "MermaidGoldenPixelTest:" << cases.size() << "cases pass Level-3 (interior exact + boundary RGBA + text IoU + empty)";
  return 0;
}
