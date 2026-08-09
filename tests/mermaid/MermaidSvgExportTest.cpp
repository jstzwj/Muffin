#include "mermaid/editor/MermaidRenderCache.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>
#include <QXmlStreamReader>

#include <cstdlib>

using muffin::mermaid::editor::MermaidRenderCache;
using muffin::mermaid::editor::MermaidSvgRenderResult;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
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

  struct FamilyCase {
    QString name;
    QString cssClass;
    QString source;
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
                !root.value(QStringLiteral("viewBox")).isEmpty(),
            family.name + QStringLiteral(" SVG root contract drifted"));
    requireRenderable(first.svg, family.name);

    const qsizetype comma = first.dataUrl.indexOf(QLatin1Char(','));
    require(first.dataUrl.startsWith(
                QStringLiteral("data:image/svg+xml;base64,")) && comma > 0 &&
                QByteArray::fromBase64(
                    first.dataUrl.mid(comma + 1).toLatin1()) == first.svg,
            family.name + QStringLiteral(" SVG data URL drifted"));
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
  };
  for (const QString& source : fixedWidthSources) {
    const QMap<QString, QString> root = svgRootAttributes(renderSvg(source).svg);
    require(root.value(QStringLiteral("width")) != QLatin1String("100%") &&
                root.value(QStringLiteral("width")).toInt() > 0 &&
                root.value(QStringLiteral("height")).toInt() > 0 &&
                !root.contains(QStringLiteral("style")),
            QStringLiteral("useMaxWidth=false did not reach SVG sizing"));
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

  require(MermaidRenderCache::renderMermaidSourceToSvg(
              QStringLiteral("flowchart TB\nA -->"))
              .svg.isEmpty(),
          QStringLiteral("Invalid Mermaid source must not export partial SVG"));

  qDebug() << "MermaidSvgExportTest: native families, SVG roots, ARIA,"
              " deterministic IDs, sizing, rendering, and safe links passed";
  return 0;
}
