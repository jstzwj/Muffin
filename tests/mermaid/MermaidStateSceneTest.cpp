#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/state/StateScenePainter.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughPaint.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPainterPath>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace muffin::mermaid::state;
namespace flowchart = muffin::mermaid::flowchart;

namespace {
[[noreturn]] void fail(const QString& message) {
  // qFatal() -> abort() does not flush buffered stderr on Windows, which made a
  // failing assertion surface only as an unprintable 0xc0000409. Flush first.
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  qFatal("%s", qPrintable(message));
}
bool finiteRect(const QRectF& rect) {
  return std::isfinite(rect.x()) && std::isfinite(rect.y()) &&
      std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
      rect.width() >= 0.0 && rect.height() >= 0.0;
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("fixture path required"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonArray cases = QJsonDocument::fromJson(file.readAll())
      .object().value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const StateDiagram diagram = StateDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    const StateLayoutInput input = buildStateLayoutInput(diagram.data());
    const StateLayoutMeasurements measurements = measureStateLayoutInput(input);
    const StatePlacementResult placement = layoutStateDiagramDagre(input, measurements);
    const StateScene scene = buildStateScene(input, placement);

    const int semanticNodes = static_cast<int>(std::count_if(
        input.nodes.cbegin(), input.nodes.cend(),
        [](const StateLayoutNodeInput& node) { return !node.isGroup; }));
    if (scene.nodes.size() != semanticNodes)
      fail(QStringLiteral("%1: scene node count %2 != input %3")
          .arg(id).arg(scene.nodes.size()).arg(semanticNodes));
    if (scene.edges.size() != input.edges.size())
      fail(QStringLiteral("%1: scene edge count mismatch").arg(id));
    QSet<QString> nodeIds;
    for (const StateSceneNode& node : scene.nodes) {
      if (node.id.isEmpty() || nodeIds.contains(node.id) || !finiteRect(node.bounds) ||
          node.bounds.isEmpty())
        fail(QStringLiteral("%1: invalid or duplicate node '%2'").arg(id, node.id));
      nodeIds.insert(node.id);
    }
    for (const StateSceneNode& cluster : scene.clusters) {
      if (cluster.id.isEmpty() || nodeIds.contains(cluster.id) ||
          !finiteRect(cluster.bounds) || cluster.bounds.isEmpty())
        fail(QStringLiteral("%1: invalid or duplicate cluster '%2'").arg(id, cluster.id));
      nodeIds.insert(cluster.id);
    }
    QSet<QString> edgeIds;
    for (const StateSceneEdge& edge : scene.edges) {
      if (edge.id.isEmpty() || edgeIds.contains(edge.id) || edge.points.size() < 2 ||
          !nodeIds.contains(edge.start) || !nodeIds.contains(edge.end))
        fail(QStringLiteral("%1: invalid edge '%2'").arg(id, edge.id));
      edgeIds.insert(edge.id);
    }
    if ((!scene.nodes.isEmpty() || !scene.clusters.isEmpty()) &&
        (!finiteRect(scene.bounds) || scene.bounds.isEmpty()))
      fail(QStringLiteral("%1: invalid scene bounds").arg(id));
  }

  // Per-node style/classDef cascade (chunk-YI7H2ERT compileStyles): `classDef`
  // and inline `style` must override the theme defaults on the resolved node.
  try {
    const StateDiagram diagram = StateDiagram::parse(QStringLiteral(
        "stateDiagram-v2\n"
        "classDef hot fill:#ff0000,stroke:#00ff00,color:#0000ff\n"
        "s1 --> s2\n"
        "class s1 hot\n"
        "style s2 fill:#ffff00,stroke:#ff00ff\n"));
    const StateLayoutInput input = buildStateLayoutInput(diagram.data());
    const StateLayoutMeasurements measurements = measureStateLayoutInput(input);
    const StatePlacementResult placement = layoutStateDiagramDagre(input, measurements);
    const StateScene scene = buildStateScene(input, placement);
    for (const StateSceneNode& node : scene.nodes) {
      if (node.id == QLatin1String("s1")) {
        if (node.fill != QLatin1String("#ff0000"))
          fail(QStringLiteral("s1 classDef fill %1 != #ff0000").arg(node.fill));
        if (node.stroke != QLatin1String("#00ff00"))
          fail(QStringLiteral("s1 classDef stroke %1 != #00ff00").arg(node.stroke));
        if (node.textColor != QLatin1String("#0000ff"))
          fail(QStringLiteral("s1 classDef color %1 != #0000ff").arg(node.textColor));
      } else if (node.id == QLatin1String("s2")) {
        if (node.fill != QLatin1String("#ffff00"))
          fail(QStringLiteral("s2 inline fill %1 != #ffff00").arg(node.fill));
        if (node.stroke != QLatin1String("#ff00ff"))
          fail(QStringLiteral("s2 inline stroke %1 != #ff00ff").arg(node.stroke));
      }
    }
  } catch (const StateParseError& error) {
    fail(QStringLiteral("style-cascade parse failed: %1").arg(error.what()));
  } catch (const std::exception& error) {
    fail(QStringLiteral("style-cascade threw: %1").arg(error.what()));
  }

  // `.edgeLabel p { background-color: edgeLabelBackground }`: html edge
  // labels paint their background on the <p> — the variable's OWN alpha is
  // the whole story (default rgba(232,232,232,0.8)). The SVG-label `rect`
  // rule and its 0.5 opacity never apply (state labels are foreignObjects;
  // browser-verified: default rgba(232,232,232,0.8), dark #585858).
  {
    const auto labelBackgroundAlpha = [](const QString& background) {
      StateScene scene;
      scene.style.edgeLabelBackground = background;
      scene.style.transitionLabelColor = QStringLiteral("transparent");
      StateSceneEdge edge;
      edge.id = QStringLiteral("e1");
      edge.start = edge.end = QStringLiteral("s");
      edge.label = QStringLiteral(" ");
      edge.labelPosition = QPointF(60.0, 60.0);
      edge.labelSize = QSizeF(80.0, 24.0);
      scene.edges.append(std::move(edge));
      scene.nodes.append(StateSceneNode{});
      scene.nodes.last().id = QStringLiteral("s");
      scene.nodes.last().bounds = QRectF(0.0, 0.0, 10.0, 10.0);
      scene.bounds = QRectF(0.0, 0.0, 120.0, 120.0);
      const QImage image = renderStateSceneToImage(scene);
      const int alpha = image.pixelColor(30, 59).alpha();
      return alpha;
    };
    const int themeAlpha = labelBackgroundAlpha(
        QStringLiteral("rgba(232, 232, 232, 0.8)"));
    if (std::abs(themeAlpha - 204) > 3)
      fail(QStringLiteral("default edge-label background alpha %1 != ~204 "
                          "(rgba 0.8, unmultiplied)")
               .arg(themeAlpha));
    const int opaqueAlpha = labelBackgroundAlpha(QStringLiteral("#ECECFF"));
    if (std::abs(opaqueAlpha - 255) > 3)
      fail(QStringLiteral("opaque edge-label background alpha %1 != ~255")
               .arg(opaqueAlpha));
  }

  // FINAL pixel alpha for the CSS opacity channels — not just the model's
  // intermediate values: the fill channel's used alpha is
  // color.alpha × element opacity × fill-opacity, each factor applied
  // EXACTLY ONCE. A model that stores the engine's effective channel
  // (already opacity-folded) and multiplies the element opacity again
  // squares 0.2 into 0.04 — ~5/255 instead of the browser's ~51. Sampled
  // at the rect's center (fully interior, no antialiasing).
  {
    const auto nodeFillAlpha = [](qreal opacity, qreal fillOpacity) {
      StateScene scene;
      StateSceneNode node;
      node.id = QStringLiteral("n1");
      node.shape = QStringLiteral("rect");
      node.bounds = QRectF(20.0, 20.0, 80.0, 60.0);
      node.fill = QStringLiteral("#ffffff");  // opaque color: alpha is
                                              // purely the CSS factors
      node.stroke = QStringLiteral("none");
      node.shapeCss.opacity = opacity;
      node.shapeCss.fillOpacity = fillOpacity;
      scene.nodes.append(node);
      scene.bounds = QRectF(0.0, 0.0, 120.0, 120.0);
      const QImage image = renderStateSceneToImage(scene);
      return image.pixelColor(60, 50).alpha();
    };
    const auto expectAlpha = [&](qreal opacity, qreal fillOpacity, int target) {
      const int alpha = nodeFillAlpha(opacity, fillOpacity);
      if (std::abs(alpha - target) > 3)
        fail(QStringLiteral("fill alpha (opacity %1, fill-opacity %2) %3 != ~%4")
                 .arg(opacity).arg(fillOpacity).arg(alpha).arg(target));
    };
    expectAlpha(1.0, 1.0, 255);   // opaque baseline
    expectAlpha(0.2, 1.0, 51);    // element opacity alone: 255 × 0.2
    expectAlpha(1.0, 0.5, 128);   // channel factor alone: 255 × 0.5
    expectAlpha(0.2, 0.5, 26);    // both, composed once each: 255 × 0.1
  }

  // handDrawn edges consume the dash pattern: the note connector's 5,5
  // (`.note-edge { stroke-dasharray: 5 }`) must dash the ROUGH line exactly
  // like the smooth path — a rough branch that rebuilds a plain QPen paints
  // it solid. The 40,40 pattern makes the gaps unmistakable under the rough
  // wobble: scan the interior columns of the line's band; a dashed line
  // leaves long ink-free column runs, a solid one inks essentially all.
  {
    StateScene scene;
    scene.handDrawn = true;
    scene.handDrawnSeed = 42;
    scene.style.transitionColor = QStringLiteral("#000000");
    scene.style.strokeWidth = 2.0;
    StateSceneEdge edge;
    edge.id = QStringLiteral("e1");
    edge.start = edge.end = QStringLiteral("s");
    edge.points = {QPointF(10.0, 60.0), QPointF(190.0, 60.0)};
    edge.strokeDasharray = QStringLiteral("40,40");
    QPainterPath straight;
    straight.moveTo(10.0, 60.0);
    straight.lineTo(190.0, 60.0);
    edge.roughDrawable = muffin::mermaid::rough::roughEdgeDrawable(straight, 42);
    edge.pathBounds = QRectF(10.0, 50.0, 180.0, 20.0);
    scene.edges.append(std::move(edge));
    scene.bounds = QRectF(0.0, 0.0, 200.0, 120.0);
    const QImage image = renderStateSceneToImage(scene);
    const auto columnInked = [&image](int x) {
      for (int y = 40; y < 80; ++y)
        if (image.pixelColor(x, y).alpha() > 0) return true;
      return false;
    };
    int inked = 0;
    const int total = 160;
    for (int x = 20; x < 20 + total; ++x)
      if (columnInked(x)) ++inked;
    // 40,40 over 180px = ink / gap / ink / gap: at most ~90 columns ink.
    if (inked > total - 20)
      fail(QStringLiteral("handDrawn edge dash not consumed: %1 of %2 "
                          "interior columns inked (solid line)")
               .arg(inked).arg(total));
  }

  // Cluster elements hide PER ELEMENT: rect.outer, the cluster-label span,
  // and rect.inner are SIBLING elements, so `rect.outer { display:none }`
  // removes only the frame — the inner body (and title) keep painting. A
  // whole-cluster gate at the painter entry blanks everything.
  {
    StateScene scene;
    StateSceneNode cluster;
    cluster.id = QStringLiteral("Running");
    cluster.group = true;
    cluster.bounds = QRectF(20.0, 20.0, 100.0, 80.0);
    cluster.innerBounds = QRectF(24.0, 40.0, 92.0, 56.0);
    cluster.innerFill = QStringLiteral("#00ff00");
    cluster.fill = QStringLiteral("#ff0000");
    cluster.label = QStringLiteral("Running");
    cluster.titleHeight = 16.0;
    cluster.shapeCss.displayed = false;  // rect.outer display:none
    scene.clusters.append(cluster);
    scene.bounds = QRectF(0.0, 0.0, 140.0, 120.0);
    const QImage image = renderStateSceneToImage(scene);
    // Inside the inner body: opaque green ink must survive.
    const QColor inner = image.pixelColor(70, 90);
    if (inner.alpha() != 255 || inner.green() < 200 || inner.red() > 10)
      fail(QStringLiteral("hidden outer frame must not blank the inner "
                          "body: pixel (%1)")
               .arg(inner.name(QColor::HexArgb)));
    // The outer frame's edge band (between outer and inner rects, e.g.
    // x=22 which is inside bounds but left of innerBounds) stays blank.
    const QColor frameBand = image.pixelColor(22, 60);
    if (frameBand.alpha() != 0)
      fail(QStringLiteral("display:none outer frame must not paint: pixel (%1)")
               .arg(frameBand.name(QColor::HexArgb)));
  }

  // rect.inner carries its OWN channels: a declared fill-opacity dims the
  // body without touching the outer frame, and display:none on the inner
  // blanks the body ENTIRELY (the brush must gate too, not just the pen).
  {
    const auto innerPixel = [](qreal opacity, bool displayed) {
      StateScene scene;
      StateSceneNode cluster;
      cluster.id = QStringLiteral("Running");
      cluster.group = true;
      cluster.bounds = QRectF(20.0, 20.0, 100.0, 80.0);
      cluster.innerBounds = QRectF(24.0, 40.0, 92.0, 56.0);
      cluster.innerFill = QStringLiteral("#ffffff");
      cluster.innerCss.fillOpacity = opacity;
      cluster.innerCss.displayed = displayed;
      cluster.fill = QStringLiteral("none");
      cluster.label = QStringLiteral("Running");
      cluster.titleHeight = 16.0;
      scene.clusters.append(cluster);
      scene.bounds = QRectF(0.0, 0.0, 140.0, 120.0);
      return renderStateSceneToImage(scene).pixelColor(78, 98);
    };
    const QColor full = innerPixel(1.0, true);
    if (full.alpha() != 255)
      fail(QStringLiteral("inner body must paint: (%1)")
               .arg(full.name(QColor::HexArgb)));
    const QColor dimmed = innerPixel(0.5, true);
    if (std::abs(dimmed.alpha() - 128) > 3)
      fail(QStringLiteral("inner fill-opacity must compose once: %1 != ~128")
               .arg(dimmed.name(QColor::HexArgb)));
    const QColor hidden = innerPixel(1.0, false);
    if (hidden.alpha() != 0)
      fail(QStringLiteral("inner display:none must blank the body: (%1)")
               .arg(hidden.name(QColor::HexArgb)));
  }

  // A DECLARED outer stroke-width of 0 disables the frame — it must not fall
  // back to the theme width.
  {
    StateScene scene;
    StateSceneNode cluster;
    cluster.id = QStringLiteral("C");
    cluster.group = true;
    cluster.bounds = QRectF(20.0, 20.0, 100.0, 80.0);
    cluster.innerBounds = QRectF(24.0, 40.0, 92.0, 56.0);
    cluster.innerFill = QStringLiteral("#00ff00");
    cluster.fill = QStringLiteral("none");
    cluster.stroke = QStringLiteral("#ff0000");
    cluster.label = QStringLiteral("C");
    cluster.titleHeight = 16.0;
    cluster.shapeCss.strokeWidthSet = true;
    cluster.shapeCss.strokeWidthPx = 0.0;
    scene.clusters.append(cluster);
    scene.bounds = QRectF(0.0, 0.0, 140.0, 120.0);
    const QImage image = renderStateSceneToImage(scene);
    const QColor frameBand = image.pixelColor(29, 68);
    if (frameBand.alpha() != 0)
      fail(QStringLiteral("declared stroke-width:0 must remove the frame: (%1)")
               .arg(frameBand.name(QColor::HexArgb)));
  }

  // The edge-label <p> channel: its opacity composes onto the background
  // (color alpha × p effective) AND the text, and display:none hides the
  // whole label — the p sits INSIDE the span, so the span's slots alone
  // cannot express either.
  {
    const auto labelAlpha = [](qreal pOpacity, bool pDisplayed) {
      StateScene scene;
      scene.style.edgeLabelBackground = QStringLiteral("#ffffff");
      scene.style.transitionLabelColor = QStringLiteral("#000000");
      StateSceneEdge edge;
      edge.id = QStringLiteral("e1");
      edge.start = edge.end = QStringLiteral("s");
      edge.label = QStringLiteral("Hi");
      edge.labelPosition = QPointF(60.0, 60.0);
      edge.labelSize = QSizeF(40.0, 24.0);
      edge.labelBackground = QStringLiteral("#ffffff");
      edge.labelBackgroundCss.opacity = pOpacity;
      edge.labelBackgroundCss.displayed = pDisplayed;
      scene.edges.append(std::move(edge));
      scene.nodes.append(StateSceneNode{});
      scene.nodes.last().id = QStringLiteral("s");
      scene.nodes.last().bounds = QRectF(0.0, 0.0, 10.0, 10.0);
      scene.bounds = QRectF(0.0, 0.0, 120.0, 120.0);
      // renderStateSceneToImage paints through translate(padding - bounds.topLeft)
      // with the default padding 8: image pixel = scene point + (8, 8).
      return renderStateSceneToImage(scene).pixelColor(68, 58);
    };
    const QColor dimmed = labelAlpha(0.5, true);
    if (std::abs(dimmed.alpha() - 128) > 3)
      fail(QStringLiteral("p opacity must dim the label background: %1 != ~128")
               .arg(dimmed.name(QColor::HexArgb)));
    const QColor hidden = labelAlpha(1.0, false);
    if (hidden.alpha() != 0)
      fail(QStringLiteral("p display:none must hide the whole label: (%1)")
               .arg(hidden.name(QColor::HexArgb)));
  }

  // The edge-label <p>'s font pair and color are the TEXT's used style: a
  // 6px red p paints tiny RED glyphs (the span's slots stay at theme size).
  {
    StateScene scene;
    scene.style.edgeLabelBackground = QStringLiteral("transparent");
    scene.style.transitionLabelColor = QStringLiteral("#000000");
    StateSceneEdge edge;
    edge.id = QStringLiteral("e1");
    edge.start = edge.end = QStringLiteral("s");
    edge.label = QStringLiteral("Hi");
    edge.labelPosition = QPointF(60.0, 60.0);
    edge.labelSize = QSizeF(40.0, 24.0);
    edge.labelBackgroundCss.fontSize = 6.0;
    edge.labelBackgroundCss.color = QStringLiteral("#ff0000");
    edge.labelDocument = flowchart::parseFlowLabel(
        QStringLiteral("Hi"), QStringLiteral("markdown"));
    scene.edges.append(std::move(edge));
    scene.nodes.append(StateSceneNode{});
    scene.nodes.last().id = QStringLiteral("s");
    scene.nodes.last().bounds = QRectF(0.0, 0.0, 10.0, 10.0);
    scene.bounds = QRectF(0.0, 0.0, 120.0, 120.0);
    const QImage image = renderStateSceneToImage(scene);
    // image pixel = scene point + (8, 8): the ink row sits at image y 66-68.
    // The INK WIDTH separates a 6px paint (~6-7 device px) from the span's
    // 16px fallback (~13-14), and the ink must be RED (not the span black).
    int inkMin = 1000, inkMax = -1;
    bool redInk = false;
    for (int x = 50; x <= 90; ++x)
      for (int y = 62; y <= 72; ++y) {
        const QColor c = image.pixelColor(x, y);
        if (c.alpha() > 40) {
          inkMin = std::min(inkMin, x);
          inkMax = std::max(inkMax, x);
          if (c.red() > 150 && c.green() < 100) redInk = true;
        }
      }
    if (inkMax < 0)
      fail(QStringLiteral("p font must still paint the label text"));
    if (inkMax - inkMin + 1 > 10)
      fail(QStringLiteral("6px p text ink width %1 is span-sized (%2..%3)")
               .arg(inkMax - inkMin + 1).arg(inkMin).arg(inkMax));
    if (!redInk)
      fail(QStringLiteral("p color must restyle the label text (no red ink)"));
  }

  // stateEnd's neo shadow input is the GROUP's actual rendering: a ring that
  // paints NOTHING (fill:none;stroke:none) with a visible inner dot casts a
  // DOT-sized shadow — never a full ring silhouette with the dot's alpha.
  {
    StateScene scene;
    scene.style.neo = true;
    scene.style.shadowCss =
        QStringLiteral("drop-shadow(4px 4px 0px rgba(0,0,0,1))");
    StateSceneNode node;
    node.id = QStringLiteral("end1");
    node.shape = QStringLiteral("stateEnd");
    node.bounds = QRectF(40.0, 40.0, 40.0, 40.0);  // center (60,60), ring r7
    node.fill = QStringLiteral("none");
    // stateEnd is a rough.js pair even under classic look: the ring's stroke
    // element is the stroke-path slot (shapeStrokeCss) — the lineColor
    // fallback wins over the plain node.stroke field for pseudostates.
    node.shapeStrokeCss.stroke = QStringLiteral("none");
    node.innerFill = QStringLiteral("#000000");
    scene.nodes.append(node);
    scene.bounds = QRectF(0.0, 0.0, 140.0, 140.0);
    const QImage image = renderStateSceneToImage(scene);
    const QColor dotShadow = image.pixelColor(72, 72);
    if (dotShadow.alpha() < 200)
      fail(QStringLiteral("visible dot must cast its shadow: (%1)")
               .arg(dotShadow.name(QColor::HexArgb)));
    // The ring's top edge band (ring r7 ± its 2px stroke, image y 60-62):
    // 7px from the dot center and 11px from the dot-shadow center — only a
    // ring-collapsed shadow (or a ring stroke that ignored its none channel)
    // inks it.
    const QColor ringBand = image.pixelColor(68, 61);
    if (ringBand.alpha() != 0)
      fail(QStringLiteral("transparent ring must not cast a ring shadow: (%1)")
               .arg(ringBand.name(QColor::HexArgb)));
  }
  return 0;
}
