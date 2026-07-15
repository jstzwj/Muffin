// Level-3 category-driven pixel golden (milestone G3). For each case in the
// golden-pixel manifest: build the native scene, render it, and run FlowSceneCompare
// (INTERIOR exact / BOUNDARY RGBA / TEXT IoU / EMPTY) against the Chrome golden PNG.
// Replaces the old MermaidFlowchartPixelTest heuristic. Diagnostic PNGs are dumped
// to a temp dir on failure.

#include "mermaid/flowchart/Flowchart.h"
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
  const QString failDir = QDir::tempPath() + QStringLiteral("/muffin-golden-pixel-fail");

  int failures = 0;
  int highDpiCases = 0;
  bool sawCjk = false;
  bool sawBidi = false;
  bool sawDeterministicAnimation = false;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
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
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(source);
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data());

    flowchart::FlowLayoutOptions options;
    for (const flowchart::FlowEdge& e : chart.data().edges)
      if (!e.text.isEmpty())
        options.measuredEdgeLabels.insert(e.id,
                                          flowchart::measureFlowchartEdgeLabel(e));
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, options);
    const flowtheme::FlowThemeVariables theme = flowtheme::resolveFlowTheme(themeId(fixture.value(QStringLiteral("theme")).toString()));
    const flowscene::FlowScene scene = flowscene::buildFlowScene(chart.data(), layout, theme);

    QImage golden;
    require(golden.load(dir.filePath(fixture.value(QStringLiteral("file")).toString())),
            QStringLiteral("Case %1: could not load golden PNG").arg(id));
    const QJsonObject content = fixture.value(QStringLiteral("content")).toObject();
    require(std::abs(golden.width() - std::ceil(content.value(QStringLiteral("width")).toDouble() * dpr)) <= 1.0 &&
                std::abs(golden.height() - std::ceil(content.value(QStringLiteral("height")).toDouble() * dpr)) <= 1.0,
            QStringLiteral("Case %1: golden physical size is not content size x DPR").arg(id));

    const flowscene::CompareReport report = flowscene::compareLevel3(
        scene, golden, QStringLiteral("Arial"), flowscene::CompareThresholds{}, failDir + QStringLiteral("/") + id,
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
  qDebug().noquote() << "MermaidGoldenPixelTest:" << cases.size() << "cases pass Level-3 (interior exact + boundary RGBA + text IoU + empty)";
  return 0;
}
