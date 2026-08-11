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
          "%%{init: {\"gantt\": {\"useMaxWidth\": false, "
          "\"useWidth\": 640}}}%%\n"
          "gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
          "Task :task, 2024-01-01, 2d"),
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

  const QByteArray treemapAria = renderSvg(QStringLiteral(
      "treemap-beta\ntitle Visible map\naccTitle: Treemap accessible\n"
      "accDescr: Treemap description\n\"Root\"\n  \"A\": 8\n  \"B\": 5")).svg;
  require(treemapAria.contains("Visible map") &&
              treemapAria.contains("Treemap accessible</title>") &&
              treemapAria.contains("Treemap description</desc>") &&
              treemapAria.contains("aria-labelledby=") &&
              treemapAria.contains("aria-describedby="),
          QStringLiteral("Treemap title/accessibility metadata drifted"));

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

  require(MermaidRenderCache::renderMermaidSourceToSvg(
              QStringLiteral("flowchart TB\nA -->"))
              .svg.isEmpty(),
          QStringLiteral("Invalid Mermaid source must not export partial SVG"));

  qDebug() << "MermaidSvgExportTest: native families, SVG roots, ARIA,"
              " deterministic IDs, sizing, rendering, and safe links passed";
  return 0;
}
