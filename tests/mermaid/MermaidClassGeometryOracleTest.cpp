// Class geometry parity oracle: compares Muffin's native classdiagram::ClassScene,
// through the render cache, against real mermaid 11.16.0 classDiagram geometry
// captured by scripts/generate_mermaid_class_geometry_fixture.mjs (headless
// Chrome via the sibling mermaid-cli checkout).
//
// The fixture is captured in the bundled Noto fonts, which is also what the
// port measures text in, so no number here depends on the machine's fonts.
// Everything but the node-local parts is relative to the first source-order
// class's centre, the origin the native layout subtracts too.
//
// ASSERTS, every case fail-on-divergence:
//   - node count + edge count (topology)
//   - every class and note: its centre, its outline, its dividers (where each
//     starts and ends) and each of its text rows (annotation, name, members,
//     methods; a note's text)
//   - every relationship: the waypoints the layout gave each of its paths and
//     the path drawn from them, its pattern, its markers, its label and its
//     cardinalities (their text box, and the box the SVG clips them to)
//   - every namespace: its box and its title
//   - the drawn canvas (the content's bounding box, which the SVG's getBBox()
//     reports), and the canvas the browser sizes the image to
//
// Texts are the ones the labels show: a member's markdown escapes and entities
// are resolved on the way to the document, which is what is compared.
//
// Marker normalization: Muffin emits "none" for a marker-less end; mermaid emits
// null. Both normalize to empty.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/ParityDiff.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

// Random cases (the fuzz corpus) whose geometry still differs from
// mermaid's. The list can only shrink: a case in it must still diverge, and
// no other case may.
QStringList openDivergences() {
  return {
      QString::fromLatin1("fuzz-003"), QString::fromLatin1("fuzz-014"), QString::fromLatin1("fuzz-020"),
      QString::fromLatin1("fuzz-021"), QString::fromLatin1("fuzz-025"), QString::fromLatin1("fuzz-029"),
      QString::fromLatin1("fuzz-031"), QString::fromLatin1("fuzz-032"), QString::fromLatin1("fuzz-034"),
      QString::fromLatin1("fuzz-036"), QString::fromLatin1("fuzz-037"), QString::fromLatin1("fuzz-041"),
      QString::fromLatin1("fuzz-043"), QString::fromLatin1("fuzz-050"), QString::fromLatin1("fuzz-051"),
      QString::fromLatin1("fuzz-055"), QString::fromLatin1("fuzz-062"), QString::fromLatin1("fuzz-065"),
      QString::fromLatin1("fuzz-071"), QString::fromLatin1("fuzz-074"), QString::fromLatin1("fuzz-076"),
      QString::fromLatin1("fuzz-079"), QString::fromLatin1("fuzz-082"), QString::fromLatin1("fuzz-083"),
      QString::fromLatin1("fuzz-085"), QString::fromLatin1("fuzz-099"), QString::fromLatin1("fuzz-103"),
      QString::fromLatin1("fuzz-108"), QString::fromLatin1("fuzz-116"), QString::fromLatin1("fuzz-120"),
      QString::fromLatin1("fuzz-122"), QString::fromLatin1("fuzz-126"), QString::fromLatin1("fuzz-131"),
      QString::fromLatin1("fuzz-136"), QString::fromLatin1("fuzz-142"), QString::fromLatin1("fuzz-143"),
      QString::fromLatin1("fuzz-158"), QString::fromLatin1("fuzz-163"), QString::fromLatin1("fuzz-166"),
      QString::fromLatin1("fuzz-170"), QString::fromLatin1("fuzz-172"), QString::fromLatin1("fuzz-185"),
      QString::fromLatin1("fuzz-186"), QString::fromLatin1("fuzz-187"), QString::fromLatin1("fuzz-188"),
      QString::fromLatin1("fuzz-189"), QString::fromLatin1("fuzz-193"), QString::fromLatin1("fuzz-195"),
  };
}
QString handDigest() {
  return QString::fromLatin1("780eefaae1783f611ded7706efd1135dc3865abd048fe5a946b3aa59b528afa3");
}
QString fuzzDigest() {
  return QString::fromLatin1("5e2edff92c47c8fa95eec00350640d6372f629a99ef84924358ad884fcc7d889");
}
constexpr int handCases = 32;
constexpr int fuzzCases = 200;

// Tolerances in px. A coordinate is where a shape is put; a label box is a
// text's measured size, which has the port's residual against Blink's.
constexpr parity::Tier kCoordinate{0.05};
constexpr parity::Tier kLabel{0.2};
constexpr parity::Tier kPath{0.05};

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

QJsonObject object(const QJsonValue& value) {
  return value.toObject();
}

QRectF rectOf(const QJsonValue& value) {
  const QJsonObject r = object(value);
  return QRectF(r.value(QStringLiteral("x")).toDouble(), r.value(QStringLiteral("y")).toDouble(),
                r.value(QStringLiteral("width")).toDouble(), r.value(QStringLiteral("height")).toDouble());
}

QRectF rectAbout(const QPointF& centre, const QSizeF& size) {
  return QRectF(centre - QPointF(size.width() / 2.0, size.height() / 2.0), size);
}

// The native layout counts from the first class of its own list, the fixture
// from the first of mermaid's; where the two are not the same class, every
// scene coordinate is moved by this before it is compared. (Node-local parts
// are not: they are shifted back.)
QPointF originShift;

// A path's data moved by an offset: every second number is an x.
QString translatedPath(const QString& path, const QPointF& offset) {
  static const QRegularExpression number(QStringLiteral("-?\\d+(?:\\.\\d+)?(?:e[-+]?\\d+)?"));
  QString out;
  qsizetype consumed = 0;
  int index = 0;
  auto matches = number.globalMatch(path);
  while (matches.hasNext()) {
    const auto match = matches.next();
    out += path.mid(consumed, match.capturedStart() - consumed);
    out += QString::number(match.captured().toDouble() - (index % 2 == 0 ? offset.x() : offset.y()), 'g', 10);
    consumed = match.capturedEnd();
    ++index;
  }
  out += path.mid(consumed);
  return out;
}

QStringList compareRect(const QRectF& actual, const QJsonValue& expected, parity::Tier tier, const QString& path) {
  const QRectF a = actual.translated(-originShift);
  const QRectF e = rectOf(expected);
  QStringList out;
  out += parity::compareNumber(a.x(), e.x(), tier, path + "/x");
  out += parity::compareNumber(a.y(), e.y(), tier, path + "/y");
  out += parity::compareNumber(actual.width(), e.width(), tier, path + "/width");
  out += parity::compareNumber(actual.height(), e.height(), tier, path + "/height");
  return out;
}

// The text a label's document holds, as the upstream DOM shows it.
QString shownText(const flowchart::FlowLabelDocument& document) {
  QString text = document.text;
  text.remove(QLatin1Char('\n'));
  return text;
}

// A label's box as the scene holds it: its centre and its size. A node's
// own labels are in its coordinates, in the fixture as in the scene, so they
// are shifted back like its outline.
QStringList compareLabel(const classdiagram::ClassSceneLabel& label, const QJsonValue& expected, const QString& path,
                         bool sceneCoordinates = true) {
  const QJsonObject e = object(expected);
  QStringList out =
      compareRect(rectAbout(label.center, label.size).translated(sceneCoordinates ? QPointF() : originShift), expected,
                  kLabel, path);
  const QString text = e.value(QStringLiteral("text")).toString();
  if (shownText(label.document) != text)
    out << QStringLiteral("%1/text: native \"%2\", expected \"%3\"").arg(path, shownText(label.document), text);
  return out;
}

// Normalize a marker field to a canonical token: Muffin's "none" and mermaid's
// null both mean "no marker at this end".
QString normMarker(const QJsonValue& v) {
  const QString s = v.toString();
  return (s.isEmpty() || s == QLatin1String("none")) ? QString() : s;
}

// Edge tuple key: pattern | start-kind | end-kind (font-independent identity).
QString edgeTuple(const QString& pattern, const QJsonValue& markerStart, const QJsonValue& markerEnd) {
  return pattern + QLatin1Char('|') + normMarker(markerStart) + QLatin1Char('|') + normMarker(markerEnd);
}

// The points of a path, as the scene holds them against the waypoints the
// layout gave it.
QStringList comparePoints(const QVector<QPointF>& actual, const QJsonArray& expected, const QString& path) {
  QStringList out;
  if (actual.size() != expected.size()) {
    out << QStringLiteral("%1: %2 points, upstream %3").arg(path).arg(actual.size()).arg(expected.size());
    return out;
  }
  for (int i = 0; i < expected.size(); ++i) {
    const QJsonObject point = expected.at(i).toObject();
    const QPointF native = actual.at(i) - originShift;
    out += parity::compareNumber(native.x(), point.value(QStringLiteral("x")).toDouble(), kCoordinate,
                                 QStringLiteral("%1[%2]/x").arg(path).arg(i));
    out += parity::compareNumber(native.y(), point.value(QStringLiteral("y")).toDouble(), kCoordinate,
                                 QStringLiteral("%1[%2]/y").arg(path).arg(i));
  }
  return out;
}

template <typename T> const T* findById(const QVector<T>& items, const QString& id) {
  const auto found = std::find_if(items.cbegin(), items.cend(), [&](const T& item) { return item.id == id; });
  return found == items.cend() ? nullptr : &*found;
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected class geometry fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  // The same comparison reads the hand-written corpus and the random one;
  // each has its own digest and size.
  const bool fuzz = QFileInfo(QString::fromLocal8Bit(argv[1])).fileName() == QLatin1String("class-geometry-fuzz.json");
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              (fuzz ? fuzzDigest() : handDigest()).toLatin1(),
          QStringLiteral("Class geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Class geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("Class geometry: fontMode drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("classDiagram.render")),
          QStringLiteral("Class geometry: oracle contract drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == (fuzz ? fuzzCases : handCases),
          QStringLiteral("Class geometry case count drifted: %1").arg(cases.size()));
  // The list is named: the iterators the set is built from must point
  // into the same list.
  const QStringList openIds = fuzz ? openDivergences() : QStringList();
  const QSet<QString> open(openIds.cbegin(), openIds.cend());
  // Set to report what the open cases still differ in, to work on them.
  const bool showOpen = qEnvironmentVariableIntValue("MUFFIN_GEOMETRY_SHOW_OPEN") == 1;
  QSet<QString> seen;
  for (const QJsonValue& caseValue : cases)
    seen.insert(object(caseValue).value(QStringLiteral("id")).toString());
  for (const QString& id : open)
    require(seen.contains(id), QStringLiteral("An open divergence is no longer in the corpus: %1").arg(id));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* scene = dynamic_cast<const classdiagram::ClassScene*>(entry.scene.get());
    if (!(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr)) {
      failures << id + QStringLiteral(": native class render failed: ") + entry.errorMessage;
      continue;
    }
    QStringList divergences;

    // Nodes: classes and notes, by id.
    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    // The fixture's origin is the class at (0, 0) in its list.
    originShift = QPointF();
    for (const QJsonValue& value : expectedNodes) {
      const QJsonObject exp = value.toObject();
      const QJsonObject centre = object(exp.value(QStringLiteral("centre")));
      if (centre.value(QStringLiteral("x")).toDouble() == 0.0 && centre.value(QStringLiteral("y")).toDouble() == 0.0) {
        if (const auto* origin = findById(scene->nodes, exp.value(QStringLiteral("id")).toString()))
          originShift = origin->center;
        break;
      }
    }
    if (scene->nodes.size() != expectedNodes.size())
      divergences
          << QStringLiteral("%1: %2 nodes, upstream %3").arg(id).arg(scene->nodes.size()).arg(expectedNodes.size());
    for (const QJsonValue& value : expectedNodes) {
      const QJsonObject exp = value.toObject();
      const QString nodeId = exp.value(QStringLiteral("id")).toString();
      const auto* node = findById(scene->nodes, nodeId);
      const QString path = id + QStringLiteral("/node/%1").arg(nodeId);
      if (!node) {
        divergences << path + QStringLiteral(": no native node");
        continue;
      }
      const QPointF centre = node->center - originShift;
      divergences += parity::compareNumber(
          centre.x(), object(exp.value(QStringLiteral("centre"))).value(QStringLiteral("x")).toDouble(), kCoordinate,
          path + "/centre/x");
      divergences += parity::compareNumber(
          centre.y(), object(exp.value(QStringLiteral("centre"))).value(QStringLiteral("y")).toDouble(), kCoordinate,
          path + "/centre/y");
      // The outline is in the node's own coordinates: compareRect() would move
      // it, so it is moved back first.
      divergences += compareRect(node->localOuter.translated(originShift), exp.value(QStringLiteral("local")),
                                 kCoordinate, path + "/outline");
      const QJsonArray dividers = exp.value(QStringLiteral("dividers")).toArray();
      if (node->localDividers.size() != dividers.size())
        divergences << QStringLiteral("%1/dividers: %2, upstream %3")
                           .arg(path)
                           .arg(node->localDividers.size())
                           .arg(dividers.size());
      for (int i = 0; i < dividers.size() && i < node->localDividers.size(); ++i) {
        const QJsonObject divider = dividers.at(i).toObject();
        const QRectF actual = node->localDividers.at(i);
        const QJsonValue from = divider.value(QStringLiteral("from"));
        divergences += parity::compareNumber(actual.left(), object(from).value(QStringLiteral("x")).toDouble(),
                                             kCoordinate, QStringLiteral("%1/divider[%2]/from/x").arg(path).arg(i));
        divergences += parity::compareNumber(actual.top(), object(from).value(QStringLiteral("y")).toDouble(),
                                             kCoordinate, QStringLiteral("%1/divider[%2]/from/y").arg(path).arg(i));
        divergences += parity::compareNumber(
            actual.right(), object(divider.value(QStringLiteral("to"))).value(QStringLiteral("x")).toDouble(),
            kCoordinate, QStringLiteral("%1/divider[%2]/to/x").arg(path).arg(i));
        divergences +=
            parity::compareNumber(actual.top() + actual.height(),
                                  object(divider.value(QStringLiteral("to"))).value(QStringLiteral("y")).toDouble(),
                                  kCoordinate, QStringLiteral("%1/divider[%2]/to/y").arg(path).arg(i));
      }
      const auto rows = [&](const QVector<classdiagram::ClassSceneLabel>& labels, const QString& key) {
        const QJsonArray expectedRows = exp.value(key).toArray();
        if (labels.size() != expectedRows.size()) {
          divergences << QStringLiteral("%1/%2: %3 rows, upstream %4")
                             .arg(path, key)
                             .arg(labels.size())
                             .arg(expectedRows.size());
          return;
        }
        for (int i = 0; i < labels.size(); ++i)
          divergences +=
              compareLabel(labels.at(i), expectedRows.at(i), QStringLiteral("%1/%2[%3]").arg(path, key).arg(i), false);
      };
      const bool note = exp.value(QStringLiteral("note")).toBool();
      if (!note) {
        rows(node->annotationLabels, QStringLiteral("annotation"));
        rows(node->nameLabels, QStringLiteral("name"));
        rows(node->memberLabels, QStringLiteral("members"));
        rows(node->methodLabels, QStringLiteral("methods"));
      } else if (!node->nameLabels.isEmpty()) {
        divergences +=
            compareLabel(node->nameLabels.constFirst(), exp.value(QStringLiteral("noteLabel")), path + "/note", false);
      }
    }

    // Namespaces, by id.
    const QJsonArray expectedClusters = expected.value(QStringLiteral("clusters")).toArray();
    if (scene->clusters.size() != expectedClusters.size())
      divergences << QStringLiteral("%1: %2 namespaces, upstream %3")
                         .arg(id)
                         .arg(scene->clusters.size())
                         .arg(expectedClusters.size());
    for (const QJsonValue& value : expectedClusters) {
      const QJsonObject exp = value.toObject();
      const auto* cluster = findById(scene->clusters, exp.value(QStringLiteral("id")).toString());
      const QString path = id + QStringLiteral("/namespace/%1").arg(exp.value(QStringLiteral("id")).toString());
      if (!cluster) {
        divergences << path + QStringLiteral(": no native namespace");
        continue;
      }
      divergences += compareRect(cluster->bounds, exp.value(QStringLiteral("bounds")), kCoordinate, path + "/bounds");
      const QJsonValue title = exp.value(QStringLiteral("title"));
      if (!title.isObject()) {
        if (!cluster->titleLabel.text.isEmpty())
          divergences << path + "/title: native has a title, upstream none";
      } else if (cluster->titleLabel.text.isEmpty()) {
        divergences << path + "/title: upstream has a title, native none";
      } else {
        divergences += compareRect(rectAbout(cluster->titleLabel.center, cluster->titleLabel.size),
                                   object(title).value(QStringLiteral("rect")), kLabel, path + "/title/rect");
        const QString text = object(title).value(QStringLiteral("text")).toString();
        if (shownText(cluster->titleLabel.document) != text)
          divergences << QStringLiteral("%1/title/text: native \"%2\", upstream \"%3\"")
                             .arg(path, shownText(cluster->titleLabel.document), text);
      }
    }

    // Relationships, by id. (Of several a class has with itself, only the
    // last is drawn; the others are `superseded`.)
    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    int drawn = 0;
    for (const QJsonValue& value : expectedEdges)
      if (!value.toObject().value(QStringLiteral("superseded")).toBool())
        ++drawn;
    if (scene->edges.size() != drawn)
      divergences << QStringLiteral("%1: %2 relationships, upstream %3").arg(id).arg(scene->edges.size()).arg(drawn);
    QStringList expectedTuples, actualTuples;
    for (const QJsonValue& value : expectedEdges) {
      const QJsonObject exp = value.toObject();
      const QString edgeId = exp.value(QStringLiteral("id")).toString();
      const QString pattern = exp.value(QStringLiteral("pattern")).toString();
      if (!exp.value(QStringLiteral("superseded")).toBool())
        expectedTuples << edgeTuple(pattern, exp.value(QStringLiteral("markerStart")),
                                    exp.value(QStringLiteral("markerEnd")));
      const auto* edge = findById(scene->edges, edgeId);
      const QString path = id + QStringLiteral("/edge/%1").arg(edgeId);
      if (exp.value(QStringLiteral("superseded")).toBool()) {
        // Of several relationships a class has with itself, only the last is
        // drawn: each is laid out under the same names.
        if (edge)
          divergences << path + QStringLiteral(": upstream drops it for a later one, native drew it");
        continue;
      }
      if (!edge) {
        divergences << path + QStringLiteral(": no native relationship");
        continue;
      }
      if (edge->pattern != pattern && !(edge->pattern.isEmpty() && pattern == QLatin1String("solid")))
        divergences << QStringLiteral("%1/pattern: native \"%2\", upstream \"%3\"").arg(path, edge->pattern, pattern);
      const QJsonArray paths = exp.value(QStringLiteral("paths")).toArray();
      const int segmentCount = edge->segments.isEmpty() ? 1 : edge->segments.size();
      if (segmentCount != paths.size()) {
        divergences << QStringLiteral("%1: %2 paths, upstream %3").arg(path).arg(segmentCount).arg(paths.size());
      } else {
        for (int i = 0; i < paths.size(); ++i) {
          const QJsonObject described = paths.at(i).toObject();
          const QVector<QPointF>& waypoints = edge->segments.isEmpty() ? edge->points : edge->segments.at(i);
          divergences += comparePoints(waypoints, described.value(QStringLiteral("points")).toArray(),
                                       QStringLiteral("%1/path[%2]/points").arg(path).arg(i));
          divergences += parity::comparePath(translatedPath(edge->paths.value(i), originShift),
                                             described.value(QStringLiteral("path")).toString(), kPath,
                                             QStringLiteral("%1/path[%2]").arg(path).arg(i));
        }
      }
      const QJsonValue label = exp.value(QStringLiteral("label"));
      if (label.isObject()) {
        if (!edge->labelBounds.isValid())
          divergences << path + "/label: upstream has a label, native none";
        else {
          divergences +=
              compareRect(edge->labelBounds, object(label).value(QStringLiteral("rect")), kLabel, path + "/label");
          const QString text = object(label).value(QStringLiteral("text")).toString();
          if (shownText(edge->labelDocument) != text)
            divergences << QStringLiteral("%1/label/text: native \"%2\", expected \"%3\"")
                               .arg(path, shownText(edge->labelDocument), text);
        }
      } else if (edge->labelBounds.isValid()) {
        divergences << path + "/label: native has a label, upstream none";
      }
      const QJsonObject terminals = exp.value(QStringLiteral("terminals")).toObject();
      const auto terminal = [&](const std::optional<classdiagram::ClassSceneTerminalLabel>& native,
                                const QString& key) {
        const QJsonValue expectedTerminal = terminals.value(key);
        if (!native) {
          if (expectedTerminal.isObject())
            divergences << QStringLiteral("%1/%2: upstream has one, native none").arg(path, key);
          return;
        }
        if (!expectedTerminal.isObject()) {
          divergences << QStringLiteral("%1/%2: native has one, upstream none").arg(path, key);
          return;
        }
        const QString at = QStringLiteral("%1/%2").arg(path, key);
        divergences +=
            compareRect(native->textBox, object(expectedTerminal).value(QStringLiteral("rect")), kLabel, at + "/text");
        divergences +=
            compareRect(native->box, object(expectedTerminal).value(QStringLiteral("box")), kLabel, at + "/box");
        const QString text = object(expectedTerminal).value(QStringLiteral("text")).toString();
        if (shownText(native->document) != text)
          divergences
              << QStringLiteral("%1/text: native \"%2\", upstream \"%3\"").arg(at, shownText(native->document), text);
      };
      terminal(edge->startLabelRight, QStringLiteral("startLabelRight"));
      terminal(edge->endLabelLeft, QStringLiteral("endLabelLeft"));
      for (const char* other : {"startLabelLeft", "endLabelRight"})
        if (terminals.value(QLatin1String(other)).isObject())
          divergences << QStringLiteral("%1/%2: upstream has a terminal the scene does not hold")
                             .arg(path, QLatin1String(other));
    }
    for (const classdiagram::ClassSceneEdge& edge : scene->edges)
      actualTuples << edgeTuple(edge.pattern, QJsonValue(edge.markerStart), QJsonValue(edge.markerEnd));
    actualTuples.sort();
    expectedTuples.sort();
    if (actualTuples != expectedTuples)
      divergences << QStringLiteral("%1: edge-tuple multiset diverged").arg(id);

    // The drawn canvas, about the origin.
    divergences +=
        compareRect(scene->bounds, object(expected.value(QStringLiteral("canvas"))).value(QStringLiteral("content")),
                    kCoordinate, id + "/canvas");

    if (open.contains(id) && !showOpen) {
      if (divergences.isEmpty())
        failures << id + QStringLiteral(": matches mermaid now; take it out of the open divergences");
      continue;
    }
    if (!divergences.isEmpty()) {
      std::fprintf(stderr, "[%s]\n", qPrintable(id));
      failures += divergences;
    }
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidClassGeometryOracleTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::printf("MermaidClassGeometryOracleTest passed: %lld cases, every class, note, path, label and canvas\n",
              static_cast<long long>(cases.size()));
  // The listed cases are not compared: say so, that a pass is not read as parity.
  if (!open.isEmpty())
    std::fprintf(stderr,
                 "MuffinMermaidClassGeometryFuzzTest: %lld of the %lld cases are listed as open divergences and were "
                 "not compared (MUFFIN_GEOMETRY_SHOW_OPEN=1 reports them)\n",
                 static_cast<long long>(open.size()), static_cast<long long>(cases.size()));
  return 0;
}
