#include "mermaid/editor/MermaidRenderCache.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>
#include <QXmlStreamReader>

#include <cmath>
#include <cstdlib>

using muffin::mermaid::editor::MermaidRenderCache;
using muffin::mermaid::editor::MermaidRenderEntry;
using muffin::mermaid::editor::MermaidSvgRenderResult;
using muffin::mermaid::MermaidPaintOptions;

namespace {

[[noreturn]] void fail(const QString& message) {
  // qCritical alone is swallowed without QT_FORCE_STDERR_LOGGING (the ctest
  // preset does not set it) — flush assertions to stderr directly, the same
  // pattern as the state tests.
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QMap<QString, QString> svgRootAttributes(const QByteArray& svg) {
  QXmlStreamReader reader(svg);
  QMap<QString, QString> attributes;
  bool rootSeen = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (!rootSeen && reader.isStartElement()) {
      require(reader.name() == QLatin1String("svg"),
              QStringLiteral("SVG export root element is not <svg>"));
      rootSeen = true;
      for (const auto& attribute : reader.attributes())
        attributes.insert(attribute.qualifiedName().toString(),
                          attribute.value().toString());
    }
  }
  require(rootSeen && !reader.hasError(),
          QStringLiteral("SVG export is not well-formed XML: %1")
              .arg(reader.errorString()));
  return attributes;
}

void requireRenderable(const QByteArray& svg, const QString& family) {
  QSvgRenderer renderer(svg);
  require(renderer.isValid(), family + QStringLiteral(" SVG is not renderable"));
  const QSize size = renderer.defaultSize().expandedTo(QSize(1, 1));
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  renderer.render(&painter);
  painter.end();
  bool hasPaint = false;
  for (int y = 0; y < image.height() && !hasPaint; ++y) {
    const QRgb* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(scanline[x]) != 0) {
        hasPaint = true;
        break;
      }
    }
  }
  require(hasPaint, family + QStringLiteral(" SVG rendered to a blank image"));
}

int differentPixels(const QImage& left, const QImage& right) {
  require(left.size() == right.size(),
          QStringLiteral("Marker raster dimensions drifted"));
  int count = 0;
  for (int y = 0; y < left.height(); ++y) {
    const QRgb* a = reinterpret_cast<const QRgb*>(left.constScanLine(y));
    const QRgb* b = reinterpret_cast<const QRgb*>(right.constScanLine(y));
    for (int x = 0; x < left.width(); ++x)
      if (a[x] != b[x]) ++count;
  }
  return count;
}

QImage paintScene(const MermaidRenderEntry& entry, bool paintEdgeMarkers) {
  require(entry.scene != nullptr, QStringLiteral("Marker scene is missing"));
  const QRectF bounds = entry.scene->sceneBounds();
  const QSize size(qMax(1, qCeil(bounds.width())),
                   qMax(1, qCeil(bounds.height())));
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.translate(-bounds.left(), -bounds.top());
  MermaidPaintOptions options;
  options.paintEdgeMarkers = paintEdgeMarkers;
  entry.scene->paint(painter, options);
  painter.end();
  return image;
}

MermaidSvgRenderResult renderSvg(const QString& source,
                                 qsizetype instanceIndex = 0) {
  const MermaidSvgRenderResult result =
      MermaidRenderCache::renderMermaidSourceToSvg(source, instanceIndex);
  require(!result.svg.isEmpty(), QStringLiteral("Expected a native SVG result"));
  require(result.svg.startsWith("<svg") &&
              !result.svg.contains("<?xml") && !result.svg.contains("<!DOCTYPE"),
          QStringLiteral("SVG fragment must be directly embeddable in HTML"));
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
#if defined(Q_PROCESSOR_ARM_64)
  // The viewBox oracle is font-coupled (label widths drive the box), and the
  // goldens were captured against the x64 Windows font stack; the ARM64
  // runner's CJK/Japanese fallback faces have different metrics (observed:
  // label-cjk[2] 381.2 vs 397.5). Same platform-infrastructure class as the
  // Linux raster-golden skip — regenerating/dual-sourcing goldens per font
  // stack is the eventual closure, not weakening the tolerance.
  qWarning("skipped on Windows ARM64: viewBox goldens embed x64 Windows font metrics");
  return 0;
#endif

  struct FamilyCase {
    QString name;
    QString cssClass;
    QString source;
    bool emitsViewBox = true;
  };
  const QVector<FamilyCase> families = {
      {QStringLiteral("flowchart"), QStringLiteral("flowchart"),
       QStringLiteral("flowchart TB\nA[Start] --> B[Done]")},
      {QStringLiteral("sequence"), QStringLiteral("sequenceDiagram"),
       QStringLiteral("sequenceDiagram\nAlice->>Bob: Hello")},
      {QStringLiteral("class"), QStringLiteral("classDiagram"),
       QStringLiteral("classDiagram\nclass Client\nclass Service\n"
                      "Client --> Service : uses")},
      {QStringLiteral("state"), QStringLiteral("stateDiagram"),
       QStringLiteral("stateDiagram-v2\n[*] --> Idle\nIdle --> Active")},
      {QStringLiteral("er"), QStringLiteral("erDiagram"),
       QStringLiteral("erDiagram\nCUSTOMER ||--o{ ORDER : places\n")},
      {QStringLiteral("requirement"), QStringLiteral("requirementDiagram"),
       QStringLiteral("requirementDiagram\nrequirement R {\n id: 1\n text: hello\n}\n"
                      "element E {\n type: hw\n}\nR -contains-> E")},
      {QStringLiteral("pie"), QStringLiteral("pieDiagram"),
       QStringLiteral("pie title Pets\n\"Dogs\" : 38\n\"Cats\" : 26\n\"Fish\" : 36")},
      {QStringLiteral("quadrant"), QStringLiteral("quadrantChart"),
       QStringLiteral("quadrantChart\ntitle Reach\nx-axis Low --> High\ny-axis Down --> Up\n"
                      "quadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n\"A\": [0.3, 0.7]")},
      {QStringLiteral("journey"), QStringLiteral("journey"),
       QStringLiteral("journey\ntitle Work\nsection Morning\nMake tea: 5: Me\nDo work: 3: Me, You")},
      {QStringLiteral("radar"), QStringLiteral("radar"),
       QStringLiteral("radar-beta\naxis Quality,Speed,Cost\ncurve Team {8,6,4}")},
      {QStringLiteral("xychart"), QStringLiteral("xychart"),
       QStringLiteral("xychart-beta\ntitle Sales\nx-axis [Jan, Feb, Mar]\n"
                      "y-axis 0 --> 100\nbar [20, 50, 80]\nline [10, 60, 90]")},
      {QStringLiteral("timeline"), QStringLiteral("timeline"),
       QStringLiteral("timeline\ntitle Releases\nsection 2026\nAlpha : API ready\nBeta : Ship")},
      {QStringLiteral("packet"), QStringLiteral("packet"),
       QStringLiteral("packet-beta\ntitle Frame\n0-7: \"Header\"\n8-15: \"Payload\"")},
      {QStringLiteral("kanban"), QStringLiteral("kanban"),
       QStringLiteral("kanban\n  todo[Todo]\n    task1[Write docs]\n"
                      "  done[Done]\n    task2[Ship]")},
      {QStringLiteral("mindmap"), QStringLiteral("mindmap"),
       QStringLiteral("mindmap\n  root((Root))\n    Alpha\n    Beta")},
      {QStringLiteral("block"), QStringLiteral("block"),
       QStringLiteral("block-beta\ncolumns 2\nA[\"Alpha\"] B(\"Beta\")\nA --> B")},
      {QStringLiteral("gitGraph"), QStringLiteral("gitGraph"),
       QStringLiteral("gitGraph\ncommit id: \"root\"\nbranch feature\n"
                      "commit id: \"feature-1\"\ncheckout main\n"
                      "merge feature id: \"release\"")},
      {QStringLiteral("c4"), QStringLiteral("c4"),
       QStringLiteral("C4Context\ntitle System context\n"
                      "Person(user, \"User\")\n"
                      "System(app, \"Application\")\n"
                      "Rel(user, app, \"Uses\")")},
      {QStringLiteral("swimlane"), QStringLiteral("flowchart"),
       QStringLiteral("swimlane-beta TB\nsubgraph one[One]\n"
                      "  A[Start] --> B[Done]\nend")},
      {QStringLiteral("gantt"), QStringLiteral("gantt"),
       QStringLiteral("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
                      "section Delivery\nBuild :build, 2024-01-01, 3d\n"
                      "Ship :ship, after build, 2d")},
      {QStringLiteral("info"), QStringLiteral("info"),
       QStringLiteral("info showInfo"), false},
      {QStringLiteral("treeView"), QStringLiteral("treeView"),
       QStringLiteral("treeView-beta\nproject/\n  src/\n    main.cpp\n  README.md")},
      {QStringLiteral("eventmodeling"), QStringLiteral("eventmodeling"),
       QStringLiteral("eventmodeling\ntf 1 ui Start\n"
                      "tf 2 cmd Submit ->> 1\n"
                      "tf 3 evt Submitted ->> 2")},
      {QStringLiteral("ishikawa"), QStringLiteral("ishikawa"),
       QStringLiteral("ishikawa\nEffect\n  Cause A\n  Cause B")},
      {QStringLiteral("venn"), QStringLiteral("venn"),
       QStringLiteral("venn-beta\ntitle Sets\nset A: 10\nset B: 8\n"
                      "union A,B: 3")},
      {QStringLiteral("sankey"), QStringLiteral("sankey"),
       QStringLiteral("sankey-beta\nA,B,8\nB,C,5\nB,D,3")},
      {QStringLiteral("treemap"), QStringLiteral("treemap"),
       QStringLiteral("treemap-beta\n\"Root\"\n  \"A\": 8\n  \"B\": 5")},
      {QStringLiteral("cynefin"), QStringLiteral("cynefin"),
       QStringLiteral("%%{init: {\"cynefin\": {\"seed\": 17}}}%%\n"
                      "cynefin-beta\nclear\n  \"Standardise\"")},
      {QStringLiteral("wardley"), QStringLiteral("wardley"),
       QStringLiteral("wardley-beta\ntitle Platform map\n"
                      "component User [0.9,0.1]\n"
                      "component Service [0.5,0.5]\nUser -> Service")},
      {QStringLiteral("railroad"), QStringLiteral("railroad"),
       QStringLiteral("railroad-beta\nA=terminal('a');")},
      {QStringLiteral("railroad-ebnf"), QStringLiteral("railroad"),
       QStringLiteral("railroad-ebnf-beta\nA='a' | B;")},
      {QStringLiteral("railroad-abnf"), QStringLiteral("railroad"),
       QStringLiteral("railroad-abnf-beta\nA=\"a\" / B;")},
      {QStringLiteral("railroad-peg"), QStringLiteral("railroad"),
       QStringLiteral("railroad-peg-beta\nA<-'a'/B;")},
  };
  for (const FamilyCase& family : families) {
    const MermaidSvgRenderResult first = renderSvg(family.source);
    const MermaidSvgRenderResult second = renderSvg(family.source);
    require(first.svg == second.svg,
            family.name + QStringLiteral(" SVG output is not deterministic"));
    const QMap<QString, QString> root = svgRootAttributes(first.svg);
    require(root.value(QStringLiteral("class")) ==
                QStringLiteral("mfn-mermaid ") + family.cssClass &&
                root.value(QStringLiteral("width")) == QLatin1String("100%") &&
                !root.contains(QStringLiteral("height")) &&
                root.value(QStringLiteral("style")).startsWith(
                    QLatin1String("max-width: ")) &&
                root.value(QStringLiteral("role")) ==
                    QLatin1String("graphics-document document") &&
                (family.emitsViewBox ==
                 !root.value(QStringLiteral("viewBox")).isEmpty()),
            family.name + QStringLiteral(" SVG root contract drifted"));
    requireRenderable(first.svg, family.name);

    const qsizetype comma = first.dataUrl.indexOf(QLatin1Char(','));
    require(first.dataUrl.startsWith(
                QStringLiteral("data:image/svg+xml;base64,")) && comma > 0 &&
                QByteArray::fromBase64(
                    first.dataUrl.mid(comma + 1).toLatin1()) == first.svg,
            family.name + QStringLiteral(" SVG data URL drifted"));
  }

  // Cross-family fractional-viewBox oracle (the client-box contract is not
  // state-only): architecture's scene exposes svgClientViewBox(), so the
  // SERIALIZED root must carry the browser's exact fractional viewBox —
  // origin included (upstream setupGraphViewbox writes svgBBox ± padding
  // with NO translate). The theme-css fixture locks the browser value per
  // case; 0.2px covers the residual Qt/Chromium shaper difference on label
  // advances (the same tolerance the theme-css comparator uses, three
  // orders below any real measurement-feedback delta).
  if (argc > 1) {
    QFile themeCssFixture(QString::fromLocal8Bit(argv[1]));
    require(themeCssFixture.open(QIODevice::ReadOnly),
            QStringLiteral("theme-css fixture unreadable: %1")
                .arg(themeCssFixture.errorString()));
    const QJsonArray cases = QJsonDocument::fromJson(
                                 themeCssFixture.readAll())
                                 .object()
                                 .value(QStringLiteral("cases"))
                                 .toArray();
    bool checkedArchitecture = false;
    for (const QJsonValue& caseValue : cases) {
      const QJsonObject fixtureCase = caseValue.toObject();
      if (fixtureCase.value(QStringLiteral("id")).toString() !=
          QLatin1String("architecture-paint"))
        continue;
      const MermaidSvgRenderResult exported =
          renderSvg(fixtureCase.value(QStringLiteral("source")).toString());
      const QStringList exportedBox =
          svgRootAttributes(exported.svg)
              .value(QStringLiteral("viewBox"))
              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      const QStringList browserBox =
          fixtureCase.value(QStringLiteral("viewBox"))
              .toString()
              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      require(exportedBox.size() == 4 && browserBox.size() == 4,
              QStringLiteral("architecture fractional viewBox shape: '%1' vs '%2'")
                  .arg(exportedBox.join(QLatin1Char(' ')),
                       browserBox.join(QLatin1Char(' '))));
      for (int component = 0; component < 4; ++component)
        require(std::abs(exportedBox.at(component).toDouble() -
                         browserBox.at(component).toDouble()) <= 0.2,
                QStringLiteral("architecture viewBox[%1] %2 vs browser %3")
                    .arg(component)
                    .arg(exportedBox.at(component),
                         browserBox.at(component)));
      checkedArchitecture = true;
    }
    require(checkedArchitecture,
            QStringLiteral("architecture-paint missing from theme-css fixture"));

    // The client-box contract is not state-only: the sibling fixtures carry
    // browser viewBox oracles for the OTHER client-box families. Flowchart
    // exports must carry the exact FRACTIONAL extents (the browser writes
    // `0 0 426.75 70` — an integer canvas or a rounded naturalSize would
    // drift by up to a pixel), and the architecture TITLE case must NOT
    // grow a shared title band (upstream stores the title but its draw
    // never renders it: same viewBox as the untitled case).
    const QString fixtureDir = QFileInfo(
        QString::fromLocal8Bit(argv[1])).absolutePath();
    const auto loadFixtureCases = [&fixtureDir](const QString& fileName) {
      QFile fixture(fixtureDir + QLatin1Char('/') + fileName);
      require(fixture.open(QIODevice::ReadOnly),
              QStringLiteral("%1 unreadable: %2")
                  .arg(fileName, fixture.errorString()));
      return QJsonDocument::fromJson(fixture.readAll()).object()
          .value(QStringLiteral("cases")).toArray();
    };
    int flowchartBoxesChecked = 0;
    QStringList flowchartBoxMismatches;
#ifdef Q_OS_WIN
    for (const QJsonValue& caseValue : loadFixtureCases(
             QStringLiteral("flowchart-geometry.json"))) {
      const QJsonObject fixtureCase = caseValue.toObject();
      const QString browserBox = fixtureCase.value(QStringLiteral("expected"))
          .toObject().value(QStringLiteral("svg")).toObject()
          .value(QStringLiteral("viewBox")).toString();
      const QStringList parts =
          browserBox.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (parts.size() != 4) continue;
      // The generator renders through EXTERNAL mermaid.initialize (fontFamily
      // Arial, htmlLabels false, 50/50 spacing, per-case curve) — not through
      // the source. The native render must mirror that config via the init
      // directive or the comparison is two different environments (trebuchet
      // vs Arial alone diverges node widths by ~2%).
      QJsonObject flowchartConfig{{QStringLiteral("htmlLabels"), false},
                                  {QStringLiteral("nodeSpacing"), 50},
                                  {QStringLiteral("rankSpacing"), 50}};
      const QString curve = fixtureCase.value(QStringLiteral("curve")).toString();
      if (!curve.isEmpty()) flowchartConfig.insert(QStringLiteral("curve"), curve);
      const QJsonObject init{{QStringLiteral("fontFamily"), QStringLiteral("Arial")},
                             {QStringLiteral("flowchart"), flowchartConfig}};
      const QString source = QStringLiteral("%%{init: %1}%%\n%2")
          .arg(QString::fromUtf8(QJsonDocument(init).toJson(QJsonDocument::Compact)),
               fixtureCase.value(QStringLiteral("source")).toString());
      const QStringList exportedBox = svgRootAttributes(
          renderSvg(source).svg)
              .value(QStringLiteral("viewBox"))
              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      require(exportedBox.size() == 4,
              QStringLiteral("flowchart %1 viewBox shape: '%2' vs browser '%3'")
                  .arg(fixtureCase.value(QStringLiteral("id")).toString(),
                       exportedBox.join(QLatin1Char(' ')), browserBox));
      for (int component = 0; component < 4; ++component) {
        const double delta = exportedBox.at(component).toDouble() -
                             parts.at(component).toDouble();
        const QString id = fixtureCase.value(QStringLiteral("id")).toString();
        if (std::abs(delta) > 0.2) {
          flowchartBoxMismatches.append(
              QStringLiteral("%1[%2] %3 vs %4")
                  .arg(id).arg(component)
                  .arg(exportedBox.at(component), parts.at(component)));
        }
      }
      ++flowchartBoxesChecked;
    }
    require(flowchartBoxesChecked >= 70,
            QStringLiteral("flowchart fractional viewBox coverage regressed: %1")
                .arg(flowchartBoxesChecked));
    require(flowchartBoxMismatches.isEmpty(),
            QStringLiteral("flowchart viewBox divergence (%1 of %2): %3")
                .arg(flowchartBoxMismatches.size())
                .arg(flowchartBoxesChecked)
                .arg(flowchartBoxMismatches.join(QStringLiteral("; "))));
#else
    std::fprintf(stderr,
                 "NOTE: flowchart fractional viewBox oracle is Windows-only "
                 "(DirectWrite-recorded deltas; other platforms are the recorded "
                 "Linux-font/platform workstream)\n");
#endif
    bool checkedArchitectureTitle = false;
    for (const QJsonValue& caseValue : loadFixtureCases(
             QStringLiteral("architecture-geometry.json"))) {
      const QJsonObject fixtureCase = caseValue.toObject();
      if (fixtureCase.value(QStringLiteral("id")).toString() !=
          QLatin1String("title"))
        continue;
      const QString browserBox = fixtureCase.value(QStringLiteral("expected"))
          .toObject().value(QStringLiteral("root")).toObject()
          .value(QStringLiteral("attrs")).toObject()
          .value(QStringLiteral("viewBox")).toString();
      const QStringList parts =
          browserBox.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      const QStringList exportedBox = svgRootAttributes(
          renderSvg(fixtureCase.value(QStringLiteral("source")).toString()).svg)
              .value(QStringLiteral("viewBox"))
              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      require(parts.size() == 4 && exportedBox.size() == 4,
              QStringLiteral("architecture title viewBox shape: '%1' vs '%2'")
                  .arg(exportedBox.join(QLatin1Char(' ')), browserBox));
      for (int component = 0; component < 4; ++component)
        require(std::abs(exportedBox.at(component).toDouble() -
                         parts.at(component).toDouble()) <= 0.2,
                QStringLiteral("architecture title viewBox[%1] %2 vs browser %3")
                    .arg(component)
                    .arg(exportedBox.at(component), parts.at(component)));
      // No title band: the root must not carry a visible-title strip (the
      // exported canvas height equals the untitled content height).
      checkedArchitectureTitle = true;
    }
    require(checkedArchitectureTitle,
            QStringLiteral("architecture title case missing from fixture"));
  }

  const QString accessibleFlow = QStringLiteral(
      "---\ntitle: Visible title\n---\n"
      "flowchart TB\naccTitle: Accessible & precise\n"
      "accDescr: Detailed <safe> description\n"
      "A[Docs] --> B[Blocked]\n"
      "click A href \"https://example.com/docs?a=1&b=2\" \"Open docs\" _blank\n"
      "click B href \"javascript:alert(1)\" \"Blocked link\"");
  const MermaidSvgRenderResult accessible = renderSvg(accessibleFlow);
  const QMap<QString, QString> accessibleRoot =
      svgRootAttributes(accessible.svg);
  require(accessibleRoot.value(QStringLiteral("aria-labelledby")) ==
              accessibleRoot.value(QStringLiteral("id")) +
                  QStringLiteral("-title") &&
              accessibleRoot.value(QStringLiteral("aria-describedby")) ==
                  accessibleRoot.value(QStringLiteral("id")) +
                      QStringLiteral("-desc") &&
              accessible.svg.contains("Accessible &amp; precise") &&
              accessible.svg.contains("Detailed &lt;safe&gt; description") &&
              accessible.svg.contains("href=\"https://example.com/docs?a=1&amp;b=2\"") &&
              accessible.svg.contains("target=\"_blank\"") &&
              accessible.svg.contains("rel=\"noopener noreferrer\"") &&
              !accessible.svg.contains("javascript:"),
          QStringLiteral("SVG accessibility or safe-link contract drifted"));
  require(accessible.svg.contains("<title>Open docs</title>"),
          QStringLiteral("SVG link tooltip must be serialized as a <title>"));

  const QString deterministicSource = QStringLiteral(
      "%%{init: {\"deterministicIds\": true, "
      "\"deterministicIDSeed\": \"seed\"}}%%\n"
      "flowchart LR\nA --> B");
  const QMap<QString, QString> deterministicRoot =
      svgRootAttributes(renderSvg(deterministicSource).svg);
  const QMap<QString, QString> indexedRoot =
      svgRootAttributes(renderSvg(deterministicSource, 2).svg);
  require(deterministicRoot.value(QStringLiteral("id")) ==
              QLatin1String("mermaid-4") &&
              indexedRoot.value(QStringLiteral("id")) ==
                  QLatin1String("mermaid-6") &&
              deterministicRoot.value(QStringLiteral("id")) !=
                  indexedRoot.value(QStringLiteral("id")),
          QStringLiteral("deterministicIds/seed or instance indexing drifted"));

  const QString markerSource = QStringLiteral(
      "%%{init: {\"arrowMarkerAbsolute\": true}}%%\n"
      "flowchart LR\nA --> B");
  const QUrl markerDocumentUrl =
      QUrl::fromLocalFile(QStringLiteral("G:/github/mermaid-cli/index.html"));
  const MermaidSvgRenderResult markerSvg =
      MermaidRenderCache::renderMermaidSourceToSvg(
          markerSource, 0, markerDocumentUrl, QStringLiteral("marker-visual"));
  require(markerSvg.svg.contains("marker-end=\"url(file:///G:/github/mermaid-cli/index.html#marker-visual_flowchart-v2-pointEnd)\""),
          QStringLiteral("Absolute Flowchart marker reference drifted"));
  const MermaidRenderEntry markerEntry = MermaidRenderCache().getSync(
      MermaidRenderCache::makeKey(markerSource), markerSource);
  require(differentPixels(paintScene(markerEntry, true),
                          paintScene(markerEntry, false)) >= 8,
          QStringLiteral("Scene marker geometry does not produce visible arrow ink"));

  const QVector<QString> fixedWidthSources = {
      QStringLiteral(
          "%%{init: {\"flowchart\": {\"useMaxWidth\": false}}}%%\n"
          "flowchart TB\nA --> B"),
      QStringLiteral(
          "%%{init: {\"sequence\": {\"useMaxWidth\": false}}}%%\n"
          "sequenceDiagram\nA->>B: Hi"),
      QStringLiteral(
          "%%{init: {\"state\": {\"useMaxWidth\": false}}}%%\n"
          "stateDiagram-v2\n[*] --> Idle"),
      QStringLiteral(
          "%%{init: {\"quadrantChart\": {\"useMaxWidth\": false}}}%%\n"
          "quadrantChart\n\"A\": [0.5, 0.5]"),
      QStringLiteral(
          "%%{init: {\"journey\": {\"useMaxWidth\": false}}}%%\n"
          "journey\nsection S\nTask: 5: A"),
      QStringLiteral(
          "%%{init: {\"radar\": {\"useMaxWidth\": false}}}%%\n"
          "radar-beta\naxis A,B,C\ncurve C {1,2,3}"),
      QStringLiteral(
          "%%{init: {\"timeline\": {\"useMaxWidth\": false}}}%%\n"
          "timeline\nsection S\nTask : Event"),
      QStringLiteral(
          "%%{init: {\"packet\": {\"useMaxWidth\": false}}}%%\n"
          "packet-beta\n0-7: \"Header\""),
      QStringLiteral(
          "%%{init: {\"mindmap\": {\"useMaxWidth\": false}}}%%\n"
          "kanban\n  todo[Todo]\n    task1[Write docs]"),
      QStringLiteral(
          "%%{init: {\"mindmap\": {\"useMaxWidth\": false}}}%%\n"
          "mindmap\n  root((Root))\n    Child"),
      QStringLiteral(
          "%%{init: {\"block\": {\"useMaxWidth\": false}}}%%\n"
          "block-beta\nA[\"Alpha\"]"),
      QStringLiteral(
          "%%{init: {\"gitGraph\": {\"useMaxWidth\": false}}}%%\n"
          "gitGraph\ncommit id: \"a\""),
      QStringLiteral(
          "%%{init: {\"c4\": {\"useMaxWidth\": false}}}%%\n"
          "C4Context\nPerson(user, \"User\")"),
      QStringLiteral(
          "%%{init: {\"flowchart\": {\"useMaxWidth\": false}}}%%\n"
          "swimlane-beta\nA --> B"),
      QStringLiteral(
          "%%{init: {\"treeView\": {\"useMaxWidth\": false}}}%%\n"
          "treeView-beta\nproject/\n  child"),
      QStringLiteral(
          "%%{init: {\"eventmodeling\": {\"useMaxWidth\": false}}}%%\n"
          "eventmodeling\ntf 1 evt Created"),
      QStringLiteral(
          "%%{init: {\"ishikawa\": {\"useMaxWidth\": false}}}%%\n"
          "ishikawa\nEffect\n  Cause A"),
      QStringLiteral(
          "%%{init: {\"venn\": {\"useMaxWidth\": false}}}%%\n"
          "venn-beta\nset A: 10\nset B: 8\nunion A,B: 3"),
      QStringLiteral(
          "%%{init: {\"sankey\": {\"useMaxWidth\": false}}}%%\n"
          "sankey-beta\nA,B,8\nB,C,5\nB,D,3"),
      QStringLiteral(
          "%%{init: {\"treemap\": {\"useMaxWidth\": false}}}%%\n"
          "treemap-beta\n\"Root\"\n  \"A\": 8\n  \"B\": 5"),
      QStringLiteral(
          "%%{init: {\"cynefin\": {\"useMaxWidth\": false, "
          "\"seed\": 17}}}%%\ncynefin-beta\nclear\n  \"Standardise\""),
      QStringLiteral(
          "%%{init: {\"gantt\": {\"useMaxWidth\": false, "
          "\"useWidth\": 640}}}%%\n"
          "gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
          "Task :task, 2024-01-01, 2d"),
      QStringLiteral(
          "%%{init: {\"railroad\": {\"useMaxWidth\": false}}}%%\n"
          "railroad-ebnf-beta\nA='a' | B;"),
  };
  for (const QString& source : fixedWidthSources) {
    const QMap<QString, QString> root = svgRootAttributes(renderSvg(source).svg);
    require(root.value(QStringLiteral("width")) != QLatin1String("100%") &&
                root.value(QStringLiteral("width")).toDouble() > 0.0 &&
                root.value(QStringLiteral("height")).toDouble() > 0.0 &&
                !root.contains(QStringLiteral("style")),
            QStringLiteral("useMaxWidth=false did not reach SVG sizing: "
                           "width='%1' height='%2' style='%3' for %4")
                .arg(root.value(QStringLiteral("width")),
                     root.value(QStringLiteral("height")),
                     root.value(QStringLiteral("style")),
                     source.left(60).replace(QLatin1Char('\n'), QLatin1Char(' '))));
  }
  // Fixed sizing on a client-box family writes the FRACTIONAL client box as
  // the width/height attributes — upstream svg.attr('width', viewBoxWidth)
  // (a local Chrome oracle measures width="207.84375" height="70.5" for this
  // flowchart; the state oracle's is width="42.671875"). The attributes must
  // equal the max-width-mode viewBox components, not the raster ints.
  {
    const QString body = QStringLiteral(
        "flowchart LR\nA[Alpha] --> B[Beta] --> C[Gamma]");
    const QMap<QString, QString> fixed = svgRootAttributes(
        renderSvg(QStringLiteral(
                      "%%{init: {\"flowchart\": {\"useMaxWidth\": false}}}%%\n") +
                  body).svg);
    const QStringList viewBox = svgRootAttributes(renderSvg(body).svg)
                                    .value(QStringLiteral("viewBox"))
                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    require(fixed.value(QStringLiteral("width")) !=
                    QLatin1String("100%") &&
                viewBox.size() == 4 &&
                std::abs(fixed.value(QStringLiteral("width")).toDouble() -
                         viewBox.at(2).toDouble()) < 0.01 &&
                std::abs(fixed.value(QStringLiteral("height")).toDouble() -
                         viewBox.at(3).toDouble()) < 0.01,
            QStringLiteral("useMaxWidth=false fractional width/height "
                           "drifted: %1x%2 vs viewBox %3x%4")
                .arg(fixed.value(QStringLiteral("width")),
                     fixed.value(QStringLiteral("height")),
                     viewBox.value(2), viewBox.value(3)));
  }

  const QString classOwnWidth = QStringLiteral(
      "%%{init: {\"class\": {\"useMaxWidth\": false}}}%%\n"
      "classDiagram\nclass A");
  const QString classViaStateWidth = QStringLiteral(
      "%%{init: {\"state\": {\"useMaxWidth\": false}}}%%\n"
      "classDiagram\nclass A");
  require(svgRootAttributes(renderSvg(classOwnWidth).svg)
                  .value(QStringLiteral("width")) == QLatin1String("100%") &&
              svgRootAttributes(renderSvg(classViaStateWidth).svg)
                      .value(QStringLiteral("width")) != QLatin1String("100%"),
          QStringLiteral("Class unified-renderer useMaxWidth parity drifted"));

  const QString sequenceMenu = QStringLiteral(
      "%%{init: {\"sequence\": {\"forceMenus\": true}}}%%\n"
      "sequenceDiagram\nparticipant A as Browser\nparticipant B as API\n"
      "links A: {\"Docs\":\"https://example.com/docs\","
      "\"Blocked\":\"javascript:alert(1)\"}\nA->>B: request");
  const QByteArray menuSvg = renderSvg(sequenceMenu).svg;
  require(menuSvg.contains("href=\"https://example.com/docs\"") &&
              menuSvg.contains("<title>Docs</title>") &&
              !menuSvg.contains("javascript:"),
          QStringLiteral("Sequence SVG menu link + accessible label sanitization drifted"));

  const QByteArray journeyAria = renderSvg(QStringLiteral(
      "journey\naccTitle: Journey accessible\n"
      "accDescr: Journey description\nsection S\ntask: 5")).svg;
  require(journeyAria.contains("<title") &&
              journeyAria.contains("Journey accessible</title>") &&
              journeyAria.contains("<desc") &&
              journeyAria.contains("Journey description</desc>") &&
              journeyAria.contains("aria-labelledby=") &&
              journeyAria.contains("aria-describedby="),
          QStringLiteral("Journey SVG accessibility metadata drifted"));

  const QByteArray radarAria = renderSvg(QStringLiteral(
      "radar-beta\naccTitle: Radar accessible\n"
      "accDescr: Radar description\naxis A,B,C\ncurve C {1,2,3}")).svg;
  require(radarAria.contains("<title") &&
              radarAria.contains("Radar accessible</title>") &&
              radarAria.contains("<desc") &&
              radarAria.contains("Radar description</desc>") &&
              radarAria.contains("aria-labelledby=") &&
              radarAria.contains("aria-describedby="),
          QStringLiteral("Radar SVG accessibility metadata drifted"));

  const QByteArray xyChartAria = renderSvg(QStringLiteral(
      "xychart-beta\naccTitle: XY accessible\n"
      "accDescr: XY description\nx-axis [A,B,C]\n"
      "y-axis 0 --> 3\nbar [1,2,3]")).svg;
  require(xyChartAria.contains("<title") &&
              xyChartAria.contains("XY accessible</title>") &&
              xyChartAria.contains("<desc") &&
              xyChartAria.contains("XY description</desc>") &&
              xyChartAria.contains("aria-labelledby=") &&
              xyChartAria.contains("aria-describedby="),
          QStringLiteral("XYChart SVG accessibility metadata drifted"));

  const QByteArray timelineAria = renderSvg(QStringLiteral(
      "timeline\naccTitle: Timeline accessible\n"
      "accDescr: Timeline description\nsection S\nTask : Event")).svg;
  require(timelineAria.contains("<title") &&
              timelineAria.contains("Timeline accessible</title>") &&
              timelineAria.contains("<desc") &&
              timelineAria.contains("Timeline description</desc>") &&
              timelineAria.contains("aria-labelledby=") &&
              timelineAria.contains("aria-describedby="),
          QStringLiteral("Timeline SVG accessibility metadata drifted"));

  const QByteArray gitGraphAria = renderSvg(QStringLiteral(
      "gitGraph\ntitle Release history\naccTitle: Git accessible\n"
      "accDescr: Git description\ncommit id: \"root\"\n"
      "commit id: \"release\"")).svg;
  require(gitGraphAria.contains("Release history") &&
              gitGraphAria.contains("Git accessible</title>") &&
              gitGraphAria.contains("Git description</desc>") &&
              gitGraphAria.contains("aria-labelledby=") &&
              gitGraphAria.contains("aria-describedby="),
          QStringLiteral("GitGraph title/accessibility metadata drifted"));

  const QByteArray treemapAria = renderSvg(QStringLiteral(
      "treemap-beta\ntitle Visible map\naccTitle: Treemap accessible\n"
      "accDescr: Treemap description\n\"Root\"\n  \"A\": 8\n  \"B\": 5")).svg;
  require(treemapAria.contains("Visible map") &&
              treemapAria.contains("Treemap accessible</title>") &&
              treemapAria.contains("Treemap description</desc>") &&
              treemapAria.contains("aria-labelledby=") &&
              treemapAria.contains("aria-describedby="),
          QStringLiteral("Treemap title/accessibility metadata drifted"));

  const QByteArray cynefinAria = renderSvg(QStringLiteral(
      "%%{init: {\"cynefin\": {\"seed\": 17}}}%%\n"
      "cynefin-beta\ntitle Visible landscape\n"
      "accTitle: Cynefin accessible\naccDescr: Cynefin description\n"
      "clear\n  \"Standardise\"")).svg;
  require(cynefinAria.contains("Visible landscape") &&
              cynefinAria.contains("Cynefin accessible</title>") &&
              cynefinAria.contains("Cynefin description</desc>") &&
              cynefinAria.contains("aria-labelledby=") &&
              cynefinAria.contains("aria-describedby="),
          QStringLiteral("Cynefin title/accessibility metadata drifted"));

  const QByteArray wardleyAria = renderSvg(QStringLiteral(
      "wardley-beta\ntitle Visible map\naccTitle: Wardley accessible\n"
      "accDescr: Wardley description\ncomponent A [0.5,0.5]")).svg;
  require(wardleyAria.contains("Visible map") &&
              wardleyAria.contains("Wardley accessible</title>") &&
              wardleyAria.contains("Wardley description</desc>") &&
              wardleyAria.contains("aria-labelledby=") &&
              wardleyAria.contains("aria-describedby="),
          QStringLiteral("Wardley title/accessibility metadata drifted"));

  const QByteArray railroadAria = renderSvg(QStringLiteral(
      "railroad-ebnf-beta\naccTitle: Railroad accessible\n"
      "accDescr: Railroad description\nA='a' | B;")).svg;
  require(railroadAria.contains("Railroad accessible</title>") &&
              railroadAria.contains("Railroad description</desc>") &&
              railroadAria.contains("aria-labelledby=") &&
              railroadAria.contains("aria-describedby="),
          QStringLiteral("Railroad SVG accessibility metadata drifted"));

  const QByteArray packetAria = renderSvg(QStringLiteral(
      "packet-beta\naccTitle: Packet accessible\n"
      "accDescr: Packet description\n0-7: \"Header\"")).svg;
  require(packetAria.contains("<title") &&
              packetAria.contains("Packet accessible</title>") &&
              packetAria.contains("<desc") &&
              packetAria.contains("Packet description</desc>") &&
              packetAria.contains("aria-labelledby=") &&
              packetAria.contains("aria-describedby="),
          QStringLiteral("Packet SVG accessibility metadata drifted"));

  const QByteArray c4Aria = renderSvg(QStringLiteral(
      "C4Context\ntitle Visible C4 title\n"
      "accTitle: C4 accessible\naccDescr: C4 description\n"
      "Person(user, \"User\")")).svg;
  require(!c4Aria.contains("Visible C4 title") &&
              c4Aria.contains("C4 accessible") &&
              c4Aria.contains("C4 description</desc>") &&
              !c4Aria.contains("aria-labelledby=") &&
              c4Aria.contains("aria-describedby="),
          QStringLiteral("C4 accTitle overwrite/accessibility quirk drifted"));

  const QByteArray treeViewAria = renderSvg(QStringLiteral(
      "---\ntitle: Invisible frontmatter tree title\n---\n"
      "treeView-beta\ntitle Invisible inline tree title\n"
      "accTitle: Tree accessible\naccDescr: Tree description\n"
      "root/\n  child")).svg;
  require(treeViewAria.contains("Tree accessible</title>") &&
              treeViewAria.contains("Tree description</desc>") &&
              treeViewAria.contains("aria-labelledby=") &&
              treeViewAria.contains("aria-describedby=") &&
              !treeViewAria.contains("Invisible frontmatter tree title") &&
              !treeViewAria.contains("Invisible inline tree title"),
          QStringLiteral("TreeView SVG title/accessibility metadata drifted"));

  const QByteArray treeViewIcons = renderSvg(QStringLiteral(
      "%%{init: {\"treeView\": {\"showIcons\": true}}}%%\n"
      "treeView-beta\nroot/\n  child.txt")).svg;
  require(!treeViewIcons.contains("<use"),
          QStringLiteral("TreeView stripped-icon SVG quirk drifted"));

  const QByteArray ganttAria = renderSvg(QStringLiteral(
      "gantt\ntitle Release plan\naccTitle: Gantt accessible\n"
      "accDescr: Gantt description\ndateFormat YYYY-MM-DD\n"
      "todayMarker off\nTask :task, 2024-01-01, 2d")).svg;
  require(ganttAria.contains("Release plan") &&
              ganttAria.contains("Gantt accessible</title>") &&
              ganttAria.contains("Gantt description</desc>") &&
              ganttAria.contains("aria-labelledby=") &&
              ganttAria.contains("aria-describedby="),
          QStringLiteral("Gantt SVG title/accessibility metadata drifted"));

  const QByteArray ganttLinks = renderSvg(QStringLiteral(
      "gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
      "Safe :safe, 2024-01-01, 1d\n"
      "Blocked :blocked, 2024-01-02, 1d\n"
      "click safe href \"https://example.org/gantt\"\n"
      "click blocked href \"javascript:alert(1)\"")).svg;
  require(ganttLinks.contains("href=\"https://example.org/gantt\"") &&
              !ganttLinks.contains("javascript:"),
          QStringLiteral("Gantt SVG link sanitization drifted"));

  // Kanban has no title/accessibility grammar and ignores frontmatter title in
  // 11.16.0. It keeps only the root graphics-document role/roledescription;
  // do not accidentally introduce the shared visible title strip or SVG ARIA.
  const QByteArray kanbanNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Board title\n---\n"
      "kanban\n  todo[Todo]\n    task1[Write docs]")).svg;
  require(!kanbanNoTitle.contains("Board title") &&
              !kanbanNoTitle.contains("aria-labelledby=") &&
              !kanbanNoTitle.contains("aria-describedby="),
          QStringLiteral("Kanban frontmatter title must remain invisible"));

  const QByteArray mindmapNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Mindmap title\n---\n"
      "mindmap\n  root((Root))\n    Child")).svg;
  require(!mindmapNoTitle.contains("Mindmap title") &&
              !mindmapNoTitle.contains("aria-labelledby=") &&
              !mindmapNoTitle.contains("aria-describedby="),
          QStringLiteral("Mindmap frontmatter title must remain invisible"));

  const QByteArray blockNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Block title\n---\n"
      "block-beta\nA[\"Alpha\"]")).svg;
  require(!blockNoTitle.contains("Block title") &&
              !blockNoTitle.contains("aria-labelledby=") &&
              !blockNoTitle.contains("aria-describedby="),
          QStringLiteral("Block frontmatter title must remain invisible"));

  const QByteArray infoNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Info frontmatter\n---\n"
      "info\ntitle Inline\naccTitle: AT\naccDescr: AD")).svg;
  require(!infoNoTitle.contains("Info frontmatter") &&
              !infoNoTitle.contains("Inline") &&
              !infoNoTitle.contains("aria-labelledby=") &&
              !infoNoTitle.contains("aria-describedby=") &&
              !infoNoTitle.contains("viewBox="),
          QStringLiteral("Info metadata/viewBox must remain renderer-inert"));

  const QByteArray eventNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Event frontmatter\n---\n"
      "eventmodeling\ntf 1 evt Created")).svg;
  require(!eventNoTitle.contains("Event frontmatter") &&
              !eventNoTitle.contains("aria-labelledby=") &&
              !eventNoTitle.contains("aria-describedby="),
          QStringLiteral("Event Modeling frontmatter title must remain invisible"));

  const QByteArray ishikawaNoTitle = renderSvg(QStringLiteral(
      "---\ntitle: Ishikawa frontmatter\n---\n"
      "ishikawa\nEffect\n  title Metadata-looking cause\n"
      "  accTitle: Still a cause")).svg;
  require(!ishikawaNoTitle.contains("Ishikawa frontmatter") &&
              !ishikawaNoTitle.contains("aria-labelledby=") &&
              !ishikawaNoTitle.contains("aria-describedby="),
          QStringLiteral("Ishikawa frontmatter title must remain invisible"));

  const QByteArray vennNoAria = renderSvg(QStringLiteral(
      "---\ntitle: Venn frontmatter\n---\n"
      "venn-beta\ntitle Visible Venn title\nset A: 10\nset B: 8\n"
      "union A,B: 3")).svg;
  require(!vennNoAria.contains("Venn frontmatter") &&
              !vennNoAria.contains("aria-labelledby=") &&
              !vennNoAria.contains("aria-describedby="),
          QStringLiteral("Venn must own its title without common ARIA"));

  const QByteArray mindmapSafeLink = renderSvg(QStringLiteral(
      "mindmap\n  root((Root))\n"
      "    id[\"<a href='https://example.org/docs'>Docs</a>\"]")).svg;
  const QByteArray mindmapBlockedLink = renderSvg(QStringLiteral(
      "mindmap\n  root((Root))\n"
      "    id[\"<a href='javascript:alert(1)'>Blocked</a>\"]")).svg;
  require(mindmapSafeLink.contains(
              "href=\"https://example.org/docs\"") &&
              !mindmapBlockedLink.contains("javascript:"),
          QStringLiteral("Mindmap inline HTML link sanitization drifted"));

  const QByteArray kanbanTicket = renderSvg(QStringLiteral(
      "%%{init: {\"kanban\": {\"ticketBaseUrl\": "
      "\"https://example.test/items/#TICKET#\"}}}%%\n"
      "kanban\n  todo[Todo]\n"
      "    task1[Write docs]@{ ticket: KAN-7 }")).svg;
  const QByteArray blockedKanbanTicket = renderSvg(QStringLiteral(
      "%%{init: {\"kanban\": {\"ticketBaseUrl\": "
      "\"javascript:alert(#TICKET#)\"}}}%%\n"
      "kanban\n  todo[Todo]\n"
      "    task1[Write docs]@{ ticket: KAN-7 }")).svg;
  require(kanbanTicket.contains(
              "href=\"https://example.test/items/KAN-7\"") &&
              !blockedKanbanTicket.contains("javascript:"),
          QStringLiteral("Kanban SVG ticket link sanitization drifted"));

  // Invalid sources export the upstream error-diagram fallback (mermaid.core
  // leaves the lightbulb SVG in the DOM for every parse/draw failure; mmdc
  // serializes it) — never a partial diagram.
  {
    const QByteArray invalidSvg = MermaidRenderCache::renderMermaidSourceToSvg(
        QStringLiteral("flowchart TB\nA -->")).svg;
    require(!invalidSvg.isEmpty() &&
                invalidSvg.contains("aria-roledescription=\"error\"") &&
                !invalidSvg.contains("flowchart"),
            QStringLiteral(
                "Invalid Mermaid source must export the error fallback SVG"));
  }

  qDebug() << "MermaidSvgExportTest: native families, SVG roots, ARIA,"
              " deterministic IDs, sizing, rendering, and safe links passed";
  return 0;
}
