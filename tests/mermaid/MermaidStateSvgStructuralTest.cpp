#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  // qCritical alone is swallowed by the ctest capture on Windows — flush the
  // assertion to stderr first (same pattern as the other state tests).
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
bool colorEquals(const QString& css, const QString& expected, int tolerance = 1) {
  const QColor left = color::toQColor(css);
  const QColor right = color::toQColor(expected);
  if (!left.isValid() || !right.isValid()) return !left.isValid() && !right.isValid();
  return std::abs(left.red() - right.red()) <= tolerance &&
         std::abs(left.green() - right.green()) <= tolerance &&
         std::abs(left.blue() - right.blue()) <= tolerance &&
         left.alpha() == right.alpha();
}
template <typename T>
const T* findById(const QVector<T>& values, const QString& id) {
  const auto it = std::find_if(values.cbegin(), values.cend(),
      [&](const T& value) { return value.id == id; });
  return it == values.cend() ? nullptr : &*it;
}
QStringList strings(const QJsonArray& values) {
  QStringList result;
  for (const QJsonValue& value : values) result.append(value.toString());
  return result;
}
QStringList expectedNodeTags(const QString& shape) {
  if (shape == QLatin1String("stateStart")) return {QStringLiteral("circle")};
  if (shape == QLatin1String("stateEnd") || shape == QLatin1String("fork") ||
      shape == QLatin1String("join") || shape == QLatin1String("choice") ||
      shape == QLatin1String("note") || shape == QLatin1String("rectWithTitle"))
    return {QStringLiteral("g")};
  return {QStringLiteral("rect"), QStringLiteral("g")};
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("Expected state structural fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("37694df59c763191710c782df3e61a2e4fb6d01b5e6251b4fc8a0974342ff972"),
          QStringLiteral("State structural fixture drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() ==
              QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("State structural font oracle drifted"));

  editor::MermaidRenderCache cache;
  int nodes = 0, clusters = 0, edges = 0, foreignObjects = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr,
            id + QStringLiteral(": native state scene failed: ") + entry.errorMessage);
    const state::StateScene& scene = *stateScene;
    const QJsonObject structure = fixture.value(QStringLiteral("structure")).toObject();
    const QJsonObject browserRoot = structure.value(QStringLiteral("root")).toObject();
    require(scene.role == browserRoot.value(QStringLiteral("role")).toString() &&
                scene.ariaRoleDescription ==
                    browserRoot.value(QStringLiteral("ariaRoledescription")).toString(),
            id + QStringLiteral(": SVG accessibility root mismatch"));
    // The exported root carries the exact FRACTIONAL client viewBox —
    // ALL FOUR components. Upstream writes svgBBox(content ∪ title) ±
    // padding with NO translate, so the ORIGIN carries the scene's raw
    // coordinates (titled: -30.9921875 -52 104.390625 198; pseudostates:
    // 2 0 86 352) — locking only width/height left the origin unchecked.
    const QStringList viewBoxParts =
        browserRoot.value(QStringLiteral("viewBox")).toString().split(
            QLatin1Char(' '), Qt::SkipEmptyParts);
    if (qEnvironmentVariableIsSet("MUFFIN_DEBUG_STATE_BOX") &&
        viewBoxParts.size() == 4) {
      std::fprintf(stderr, "[%s] browser=%s native-bounds=%g,%g %gx%g\n",
                   qPrintable(id), qPrintable(browserRoot.value(
                       QStringLiteral("viewBox")).toString()),
                   scene.bounds.x(), scene.bounds.y(),
                   scene.bounds.width(), scene.bounds.height());
    }
    if (viewBoxParts.size() == 4) {
      // mermaidClientBox: the content box united with the title text box
      // (baseline at ABSOLUTE -titleTopMargin, centered on the content
      // bbox), padded by the title band padding.
      const QRectF clientBox = editor::mermaidClientBox(entry);
      require(clientBox.isValid(),
              id + QStringLiteral(": state must expose a client box"));
      for (int component = 0; component < 4; ++component)
        require(std::abs(viewBoxParts.at(component).toDouble() -
                         (component == 0 ? clientBox.x() :
                          component == 1 ? clientBox.y() :
                          component == 2 ? clientBox.width() :
                          clientBox.height())) < 0.01,
                id + QStringLiteral(": client viewBox[%1] %2 vs browser %3")
                    .arg(component)
                    .arg(component == 0 ? clientBox.x() :
                         component == 1 ? clientBox.y() :
                         component == 2 ? clientBox.width() :
                         clientBox.height())
                    .arg(browserRoot.value(QStringLiteral("viewBox")).toString()));
      // End-to-end: the SERIALIZED SVG root must carry the same fractional
      // box (viewBox origin + size + max-width) — a content-only or
      // zero-origin viewBox would clip the title and shift every child.
      const editor::MermaidSvgRenderResult exported =
          editor::MermaidRenderCache::renderMermaidSourceToSvg(source, 0);
      require(!exported.svg.isEmpty(),
              id + QStringLiteral(": SVG export failed"));
      const QString svgText = QString::fromUtf8(exported.svg);
      const auto attributeValue = [&](const char* name) {
        const QString token = QLatin1String(name) + QLatin1String("=\"");
        const int start = svgText.indexOf(token);
        if (start < 0) return QString();
        const int valueStart = start + token.size();
        const int end = svgText.indexOf(QLatin1Char('"'), valueStart);
        return end < 0 ? QString() : svgText.mid(valueStart, end - valueStart);
      };
      const QStringList exportedViewBox = attributeValue("viewBox")
          .split(QLatin1Char(' '), Qt::SkipEmptyParts);
      require(exportedViewBox.size() == 4,
              id + QStringLiteral(": exported viewBox '%1' vs browser '%2'")
                  .arg(attributeValue("viewBox"),
                       browserRoot.value(QStringLiteral("viewBox")).toString()));
      for (int component = 0; component < 4; ++component)
        require(std::abs(exportedViewBox.at(component).toDouble() -
                         viewBoxParts.at(component).toDouble()) < 0.01,
                id + QStringLiteral(": exported viewBox[%1] %2 vs browser %3")
                    .arg(component).arg(exportedViewBox.at(component),
                                        viewBoxParts.at(component)));
      const QString style = attributeValue("style");
      const qreal maxWidth = style.startsWith(QStringLiteral("max-width: "))
          ? style.mid(11).chopped(3).toDouble() : -1.0;
      require(std::abs(maxWidth - viewBoxParts.at(2).toDouble()) < 0.01,
              id + QStringLiteral(": exported max-width '%1' vs %2")
                  .arg(style).arg(viewBoxParts.at(2)));
    }

    const QJsonArray markers = structure.value(QStringLiteral("markers")).toArray();
    require(markers.size() == 1, id + QStringLiteral(": marker definition count drifted"));
    const QJsonObject marker = markers.at(0).toObject();
    require(marker.value(QStringLiteral("markerWidth")).toString().toDouble() ==
                scene.arrowMarkerSize.width() &&
                marker.value(QStringLiteral("markerHeight")).toString().toDouble() ==
                scene.arrowMarkerSize.height() &&
                marker.value(QStringLiteral("refX")).toString().toDouble() ==
                scene.arrowMarkerRef.x() &&
                marker.value(QStringLiteral("refY")).toString().toDouble() ==
                scene.arrowMarkerRef.y() &&
                marker.value(QStringLiteral("orient")).toString() == scene.arrowMarkerOrient &&
                marker.value(QStringLiteral("childTag")).toString() == QLatin1String("path"),
            id + QStringLiteral(": arrow marker structure mismatch"));
    // The marker is the CONCAVE barb path (a solid triangle diverges from
    // the browser on every edge) — locked verbatim from the DOM, and the
    // native SVG projection must serialize the same geometry.
    require(marker.value(QStringLiteral("childPathD")).toString() ==
                QLatin1String("M 19,7 L9,13 L14,7 L9,1 Z"),
            id + QStringLiteral(": barb marker path drifted"));
    const SvgMarkerProjection projection = scene.svgMarkerProjection();
    require(projection.definitions.size() == 1 &&
                projection.definitions.first().children.size() == 1 &&
                projection.definitions.first().children.first().path ==
                    marker.value(QStringLiteral("childPathD")).toString(),
            id + QStringLiteral(": native SVG marker projection drifted"));

    // click contract: upstream wraps the linked node's <g> in an
    // <a xlink:href title> AFTER layout; the native scene exposes the same
    // link as an interaction region on the node's bounds.
    const QJsonArray anchors = structure.value(QStringLiteral("anchors")).toArray();
    for (const QJsonValue& anchorValue : anchors) {
      const QJsonObject anchor = anchorValue.toObject();
      const QString href = anchor.value(QStringLiteral("href")).toString();
      const auto& regions = scene.interactionRegions();
      const auto region = std::find_if(regions.cbegin(), regions.cend(),
          [&](const InteractionRegion& r) { return r.href == href; });
      require(region != regions.cend(),
              id + QStringLiteral(": click link missing from interaction regions: ") + href);
      require(region->toolTip == anchor.value(QStringLiteral("title")).toString(),
              id + QStringLiteral(": click tooltip mismatch"));
    }

    const QJsonArray browserNodes = structure.value(QStringLiteral("nodes")).toArray();
    require(browserNodes.size() == scene.nodes.size(),
            id + QStringLiteral(": node DOM count mismatch"));
    for (const QJsonValue& nodeValue : browserNodes) {
      const QJsonObject expected = nodeValue.toObject();
      const QString nodeId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneNode* actual = findById(scene.nodes, nodeId);
      require(actual, id + QStringLiteral(": missing native node ") + nodeId);
      const QStringList browserTags = strings(expected.value(QStringLiteral("childTags")).toArray());
      const QStringList requiredTags = expectedNodeTags(actual->shape);
      for (const QString& tag : requiredTags)
        require(browserTags.contains(tag),
                id + QLatin1Char('/') + nodeId + QStringLiteral(": missing browser shape tag ") + tag);
      const int browserForeignObjects =
          expected.value(QStringLiteral("foreignObjectCount")).toInt();
      const bool labelBearingShape =
          actual->shape != QLatin1String("stateStart") &&
          actual->shape != QLatin1String("stateEnd") &&
          actual->shape != QLatin1String("fork") &&
          actual->shape != QLatin1String("join") &&
          actual->shape != QLatin1String("choice");
      require((browserForeignObjects > 0) == labelBearingShape &&
                  (!labelBearingShape || !actual->labelDocument.text.isEmpty()),
              id + QLatin1Char('/') + nodeId + QStringLiteral(": label container mismatch"));
      foreignObjects += browserForeignObjects;
      ++nodes;
    }

    const QJsonArray browserClusters = structure.value(QStringLiteral("clusters")).toArray();
    require(browserClusters.size() == scene.clusters.size(),
            id + QStringLiteral(": cluster DOM count mismatch"));
    for (const QJsonValue& clusterValue : browserClusters) {
      const QJsonObject expected = clusterValue.toObject();
      const QString clusterId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneNode* actual = findById(scene.clusters, clusterId);
      require(actual && actual->group,
              id + QStringLiteral(": missing native cluster ") + clusterId);
      const QStringList tags = strings(expected.value(QStringLiteral("childTags")).toArray());
      require(tags.contains(actual->shape == QLatin1String("noteGroup")
                                ? QStringLiteral("rect") : QStringLiteral("g")),
              id + QLatin1Char('/') + clusterId + QStringLiteral(": cluster outline mismatch"));
      // Rect inventory: composites carry rect.outer + rect.inner; `--`
      // partitions carry a single rect.divider (grey alt fill, 10/10 dash,
      // square corners); note groups carry an invisible rect.
      const QJsonArray rects = expected.value(QStringLiteral("rects")).toArray();
      const bool divider = actual->shape == QLatin1String("divider");
      for (const QJsonValue& rectValue : rects) {
        const QJsonObject rect = rectValue.toObject();
        const QString rectClass = rect.value(QStringLiteral("class")).toString();
        const QJsonObject computed =
            rect.value(QStringLiteral("computed")).toObject();
        if (rectClass == QLatin1String("divider")) {
          require(divider,
                  id + QLatin1Char('/') + clusterId +
                      QStringLiteral(": divider rect on a non-divider cluster"));
          require(computed.value(QStringLiteral("fill")).toString() ==
                      QStringLiteral("rgb(240, 240, 240)") &&
                      computed.value(QStringLiteral("dasharray")).toString() ==
                          QStringLiteral("10px, 10px") &&
                      computed.value(QStringLiteral("rx")).toString() ==
                          QStringLiteral("auto"),
                  id + QLatin1Char('/') + clusterId +
                      QStringLiteral(": divider rect paint drifted"));
        } else if (rectClass == QLatin1String("inner")) {
          require(!divider && actual->innerBounds.isValid(),
                  id + QLatin1Char('/') + clusterId + QStringLiteral(": missing inner rect"));
        }
      }
      if (divider)
        require(rects.size() == 1,
                id + QLatin1Char('/') + clusterId +
                    QStringLiteral(": divider partition must be a single rect"));
      ++clusters;
    }

    const QJsonArray browserEdges = structure.value(QStringLiteral("edges")).toArray();
    require(browserEdges.size() == scene.edges.size(),
            id + QStringLiteral(": edge DOM count mismatch"));
    for (const QJsonValue& edgeValue : browserEdges) {
      const QJsonObject expected = edgeValue.toObject();
      const QString edgeId = expected.value(QStringLiteral("id")).toString();
      const state::StateSceneEdge* actual = findById(scene.edges, edgeId);
      require(actual && actual->points.size() >= 2,
              id + QStringLiteral(": missing native edge ") + edgeId);
      const bool browserArrow =
          expected.value(QStringLiteral("markerEnd")).toString() == QLatin1String("barbEnd");
      require(browserArrow == (actual->markerEnd == QLatin1String("arrow_barb")),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": marker-end mismatch"));
      const QString classes = expected.value(QStringLiteral("classes")).toString();
      require(classes.contains(QLatin1String("transition")) &&
                  (!classes.contains(QLatin1String("note-edge")) ||
                   actual->classes.contains(QLatin1String("note-edge"))),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": edge class mismatch"));
      // Paint contract: transitions are solid; the note connector is the
      // ONLY dashed edge (`.note-edge { stroke-dasharray: 5 }`).
      const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
      const bool noteEdge = classes.contains(QLatin1String("note-edge"));
      require((computed.value(QStringLiteral("dasharray")).toString() ==
               QLatin1String("5px")) == noteEdge,
              id + QLatin1Char('/') + edgeId + QStringLiteral(": dash contract drifted"));
      require((actual->strokeDasharray == QLatin1String("5,5")) == noteEdge,
              id + QLatin1Char('/') + edgeId +
                  QStringLiteral(": native note-edge dash drifted"));
      require(!expected.value(QStringLiteral("pathCommands")).toString().isEmpty() &&
                  (!actual->path.isEmpty() || actual->points.size() >= 2),
              id + QLatin1Char('/') + edgeId + QStringLiteral(": edge path structure missing"));
      ++edges;
    }
  }
  require(nodes >= 15 && clusters >= 4 && edges >= 9 && foreignObjects >= 10,
          QStringLiteral("State SVG structural coverage regressed"));

  // ---- themeCSS differential against the real state DOM ----
  // Structural selectors must resolve per element: :nth-of-type and sibling
  // combinators pick individual nodes, note/cluster/edge/note-edge/label-p
  // rules hit their own channels, display:none folds per element, and label
  // font feedback reaches the layout (client size).
  int themeCssCasesChecked = 0;
  for (const QJsonValue& themeValue : root.value(QStringLiteral("themeCss")).toArray()) {
    const QJsonObject themeCase = themeValue.toObject();
    const QString id = themeCase.value(QStringLiteral("id")).toString();
    // Rebuild the init directive the generator used: themeCSS plus the
    // case's look (handDrawn cases ride look + handDrawnSeed through the
    // directive, exactly like the browser render).
    QJsonObject init{{QStringLiteral("themeCSS"),
                      themeCase.value(QStringLiteral("themeCSS")).toString()}};
    if (themeCase.value(QStringLiteral("look")).toString() == QLatin1String("handDrawn")) {
      init.insert(QStringLiteral("look"), QStringLiteral("handDrawn"));
      init.insert(QStringLiteral("handDrawnSeed"), 42);
    }
    const QString source = QStringLiteral("%%{init: %1}%%\n%2")
        .arg(QString::fromUtf8(QJsonDocument(init).toJson(QJsonDocument::Compact)),
             themeCase.value(QStringLiteral("source")).toString());
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr,
            id + QStringLiteral(": themeCSS render failed: ") + entry.errorMessage);
    // A hidden transition (display:none / visibility:hidden) must not leave
    // dangling arrowheads: the SVG marker overlay only carries visible edges.
    const SvgMarkerProjection projection = stateScene->svgMarkerProjection();
    int visibleEdges = 0;
    for (const state::StateSceneEdge& edge : stateScene->edges)
      if (!edge.markerEnd.isEmpty() && edge.markerEnd != QLatin1String("none") &&
          edge.shapeCss.displayed && edge.shapeCss.painted)
        ++visibleEdges;
    require(projection.edges.size() == visibleEdges,
            id + QStringLiteral(": marker overlay edges %1 vs visible %2")
                .arg(projection.edges.size()).arg(visibleEdges));
    const double clientWidth = themeCase.value(QStringLiteral("client")).toObject()
                                   .value(QStringLiteral("width")).toDouble();
    const double clientHeight = themeCase.value(QStringLiteral("client")).toObject()
                                    .value(QStringLiteral("height")).toDouble();
    // handDrawn rough INK extents (canvas size) are a separate open
    // workstream — the computed-style channels below still lock.
    if (themeCase.value(QStringLiteral("look")).toString() != QLatin1String("handDrawn"))
      require(qRound(clientWidth) == entry.naturalSize.width() &&
                  qRound(clientHeight) == entry.naturalSize.height(),
              id + QStringLiteral(": client box %1x%2 vs native %3x%4")
                  .arg(clientWidth).arg(clientHeight)
                  .arg(entry.naturalSize.width()).arg(entry.naturalSize.height()));
    const QJsonArray themeNodes = themeCase.value(QStringLiteral("nodes")).toArray();
    require(themeNodes.size() == static_cast<int>(stateScene->nodes.size()),
            id + QStringLiteral(": node count mismatch"));
    // Computed opacity factors arrive as strings ("0.2"); absent = 1.
    const auto opacityFactor = [](const QJsonObject& source, const char* key) {
      const QString text = source.value(QLatin1String(key)).toString();
      return text.isEmpty() ? 1.0 : text.toDouble();
    };
    for (int index = 0; index < themeNodes.size(); ++index) {
      const QJsonObject expected = themeNodes.at(index).toObject();
      const state::StateSceneNode& node = stateScene->nodes.at(index);
      const QJsonObject shape = expected.value(QStringLiteral("shape")).toObject();
      if (!shape.isEmpty()) {
        // Opacity channel model: the element opacity is EFFECTIVE (ancestor
        // chain folded) while fill-/stroke-opacity keep the PURE declared
        // factors — the paint channel composes color alpha × opacity ×
        // channel exactly once each (an effective channel + an extra
        // opacity multiply would square `opacity: 0.2` into 0.04).
        require(std::abs(node.shapeCss.opacity -
                         opacityFactor(shape, "opacity")) < 0.01 &&
                    std::abs(node.shapeCss.fillOpacity -
                             opacityFactor(shape, "fillOpacity")) < 0.01 &&
                    std::abs(node.shapeCss.strokeOpacity -
                             opacityFactor(shape, "strokeOpacity")) < 0.01,
                id + QStringLiteral("/node%1/opacity: %2/%3/%4 vs browser %5/%6/%7")
                    .arg(index).arg(node.shapeCss.opacity)
                    .arg(node.shapeCss.fillOpacity)
                    .arg(node.shapeCss.strokeOpacity)
                    .arg(opacityFactor(shape, "opacity"))
                    .arg(opacityFactor(shape, "fillOpacity"))
                    .arg(opacityFactor(shape, "strokeOpacity")));
        // The browser's first rect/circle/path IS the shape element (rects
        // for rect/rectWithTitle, circle.state-start, the rough fill path
        // first for note/choice/fork/end) — the native shapeCss slot.
        if (!shape.value(QStringLiteral("display")).toString().isEmpty())
          require(node.shapeCss.displayed ==
                      (shape.value(QStringLiteral("display")).toString() !=
                       QLatin1String("none")),
                  id + QStringLiteral("/node%1/visible").arg(index));
        if (node.shapeCss.displayed) {
          require(colorEquals(node.shapeCss.fill.isEmpty() ? node.fill
                                                           : node.shapeCss.fill,
                              shape.value(QStringLiteral("fill")).toString()),
                  id + QStringLiteral("/node%1/fill: %2 (css %3) vs %4")
                      .arg(index).arg(node.fill, node.shapeCss.fill,
                                      shape.value(QStringLiteral("fill")).toString()));
          require(shape.value(QStringLiteral("stroke")).toString() ==
                      QLatin1String("none")
                      ? node.shapeCss.stroke.compare(QLatin1String("none"),
                                                     Qt::CaseInsensitive) == 0
                      : colorEquals(node.shapeCss.stroke.isEmpty() ? node.stroke
                                                                   : node.shapeCss.stroke,
                                    shape.value(QStringLiteral("stroke")).toString()),
                  id + QStringLiteral("/node%1/stroke: %2 vs %3")
                      .arg(index).arg(node.shapeCss.stroke,
                                      shape.value(QStringLiteral("stroke")).toString()));
        }
      }
      const QJsonObject span = expected.value(QStringLiteral("span")).toObject();
      if (!span.isEmpty()) {
        require(colorEquals(node.labelCss.color.isEmpty() ? node.textColor
                                                          : node.labelCss.color,
                            span.value(QStringLiteral("color")).toString()),
                id + QStringLiteral("/node%1/span-color").arg(index));
        const double fontSize = span.value(QStringLiteral("fontSize")).toString()
                                    .chopped(2).toDouble();
        require(std::abs(node.labelCss.fontSize - fontSize) < 0.01,
                id + QStringLiteral("/node%1/span-font-size").arg(index));
        // visibility:hidden hides ONLY the label text — the frame and the
        // layout box stay (a display:none would have collapsed above).
        if (!span.value(QStringLiteral("visibility")).toString().isEmpty())
          require(node.labelCss.painted ==
                      (span.value(QStringLiteral("visibility")).toString() ==
                       QLatin1String("visible")),
                  id + QStringLiteral("/node%1/span-visibility").arg(index));
      }
      // The rough pair's SECOND path (the outline element): its own
      // stroke channel and display/visibility gates resolve independently
      // of the fill path (`.rough-node path:nth-of-type(2)`).
      const QJsonObject strokeShape = expected.value(QStringLiteral("strokeShape")).toObject();
      if (!strokeShape.isEmpty()) {
        require(std::abs(node.shapeStrokeCss.opacity -
                         opacityFactor(strokeShape, "opacity")) < 0.01 &&
                    std::abs(node.shapeStrokeCss.strokeOpacity -
                             opacityFactor(strokeShape, "strokeOpacity")) < 0.01,
                id + QStringLiteral("/node%1/stroke-shape-opacity: %2/%3 vs %4/%5")
                    .arg(index).arg(node.shapeStrokeCss.opacity)
                    .arg(node.shapeStrokeCss.strokeOpacity)
                    .arg(opacityFactor(strokeShape, "opacity"))
                    .arg(opacityFactor(strokeShape, "strokeOpacity")));
        const QString browserStroke = strokeShape.value(QStringLiteral("stroke")).toString();
        if (browserStroke == QLatin1String("none")) {
          require(node.shapeStrokeCss.stroke.compare(QLatin1String("none"),
                                                     Qt::CaseInsensitive) == 0,
                  id + QStringLiteral("/node%1/stroke-shape-none: %2")
                      .arg(index).arg(node.shapeStrokeCss.stroke));
        } else {
          require(colorEquals(node.shapeStrokeCss.stroke.isEmpty() ? node.stroke
                                                                   : node.shapeStrokeCss.stroke,
                              browserStroke),
                  id + QStringLiteral("/node%1/stroke-shape: %2 vs %3")
                      .arg(index).arg(node.shapeStrokeCss.stroke, browserStroke));
        }
        if (!strokeShape.value(QStringLiteral("display")).toString().isEmpty())
          require(node.shapeStrokeCss.displayed ==
                      (strokeShape.value(QStringLiteral("display")).toString() !=
                       QLatin1String("none")),
                  id + QStringLiteral("/node%1/stroke-shape-visible").arg(index));
        if (!strokeShape.value(QStringLiteral("visibility")).toString().isEmpty())
          require(node.shapeStrokeCss.painted ==
                      (strokeShape.value(QStringLiteral("visibility")).toString() ==
                       QLatin1String("visible")),
                  id + QStringLiteral("/node%1/stroke-shape-painted").arg(index));
      }
      // The label's <p> is the TEXT wrapper: its computed font/color drive
      // measurement and paint (folding the span chain), and display:none
      // collapses the label BOX (the node shrinks to its padding-only rect).
      const QJsonObject labelP = expected.value(QStringLiteral("p")).toObject();
      if (!labelP.isEmpty()) {
        const double pFontSize = labelP.value(QStringLiteral("fontSize"))
                                     .toString().chopped(2).toDouble();
        require(std::abs(node.labelTextCss.fontSize - pFontSize) < 0.01,
                id + QStringLiteral("/node%1/p-font-size: %2 vs %3")
                    .arg(index).arg(node.labelTextCss.fontSize).arg(pFontSize));
        require(colorEquals(node.labelTextCss.color, labelP.value(
                    QStringLiteral("color")).toString()),
                id + QStringLiteral("/node%1/p-color: %2 vs %3")
                    .arg(index).arg(node.labelTextCss.color,
                                    labelP.value(QStringLiteral("color")).toString()));
        if (!labelP.value(QStringLiteral("display")).toString().isEmpty())
          require(node.labelTextCss.displayed ==
                      (labelP.value(QStringLiteral("display")).toString() !=
                       QLatin1String("none")),
                  id + QStringLiteral("/node%1/p-display").arg(index));
      }
      // The rectWithTitle description rows' own <p>: font/color are the
      // rows' used style (the titled box measures the rows with it), and
      // display:none collapses the rows out of the dagre box.
      const QJsonObject descP = expected.value(QStringLiteral("descP")).toObject();
      if (!descP.isEmpty()) {
        const double rowFontSize = descP.value(QStringLiteral("fontSize"))
                                       .toString().chopped(2).toDouble();
        require(std::abs(node.descriptionTextCss.fontSize - rowFontSize) < 0.01,
                id + QStringLiteral("/node%1/desc-p-font-size: %2 vs %3")
                    .arg(index).arg(node.descriptionTextCss.fontSize)
                    .arg(rowFontSize));
        require(colorEquals(node.descriptionTextCss.color,
                            descP.value(QStringLiteral("color")).toString()),
                id + QStringLiteral("/node%1/desc-p-color: %2 vs %3")
                    .arg(index).arg(node.descriptionTextCss.color,
                                    descP.value(QStringLiteral("color")).toString()));
        if (!descP.value(QStringLiteral("display")).toString().isEmpty())
          require(node.descriptionTextCss.displayed ==
                      (descP.value(QStringLiteral("display")).toString() !=
                       QLatin1String("none")),
                  id + QStringLiteral("/node%1/desc-p-display").arg(index));
      }
      // The rectWithTitle description foreignObject (second fo inside
      // g.label): visibility/display gate the description rows' paint
      // without touching the title.
      const QJsonObject desc = expected.value(QStringLiteral("desc")).toObject();
      if (!desc.isEmpty()) {
        const QString visibility = desc.value(QStringLiteral("visibility")).toString();
        require(node.descriptionCss.painted ==
                    (visibility != QLatin1String("hidden") &&
                     visibility != QLatin1String("collapse")),
                id + QStringLiteral("/node%1/desc-visibility").arg(index));
      }
    }
    for (const QJsonValue& clusterValue :
         themeCase.value(QStringLiteral("clusters")).toArray()) {
      const QJsonObject cluster = clusterValue.toObject();
      const QJsonObject inner = cluster.value(QStringLiteral("inner")).toObject();
      if (inner.isEmpty()) continue;
      const QString classes = cluster.value(QStringLiteral("classes")).toString();
      const auto native = std::find_if(
          stateScene->clusters.cbegin(), stateScene->clusters.cend(),
          [&](const state::StateSceneNode& c) {
            return c.innerBounds.isValid() && classes.contains(
                QStringLiteral("statediagram-cluster")) && !classes.contains(
                QStringLiteral("note-cluster"));
          });
      require(native != stateScene->clusters.cend(),
              id + QStringLiteral(": composite cluster missing"));
      const QString nativeInnerFill = native->innerCss.fill.isEmpty()
          ? native->innerFill : native->innerCss.fill;
      require(colorEquals(nativeInnerFill, inner.value(QStringLiteral("fill")).toString()),
              id + QStringLiteral("/cluster-inner/fill: %1 vs %2")
                  .arg(nativeInnerFill, inner.value(QStringLiteral("fill")).toString()));
      // rect.outer carries its own opacity channels (element opacity and a
      // pure fill-opacity factor): the frame composes alpha × opacity ×
      // fill-opacity once each.
      const QJsonObject outer = cluster.value(QStringLiteral("outer")).toObject();
      if (!outer.isEmpty()) {
        require(std::abs(native->shapeCss.opacity -
                         opacityFactor(outer, "opacity")) < 0.01 &&
                    std::abs(native->shapeCss.fillOpacity -
                             opacityFactor(outer, "fillOpacity")) < 0.01,
                id + QStringLiteral("/cluster-outer/opacity: %1/%2 vs %3/%4")
                    .arg(native->shapeCss.opacity)
                    .arg(native->shapeCss.fillOpacity)
                    .arg(opacityFactor(outer, "opacity"))
                    .arg(opacityFactor(outer, "fillOpacity")));
        require(colorEquals(native->shapeCss.fill.isEmpty()
                                ? native->fill : native->shapeCss.fill,
                            outer.value(QStringLiteral("fill")).toString()),
                id + QStringLiteral("/cluster-outer/fill: %1 vs %2")
                    .arg(native->shapeCss.fill,
                         outer.value(QStringLiteral("fill")).toString()));
      }
      // Cluster-title span: the css font drives BOTH the painted title and
      // the composite box measurement (client/viewBox growth).
      const QJsonObject clusterLabel = cluster.value(QStringLiteral("label")).toObject();
      if (!clusterLabel.isEmpty()) {
        const double fontSize = clusterLabel.value(QStringLiteral("fontSize"))
                                    .toString().chopped(2).toDouble();
        require(std::abs(native->labelCss.fontSize - fontSize) < 0.01,
                id + QStringLiteral("/cluster-label/font-size: %1 vs %2")
                    .arg(native->labelCss.fontSize).arg(fontSize));
      }
      // The cluster title's <p>: its own computed font is the text's used
      // style — the painted title and the title-band measurement read it.
      const QJsonObject clusterLabelP = cluster.value(QStringLiteral("labelP")).toObject();
      if (!clusterLabelP.isEmpty()) {
        const double fontSize = clusterLabelP.value(QStringLiteral("fontSize"))
                                     .toString().chopped(2).toDouble();
        require(std::abs(native->labelTextCss.fontSize - fontSize) < 0.01,
                id + QStringLiteral("/cluster-label/p-font-size: %1 vs %2")
                    .arg(native->labelTextCss.fontSize).arg(fontSize));
      }
    }
    const QJsonArray themeEdges = themeCase.value(QStringLiteral("edges")).toArray();
    require(themeEdges.size() == static_cast<int>(stateScene->edges.size()),
            id + QStringLiteral(": edge count mismatch"));
    for (int index = 0; index < themeEdges.size(); ++index) {
      const QJsonObject expected = themeEdges.at(index).toObject();
      const state::StateSceneEdge& edge = stateScene->edges.at(index);
      const QString browserStroke = expected.value(QStringLiteral("stroke")).toString();
      if (browserStroke == QLatin1String("none")) {
        // `stroke: none` removes the line — the resolved slot must carry
        // the none keyword (a color parse would draw a black hairline).
        require(edge.stroke.compare(QLatin1String("none"),
                                    Qt::CaseInsensitive) == 0 &&
                    edge.shapeCss.stroke.compare(QLatin1String("none"),
                                                 Qt::CaseInsensitive) == 0,
                id + QStringLiteral("/edge%1/stroke-none: %2/%3")
                    .arg(index).arg(edge.stroke, edge.shapeCss.stroke));
      } else {
        require(colorEquals(edge.stroke, browserStroke),
                id + QStringLiteral("/edge%1/stroke: %2 vs %3")
                    .arg(index).arg(edge.stroke, browserStroke));
      }
      if (!expected.value(QStringLiteral("display")).toString().isEmpty())
        require(edge.shapeCss.displayed ==
                    (expected.value(QStringLiteral("display")).toString() !=
                     QLatin1String("none")),
                id + QStringLiteral("/edge%1/visible").arg(index));
      // The transition's stroke channel factor stays RAW in the model — the
      // painter multiplies the effective element opacity in exactly once.
      require(std::abs(edge.shapeCss.strokeOpacity -
                       opacityFactor(expected, "strokeOpacity")) < 0.01,
              id + QStringLiteral("/edge%1/stroke-opacity: %2 vs %3")
                  .arg(index).arg(edge.shapeCss.strokeOpacity)
                  .arg(opacityFactor(expected, "strokeOpacity")));
    }
    const QJsonArray labelP = themeCase.value(QStringLiteral("edgeLabelP")).toArray();
    QVector<const state::StateSceneEdge*> labeledEdges;
    for (const state::StateSceneEdge& edge : stateScene->edges)
      if (!edge.label.isEmpty()) labeledEdges.append(&edge);
    require(labelP.size() == labeledEdges.size(),
            id + QStringLiteral(": labeled edge count mismatch"));
    for (int index = 0; index < labelP.size(); ++index) {
      const QJsonObject p = labelP.at(index).toObject();
      require(colorEquals(labeledEdges.at(index)->labelBackground,
                          p.value(QStringLiteral("background")).toString()),
              id + QStringLiteral("/edge-label/background: %1 vs %2")
                  .arg(labeledEdges.at(index)->labelBackground,
                       p.value(QStringLiteral("background")).toString()));
      // The p's OWN channels ride on labelBackgroundCss: opacity composes
      // onto background AND text (the text lives inside the p), and
      // display/visibility hide the whole label.
      require(std::abs(labeledEdges.at(index)->labelBackgroundCss.opacity -
                       opacityFactor(p, "opacity")) < 0.01,
              id + QStringLiteral("/edge-label/p-opacity: %1 vs %2")
                  .arg(labeledEdges.at(index)->labelBackgroundCss.opacity)
                  .arg(opacityFactor(p, "opacity")));
      if (!p.value(QStringLiteral("display")).toString().isEmpty())
        require(labeledEdges.at(index)->labelBackgroundCss.displayed ==
                    (p.value(QStringLiteral("display")).toString() !=
                     QLatin1String("none")),
                id + QStringLiteral("/edge-label/p-display"));
      if (!p.value(QStringLiteral("visibility")).toString().isEmpty())
        require(labeledEdges.at(index)->labelBackgroundCss.painted ==
                    (p.value(QStringLiteral("visibility")).toString() ==
                     QLatin1String("visible")),
                id + QStringLiteral("/edge-label/p-visibility"));
      // The p's font pair is the TEXT's used style: it sizes the label chip
      // (measurement feedback — a 31px p grows the viewBox) and the painted
      // glyphs; its color is the text color.
      const double pFontSize = p.value(QStringLiteral("fontSize"))
                                   .toString().chopped(2).toDouble();
      require(std::abs(labeledEdges.at(index)->labelBackgroundCss.fontSize -
                       pFontSize) < 0.01,
              id + QStringLiteral("/edge-label/p-font-size: %1 vs %2")
                  .arg(labeledEdges.at(index)->labelBackgroundCss.fontSize)
                  .arg(pFontSize));
      require(colorEquals(labeledEdges.at(index)->labelBackgroundCss.color,
                          p.value(QStringLiteral("color")).toString()),
              id + QStringLiteral("/edge-label/p-color: %1 vs %2")
                  .arg(labeledEdges.at(index)->labelBackgroundCss.color,
                       p.value(QStringLiteral("color")).toString()));
    }
    // The marker defs path is the raster arrowhead channel — themeCSS
    // recolors it globally (per-edge transition rules must not) and its OWN
    // element channels (opacity / stroke-width / display / per-channel
    // opacity) restyle the arrowhead itself.
    const QJsonArray markers = themeCase.value(QStringLiteral("markers")).toArray();
    require(!markers.isEmpty(),
            id + QStringLiteral(": marker path capture missing"));
    for (const QJsonValue& markerValue : markers) {
      const QJsonObject marker = markerValue.toObject();
      const QString fill = stateScene->markerCss.fill.isEmpty()
          ? stateScene->style.transitionColor : stateScene->markerCss.fill;
      const QString stroke = stateScene->markerCss.stroke.isEmpty()
          ? stateScene->style.transitionColor : stateScene->markerCss.stroke;
      require(colorEquals(fill, marker.value(QStringLiteral("fill")).toString()),
              id + QStringLiteral("/marker/fill: %1 vs %2")
                  .arg(fill, marker.value(QStringLiteral("fill")).toString()));
      require(colorEquals(stroke, marker.value(QStringLiteral("stroke")).toString()),
              id + QStringLiteral("/marker/stroke: %1 vs %2")
                  .arg(stroke, marker.value(QStringLiteral("stroke")).toString()));
      const QString markerOpacityText =
          marker.value(QStringLiteral("opacity")).toString();
      const double pathOpacity = markerOpacityText.isEmpty()
          ? 1.0 : markerOpacityText.toDouble();
      // The raster arrowhead's used opacity is the MARKER element's own
      // opacity times the path's (`defs [id$="-barbEnd"] { opacity: … }`
      // matches the id-carrying marker; opacity is not inherited, but the
      // referenced marker's rendering composites at the marker's opacity).
      const QString markerElementOpacityText =
          marker.value(QStringLiteral("markerOpacity")).toString();
      const double markerOpacity = markerElementOpacityText.isEmpty()
          ? 1.0 : markerElementOpacityText.toDouble();
      const double usedOpacity = pathOpacity * markerOpacity;
      require(std::abs(stateScene->markerCss.opacity - usedOpacity) < 0.01,
              id + QStringLiteral("/marker/opacity: %1 vs %2")
                  .arg(stateScene->markerCss.opacity).arg(usedOpacity));
      // fill-opacity INHERITS (opacity does not): the path's computed
      // channel factor already carries a marker-element declaration, and
      // the model stores it RAW — the used channel alpha is
      // color × markerOpacity-composite × this factor, composed once.
      const QString pathFillOpacityText =
          marker.value(QStringLiteral("fillOpacity")).toString();
      const double pathFillOpacity =
          pathFillOpacityText.isEmpty() ? 1.0 : pathFillOpacityText.toDouble();
      require(std::abs(stateScene->markerCss.fillOpacity - pathFillOpacity) < 0.01,
              id + QStringLiteral("/marker/fill-opacity: %1 vs %2")
                  .arg(stateScene->markerCss.fillOpacity).arg(pathFillOpacity));
      const QString pathStrokeOpacityText =
          marker.value(QStringLiteral("strokeOpacity")).toString();
      const double pathStrokeOpacity =
          pathStrokeOpacityText.isEmpty() ? 1.0 : pathStrokeOpacityText.toDouble();
      require(std::abs(stateScene->markerCss.strokeOpacity - pathStrokeOpacity) < 0.01,
              id + QStringLiteral("/marker/stroke-opacity: %1 vs %2")
                  .arg(stateScene->markerCss.strokeOpacity).arg(pathStrokeOpacity));
      const double markerWidth = marker.value(QStringLiteral("strokeWidth"))
                                     .toString().chopped(2).toDouble();
      require(std::abs(stateScene->markerCss.strokeWidthPx - markerWidth) < 0.01,
              id + QStringLiteral("/marker/stroke-width: %1 vs %2")
                  .arg(stateScene->markerCss.strokeWidthPx).arg(markerWidth));
      const QString markerDisplay =
          marker.value(QStringLiteral("display")).toString();
      require(markerDisplay != QLatin1String("none")
                  ? stateScene->markerCss.displayed
                  : !stateScene->markerCss.displayed,
              id + QStringLiteral("/marker/display: %1").arg(markerDisplay));
    }
    ++themeCssCasesChecked;
  }
  require(themeCssCasesChecked == 15,
          QStringLiteral("State themeCSS differential coverage regressed"));
  qDebug() << "MermaidStateSvgStructuralTest:" << nodes << "nodes," << clusters
           << "clusters," << edges << "edges passed;" << themeCssCasesChecked
           << "themeCSS cases";
  return 0;
}
