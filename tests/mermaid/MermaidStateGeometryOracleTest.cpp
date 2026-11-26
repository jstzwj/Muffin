// State geometry parity oracle: compares Muffin's native state::StateScene,
// through the render cache, against real mermaid 11.16.0 stateDiagram-v2
// geometry captured by scripts/generate_mermaid_state_geometry_fixture.mjs
// (headless Chrome via the sibling mermaid-cli checkout).
//
// The fixture is captured in the bundled Noto fonts, which is also what the
// port measures text in, so no number here depends on the machine's fonts.
// Everything is relative to the centre of the first node the document shows,
// the origin Muffin's layout oracle subtracts too.
//
// ASSERTS, every case fail-on-divergence:
//   - node count + transition count (topology)
//   - every state, note and pseudo-state: its centre, its box, each of its
//     text boxes (a plain state's label; a titled state's title, its divider
//     and the box its description rows take) and their texts
//   - every composite: each rectangle it draws and its title
//   - every transition: its ends, its marker, every waypoint the layout gave
//     it, the path drawn from them and its label box
//   - the drawn canvas (the SVG's getBBox())

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/ParityDiff.h"
#include "mermaid/state/StateScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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
      QString::fromLatin1("fuzz-000"), QString::fromLatin1("fuzz-007"), QString::fromLatin1("fuzz-008"),
      QString::fromLatin1("fuzz-009"), QString::fromLatin1("fuzz-011"), QString::fromLatin1("fuzz-016"),
      QString::fromLatin1("fuzz-017"), QString::fromLatin1("fuzz-018"), QString::fromLatin1("fuzz-020"),
      QString::fromLatin1("fuzz-021"), QString::fromLatin1("fuzz-023"), QString::fromLatin1("fuzz-025"),
      QString::fromLatin1("fuzz-028"), QString::fromLatin1("fuzz-029"), QString::fromLatin1("fuzz-030"),
      QString::fromLatin1("fuzz-031"), QString::fromLatin1("fuzz-037"), QString::fromLatin1("fuzz-038"),
      QString::fromLatin1("fuzz-043"), QString::fromLatin1("fuzz-047"), QString::fromLatin1("fuzz-048"),
      QString::fromLatin1("fuzz-053"), QString::fromLatin1("fuzz-054"), QString::fromLatin1("fuzz-056"),
      QString::fromLatin1("fuzz-061"), QString::fromLatin1("fuzz-065"), QString::fromLatin1("fuzz-066"),
      QString::fromLatin1("fuzz-068"), QString::fromLatin1("fuzz-071"), QString::fromLatin1("fuzz-074"),
      QString::fromLatin1("fuzz-076"), QString::fromLatin1("fuzz-077"), QString::fromLatin1("fuzz-078"),
      QString::fromLatin1("fuzz-079"), QString::fromLatin1("fuzz-082"), QString::fromLatin1("fuzz-084"),
      QString::fromLatin1("fuzz-086"), QString::fromLatin1("fuzz-087"), QString::fromLatin1("fuzz-088"),
      QString::fromLatin1("fuzz-091"), QString::fromLatin1("fuzz-093"), QString::fromLatin1("fuzz-095"),
      QString::fromLatin1("fuzz-096"), QString::fromLatin1("fuzz-105"), QString::fromLatin1("fuzz-114"),
      QString::fromLatin1("fuzz-117"), QString::fromLatin1("fuzz-121"), QString::fromLatin1("fuzz-124"),
      QString::fromLatin1("fuzz-126"), QString::fromLatin1("fuzz-129"), QString::fromLatin1("fuzz-131"),
      QString::fromLatin1("fuzz-132"), QString::fromLatin1("fuzz-134"), QString::fromLatin1("fuzz-140"),
      QString::fromLatin1("fuzz-142"), QString::fromLatin1("fuzz-144"), QString::fromLatin1("fuzz-146"),
      QString::fromLatin1("fuzz-149"), QString::fromLatin1("fuzz-151"), QString::fromLatin1("fuzz-152"),
      QString::fromLatin1("fuzz-153"), QString::fromLatin1("fuzz-154"), QString::fromLatin1("fuzz-155"),
      QString::fromLatin1("fuzz-157"), QString::fromLatin1("fuzz-159"), QString::fromLatin1("fuzz-162"),
      QString::fromLatin1("fuzz-167"), QString::fromLatin1("fuzz-169"), QString::fromLatin1("fuzz-172"),
      QString::fromLatin1("fuzz-174"), QString::fromLatin1("fuzz-176"), QString::fromLatin1("fuzz-178"),
      QString::fromLatin1("fuzz-179"), QString::fromLatin1("fuzz-180"), QString::fromLatin1("fuzz-181"),
      QString::fromLatin1("fuzz-185"), QString::fromLatin1("fuzz-187"), QString::fromLatin1("fuzz-189"),
      QString::fromLatin1("fuzz-193"), QString::fromLatin1("fuzz-195"), QString::fromLatin1("fuzz-197"),
      QString::fromLatin1("fuzz-198"), QString::fromLatin1("fuzz-199"),
  };
}
QString handDigest() {
  return QString::fromLatin1("ad812b9ccd071bce68433cd4a1961a640a09346c9052c671c1b59c27f6ed0a53");
}
QString fuzzDigest() {
  return QString::fromLatin1("9fe55bcd213c93bda8abe4a9fc46df886a48d361d5b95b687836df2694cd492d");
}
constexpr int handCases = 23;
constexpr int fuzzCases = 165;

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

QString shownText(const flowchart::FlowLabelDocument& document) {
  QString text = document.text;
  text.remove(QLatin1Char('\n'));
  return text;
}

QStringList compareRect(const QRectF& actual, const QJsonValue& expected, parity::Tier tier, const QString& path) {
  const QRectF e = rectOf(expected);
  QStringList out;
  out += parity::compareNumber(actual.x(), e.x(), tier, path + "/x");
  out += parity::compareNumber(actual.y(), e.y(), tier, path + "/y");
  out += parity::compareNumber(actual.width(), e.width(), tier, path + "/width");
  out += parity::compareNumber(actual.height(), e.height(), tier, path + "/height");
  return out;
}

QStringList comparePoints(const QVector<QPointF>& actual, const QJsonArray& expected, const QString& path) {
  QStringList out;
  if (actual.size() != expected.size()) {
    out << QStringLiteral("%1: %2 points, upstream %3").arg(path).arg(actual.size()).arg(expected.size());
    return out;
  }
  for (int i = 0; i < expected.size(); ++i) {
    const QJsonObject point = expected.at(i).toObject();
    out += parity::compareNumber(actual.at(i).x(), point.value(QStringLiteral("x")).toDouble(), kCoordinate,
                                 QStringLiteral("%1[%2]/x").arg(path).arg(i));
    out += parity::compareNumber(actual.at(i).y(), point.value(QStringLiteral("y")).toDouble(), kCoordinate,
                                 QStringLiteral("%1[%2]/y").arg(path).arg(i));
  }
  return out;
}

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

template <typename T> const T* findById(const QVector<T>& items, const QString& id) {
  const auto found = std::find_if(items.cbegin(), items.cend(), [&](const T& item) { return item.id == id; });
  return found == items.cend() ? nullptr : &*found;
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected state geometry fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  // The same comparison reads the hand-written corpus and the random one;
  // each has its own digest and size.
  const bool fuzz = QFileInfo(QString::fromLocal8Bit(argv[1])).fileName() == QLatin1String("state-geometry-fuzz.json");
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              (fuzz ? fuzzDigest() : handDigest()).toLatin1(),
          QStringLiteral("State geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("State geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("State geometry: fontMode drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("stateDiagram.render")),
          QStringLiteral("State geometry: oracle contract drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == (fuzz ? fuzzCases : handCases),
          QStringLiteral("State geometry case count drifted: %1").arg(cases.size()));
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
    const auto* scene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    if (!(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr)) {
      failures << id + QStringLiteral(": native state render failed: ") + entry.errorMessage;
      continue;
    }
    QStringList divergences;
    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    const QJsonArray expectedClusters = expected.value(QStringLiteral("clusters")).toArray();
    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();

    // The origin: the centre of the first node the document shows.
    QPointF origin;
    if (!expectedNodes.isEmpty()) {
      const auto* originNode =
          findById(scene->nodes, object(expectedNodes.first()).value(QStringLiteral("id")).toString());
      if (!originNode) {
        divergences << id + QStringLiteral(": no native node for the origin");
        continue;
      }
      origin = originNode->bounds.center();
    }
    const auto about = [&](const QRectF& rect) { return rect.translated(-origin); };
    const auto aboutPoint = [&](const QPointF& point) { return point - origin; };

    if (scene->nodes.size() != expectedNodes.size())
      divergences
          << QStringLiteral("%1: %2 nodes, upstream %3").arg(id).arg(scene->nodes.size()).arg(expectedNodes.size());
    for (const QJsonValue& value : expectedNodes) {
      const QJsonObject exp = value.toObject();
      const auto* node = findById(scene->nodes, exp.value(QStringLiteral("id")).toString());
      const QString path = id + QStringLiteral("/node/%1").arg(exp.value(QStringLiteral("id")).toString());
      if (!node) {
        divergences << path + QStringLiteral(": no native node");
        continue;
      }
      const QPointF centre(object(exp.value(QStringLiteral("centre"))).value(QStringLiteral("x")).toDouble(),
                           object(exp.value(QStringLiteral("centre"))).value(QStringLiteral("y")).toDouble());
      divergences +=
          parity::compareNumber(aboutPoint(node->bounds.center()).x(), centre.x(), kCoordinate, path + "/centre/x");
      divergences +=
          parity::compareNumber(aboutPoint(node->bounds.center()).y(), centre.y(), kCoordinate, path + "/centre/y");
      // The box in the node's own coordinates.
      divergences += compareRect(node->bounds.translated(-node->bounds.center()), exp.value(QStringLiteral("box")),
                                 kCoordinate, path + "/box");
      const QJsonArray labels = exp.value(QStringLiteral("labels")).toArray();
      if (labels.size() == 1) {
        divergences += compareRect(about(node->labelBounds), object(labels.first()).value(QStringLiteral("rect")),
                                   kLabel, path + "/label");
        const QString text = object(labels.first()).value(QStringLiteral("text")).toString();
        if (shownText(node->labelDocument) != text)
          divergences << QStringLiteral("%1/label/text: native \"%2\", upstream \"%3\"")
                             .arg(path, shownText(node->labelDocument), text);
      } else if (labels.size() == 2) {
        divergences += compareRect(about(node->titleBounds), object(labels.first()).value(QStringLiteral("rect")),
                                   kLabel, path + "/title");
        const QRectF description = about(node->descriptionBounds);
        const QRectF expectedDescription = rectOf(object(labels.last()).value(QStringLiteral("rect")));
        const QString at = path + "/description";
        divergences += parity::compareNumber(description.x(), expectedDescription.x(), kLabel, at + "/x");
        divergences += parity::compareNumber(description.y(), expectedDescription.y(), kLabel, at + "/y");
        divergences += parity::compareNumber(description.width(), expectedDescription.width(), kLabel, at + "/width");
        divergences += parity::compareNumber(description.height(),
                                             object(labels.last()).value(QStringLiteral("contentHeight")).toDouble(),
                                             kLabel, at + "/height");
        if (!node->divider) {
          divergences << path + "/divider: native none, upstream one";
        } else {
          const QJsonValue divider = exp.value(QStringLiteral("divider"));
          const QPointF from = aboutPoint(node->divider->p1());
          const QPointF to = aboutPoint(node->divider->p2());
          divergences += parity::compareNumber(
              from.x(), object(object(divider).value(QStringLiteral("from"))).value(QStringLiteral("x")).toDouble(),
              kCoordinate, path + "/divider/from/x");
          divergences += parity::compareNumber(
              from.y(), object(object(divider).value(QStringLiteral("from"))).value(QStringLiteral("y")).toDouble(),
              kCoordinate, path + "/divider/from/y");
          divergences += parity::compareNumber(
              to.x(), object(object(divider).value(QStringLiteral("to"))).value(QStringLiteral("x")).toDouble(),
              kCoordinate, path + "/divider/to/x");
          divergences += parity::compareNumber(
              to.y(), object(object(divider).value(QStringLiteral("to"))).value(QStringLiteral("y")).toDouble(),
              kCoordinate, path + "/divider/to/y");
        }
      } else if (!labels.isEmpty()) {
        divergences << QStringLiteral("%1/labels: %2 native, upstream %3").arg(path).arg(0).arg(labels.size());
      } else if (!node->label.isEmpty() &&
                 (node->shape == QLatin1String("rect") || node->shape == QLatin1String("rectWithTitle") ||
                  node->shape == QLatin1String("note"))) {
        divergences << path + QStringLiteral("/labels: native has text, upstream none");
      }
    }

    if (scene->clusters.size() != expectedClusters.size())
      divergences << QStringLiteral("%1: %2 composites, upstream %3")
                         .arg(id)
                         .arg(scene->clusters.size())
                         .arg(expectedClusters.size());
    for (const QJsonValue& value : expectedClusters) {
      const QJsonObject exp = value.toObject();
      const auto* cluster = findById(scene->clusters, exp.value(QStringLiteral("id")).toString());
      const QString path = id + QStringLiteral("/composite/%1").arg(exp.value(QStringLiteral("id")).toString());
      if (!cluster) {
        divergences << path + QStringLiteral(": no native composite");
        continue;
      }
      const QJsonArray rects = exp.value(QStringLiteral("rects")).toArray();
      // A composite draws its outer box and its body; a divider or a note's
      // group draws one box.
      const int nativeRects = cluster->shape == QLatin1String("roundedWithTitle") ? 2 : 1;
      if (rects.size() != nativeRects)
        divergences << QStringLiteral("%1/rects: %2 native, upstream %3").arg(path).arg(nativeRects).arg(rects.size());
      for (const QJsonValue& rectValue : rects) {
        const QString kind = object(rectValue).value(QStringLiteral("kind")).toString();
        const QRectF native = kind == QLatin1String("inner") ? cluster->innerBounds : cluster->bounds;
        divergences += compareRect(about(native), object(rectValue).value(QStringLiteral("rect")), kCoordinate,
                                   path + "/rects/" + (kind.isEmpty() ? QStringLiteral("box") : kind));
      }
      const QJsonValue title = exp.value(QStringLiteral("title"));
      if (title.isObject()) {
        divergences += compareRect(about(cluster->titleBounds), object(title).value(QStringLiteral("rect")), kLabel,
                                   path + "/title");
        const QString text = object(title).value(QStringLiteral("text")).toString();
        if (shownText(cluster->labelDocument) != text)
          divergences << QStringLiteral("%1/title/text: native \"%2\", upstream \"%3\"")
                             .arg(path, shownText(cluster->labelDocument), text);
      }
    }

    if (scene->edges.size() != expectedEdges.size())
      divergences << QStringLiteral("%1: %2 transitions, upstream %3")
                         .arg(id)
                         .arg(scene->edges.size())
                         .arg(expectedEdges.size());
    for (const QJsonValue& value : expectedEdges) {
      const QJsonObject exp = value.toObject();
      const auto* edge = findById(scene->edges, exp.value(QStringLiteral("id")).toString());
      const QString path = id + QStringLiteral("/transition/%1").arg(exp.value(QStringLiteral("id")).toString());
      if (!edge) {
        divergences << path + QStringLiteral(": no native transition");
        continue;
      }
      if (edge->start != exp.value(QStringLiteral("from")).toString() ||
          edge->end != exp.value(QStringLiteral("to")).toString())
        divergences << QStringLiteral("%1: native %2 -> %3, upstream %4 -> %5")
                           .arg(path, edge->start, edge->end, exp.value(QStringLiteral("from")).toString(),
                                exp.value(QStringLiteral("to")).toString());
      // Muffin's arrow_barb is mermaid's barbEnd.
      QString nativeMarker = edge->markerEnd;
      if (nativeMarker == QLatin1String("arrow_barb"))
        nativeMarker = QLatin1String("barbEnd");
      const QString markerEnd = exp.value(QStringLiteral("markerEnd")).toString();
      if (nativeMarker != markerEnd)
        divergences
            << QStringLiteral("%1/markerEnd: native \"%2\", upstream \"%3\"").arg(path, edge->markerEnd, markerEnd);
      if (!exp.value(QStringLiteral("cut")).toBool()) {
        QVector<QPointF> points;
        for (const QPointF& point : edge->points)
          points.append(aboutPoint(point));
        divergences += comparePoints(points, exp.value(QStringLiteral("points")).toArray(), path + "/points");
      }
      divergences += parity::comparePath(translatedPath(edge->path, origin),
                                         exp.value(QStringLiteral("path")).toString(), kPath, path + "/path");
      const QJsonValue label = exp.value(QStringLiteral("label"));
      if (label.isObject()) {
        if (!edge->labelBounds.isValid())
          divergences << path + "/label: upstream has one, native none";
        else {
          divergences += compareRect(about(edge->labelBounds), object(label).value(QStringLiteral("rect")), kLabel,
                                     path + "/label");
          const QString text = object(label).value(QStringLiteral("text")).toString();
          if (shownText(edge->labelDocument) != text)
            divergences << QStringLiteral("%1/label/text: native \"%2\", upstream \"%3\"")
                               .arg(path, shownText(edge->labelDocument), text);
        }
      } else if (edge->labelBounds.isValid()) {
        divergences << path + "/label: native has one, upstream none";
      }
    }

    // The drawn canvas, about the origin.
    // The scene's bounds are the content grown by the SVG's padding.
    divergences += compareRect(about(scene->bounds).adjusted(8.0, 8.0, -8.0, -8.0),
                               object(expected.value(QStringLiteral("canvas"))).value(QStringLiteral("content")),
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
    std::fprintf(stderr, "MermaidStateGeometryOracleTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::printf(
      "MermaidStateGeometryOracleTest passed: %lld cases, every state, note, transition, composite and canvas\n",
      static_cast<long long>(cases.size()));
  // The listed cases are not compared: say so, that a pass is not read as parity.
  if (!open.isEmpty())
    std::fprintf(stderr,
                 "MuffinMermaidStateGeometryFuzzTest: %lld of the %lld cases are listed as open divergences and were "
                 "not compared (MUFFIN_GEOMETRY_SHOW_OPEN=1 reports them)\n",
                 static_cast<long long>(open.size()), static_cast<long long>(cases.size()));
  return 0;
}
