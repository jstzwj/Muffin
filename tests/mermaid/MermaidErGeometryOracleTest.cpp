// Oracle for the erDiagram renderer against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-geometry.json is captured by
// scripts/generate_mermaid_er_geometry_fixture.mjs in the bundled Noto fonts,
// which is also what the port measures text in, so nothing here depends on
// the machine's fonts and every number is asserted: each entity table with
// its name, rows, cells, dividers and paint, each relationship path with its
// markers, each label, and the canvas. Entities are matched by their place in
// the diagram and relationship paths by the id Mermaid's document gives them.
// The fixture's coordinates are relative to the first source-order entity's
// centre, as the native scene's are, and a table's own to its centre.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/ParityDiff.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

constexpr qreal kGeometry = 0.05; // every coordinate and size (px)
constexpr parity::Tier kTier{kGeometry};

// Cases of a part of erDiagram the port does not draw yet. A part leaves the
// list as it lands, and the list is empty once the port is complete.
// Under the handDrawn look rough.js fills a shape with a hachure whose every
// line depends on the shape's size, so a text width a hundredth of a pixel
// off moves the sketch by a quarter: "order number" as an SVG text measures
// 106.200 in the port and 106.213 in Chrome, and the row it sits in is drawn
// otherwise.
const QSet<QString> kOpenDivergences = {
    "look-hand-drawn-svg-text",
};
qsizetype openCases = 0;

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

QRectF toRect(const QJsonValue& value) {
  const QJsonObject object = value.toObject();
  return QRectF(
      QPointF(object.value(QStringLiteral("x")).toDouble(), object.value(QStringLiteral("y")).toDouble()),
      QSizeF(object.value(QStringLiteral("width")).toDouble(), object.value(QStringLiteral("height")).toDouble()));
}

bool near(const qreal* actual, const QJsonObject& reference, const char* const* keys) {
  for (int i = 0; i < 4; ++i)
    if (std::abs(actual[i] - reference.value(QLatin1String(keys[i])).toDouble()) > kGeometry)
      return false;
  return true;
}

// A rectangle of the native scene against one the fixture recorded relative
// to `origin`, or against no rectangle at all.
void compareRect(QStringList& out, const QRectF& actual, const QJsonValue& expected, const QPointF& origin,
                 const QString& path) {
  if (expected.isNull()) {
    if (!actual.isNull())
      out << QStringLiteral("%1: native %2x%3, expected none")
                 .arg(path, QString::number(actual.width()), QString::number(actual.height()));
    return;
  }
  const QRectF native = actual.translated(-origin);
  const QJsonObject reference = expected.toObject();
  static const char* const keys[4] = {"x", "y", "width", "height"};
  const qreal values[4] = {native.x(), native.y(), native.width(), native.height()};
  for (int i = 0; i < 4; ++i)
    out += parity::compareNumber(values[i], reference.value(QLatin1String(keys[i])).toDouble(), kTier,
                                 path + QLatin1Char('/') + QLatin1String(keys[i]));
}

// A text of the native scene's JSON, which is null where there is no text.
void compareText(QStringList& out, const QJsonValue& actual, const QJsonValue& expected, const QPointF& origin,
                 const QString& path) {
  const QJsonObject native = actual.toObject();
  const QJsonObject reference = expected.toObject();
  const QString nativeText = native.value(QStringLiteral("text")).toString();
  const QString referenceText = reference.value(QStringLiteral("text")).toString();
  if (expected.isNull() ? !nativeText.isEmpty() : nativeText != referenceText)
    out << QStringLiteral("%1/text: native '%2', expected '%3'").arg(path, nativeText, referenceText);
  compareRect(out, native.contains(QStringLiteral("rect")) ? toRect(native.value(QStringLiteral("rect"))) : QRectF(),
              reference.value(QStringLiteral("rect")), origin, path + QStringLiteral("/rect"));
}

// A color as Chrome computes it, rgb(r, g, b) or rgba(r, g, b, a); invalid
// where there is none.
QColor computedColor(const QString& value) {
  static const QRegularExpression rgba(
      QStringLiteral("rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)(?:\\s*,\\s*([\\d.]+))?"));
  const auto match = rgba.match(value);
  if (!match.hasMatch())
    return {};
  QColor color =
      QColor::fromRgb(match.capturedView(1).toInt(), match.capturedView(2).toInt(), match.capturedView(3).toInt());
  if (match.lastCapturedIndex() >= 4 && !match.capturedView(4).isEmpty())
    color.setAlphaF(match.capturedView(4).toDouble());
  return color;
}

// A color with its alpha, unlike compareColor, which compares paint that is
// opaque.
void compareRgba(QStringList& out, const QColor& native, const QJsonValue& expected, const QString& path) {
  const QColor reference = expected.isNull() ? QColor() : computedColor(expected.toString());
  const bool same = reference.isValid() == native.isValid() &&
                    (!reference.isValid() ||
                     (reference.rgb() == native.rgb() && std::abs(reference.alphaF() - native.alphaF()) <= 0.01));
  if (!same)
    out << QStringLiteral("%1: native %2, expected %3")
               .arg(path, native.isValid() ? native.name(QColor::HexArgb) : QStringLiteral("none"),
                    expected.toString());
}

// A paint the native scene resolved: none where it is empty or `none`.
QColor nativeColor(const QString& value) {
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0 ||
      !color::isParsableColor(value))
    return {};
  return color::toQColor(value);
}

// A paint that is a reference to a paint server: the gradient the neo look
// strokes with, the only one erDiagram's stylesheet names. The element it
// names is the document's own, so only the kind of the reference compares.
bool gradientReference(const QString& value) {
  const QString text = value.trimmed();
  return text.startsWith(QLatin1String("url(")) && text.contains(QLatin1String("gradient"));
}

void compareColor(QStringList& out, const QString& native, const QJsonValue& expected, const QString& path) {
  const QColor reference = expected.isNull() ? QColor() : computedColor(expected.toString());
  const QColor actual = nativeColor(native);
  if (gradientReference(native) || gradientReference(expected.toString())) {
    if (!(gradientReference(native) && gradientReference(expected.toString())))
      out << QStringLiteral("%1: native %2, expected %3").arg(path, native, expected.toString());
    return;
  }
  if (reference.isValid() != actual.isValid() || (reference.isValid() && reference.rgb() != actual.rgb()))
    out << QStringLiteral("%1: native %2, expected %3")
               .arg(path, actual.isValid() ? actual.name(QColor::HexRgb) : QStringLiteral("none"),
                    reference.isValid() ? reference.name(QColor::HexRgb) : QStringLiteral("none"));
}

// The lengths of a stroke-dasharray, as Chrome computes them or as the scene
// holds them; empty for "none".
QVector<qreal> dashLengths(const QString& dasharray) {
  static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
  QVector<qreal> lengths;
  for (QString token : dasharray.trimmed().split(separators, Qt::SkipEmptyParts)) {
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
      token.chop(2);
    bool ok = false;
    const qreal length = token.toDouble(&ok);
    if (!ok)
      return {};
    lengths.append(length);
  }
  return lengths;
}

// A stroke-dasharray that paints a solid line: none, or lengths with no gap
// among them, as the "0 0 101 0" the neo look writes along a solid line.
bool isSolid(const QString& dasharray) {
  const QVector<qreal> lengths = dashLengths(dasharray);
  if (lengths.isEmpty())
    return true;
  for (int index = 1; index < lengths.size(); index += 2)
    if (lengths.at(index) > 0.0)
      return false;
  return true;
}

// A dash is compared by its lengths: the length a dash is written along is
// the path's, which the port measures for itself.
void compareDash(QStringList& out, const QString& native, const QJsonValue& expected, const QString& path) {
  const QString reference = expected.isNull() ? QString() : expected.toString();
  if (isSolid(reference)) {
    if (!isSolid(native))
      out << QStringLiteral("%1: native dash '%2', expected a solid line").arg(path, native);
    return;
  }
  const QVector<qreal> mine = dashLengths(native);
  const QVector<qreal> theirs = dashLengths(reference);
  bool same = mine.size() == theirs.size() && !mine.isEmpty();
  for (int index = 0; same && index < mine.size(); ++index)
    same = std::abs(mine.at(index) - theirs.at(index)) <= kGeometry;
  if (!same)
    out << QStringLiteral("%1: native dash '%2', expected %3").arg(path, native, reference);
}

void compareLineStrokeWidth(QStringList& out, qreal native, const QJsonValue& expected, const QString& path) {
  static const QRegularExpression pixels(QStringLiteral("^([\\d.]+)px$"));
  const auto match = pixels.match(expected.toString());
  out += parity::compareNumber(native, match.hasMatch() ? match.capturedView(1).toDouble() : -1.0, kTier, path);
}

// The id Mermaid's document gives a relationship path: the edge id for a path
// between two entities, and for one of the three paths of a loop the entity's
// id and the path's place in the loop.
QString documentId(const QJsonObject& relationship) {
  const int loopIndex = relationship.value(QStringLiteral("loopIndex")).toInt(-1);
  if (loopIndex < 0)
    return relationship.value(QStringLiteral("id")).toString();
  static const QRegularExpression loop(QStringLiteral("^id_(.+)_\\1_[0-9]+$"));
  const auto match = loop.match(relationship.value(QStringLiteral("id")).toString());
  static const char* const parts[3] = {"1", "mid", "2"};
  return match.capturedView(1) + QStringLiteral("-cyclic-special-") + QLatin1String(parts[std::clamp(loopIndex, 0, 2)]);
}

QString cardinality(const QJsonObject& relationship, const QString& key) {
  const QJsonValue value = relationship.value(key);
  return value.isUndefined() || value.isNull() ? QString() : value.toString();
}

// One entity of the native scene's JSON, or the copy drawn behind it,
// against the fixture's: the table about its centre, its rows, its cells, its
// dividers and its paint.
void compareEntity(QStringList& out, const QJsonObject& native, const QJsonObject& reference, const QString& prefix,
                   const QString& expectedId) {
  // erDB numbers the entities as they are declared.
  if (native.value(QStringLiteral("id")).toString() != expectedId)
    out << prefix + QStringLiteral(": native id '%1'").arg(native.value(QStringLiteral("id")).toString());
  const QRectF bounds = toRect(native.value(QStringLiteral("bounds")));
  compareRect(out, bounds, reference.value(QStringLiteral("bounds")), QPointF(), prefix + QStringLiteral("/bounds"));
  // The table's own coordinates are about the centre of its rectangle, which
  // erBox lays it out by. Under the handDrawn look the box it takes is drawn
  // about that centre too, and is not centred on it.
  const QJsonObject referenceCentre = reference.value(QStringLiteral("centre")).toObject();
  const QPointF centre = toRect(native.value(QStringLiteral("centre"))).topLeft();
  out += parity::compareNumber(centre.x(), referenceCentre.value(QStringLiteral("x")).toDouble(), kTier,
                               prefix + QStringLiteral("/centre/x"));
  out += parity::compareNumber(centre.y(), referenceCentre.value(QStringLiteral("y")).toDouble(), kTier,
                               prefix + QStringLiteral("/centre/y"));
  compareText(out, native.value(QStringLiteral("name")), reference.value(QStringLiteral("name")), centre,
              prefix + QStringLiteral("/name"));

  const QJsonArray nativeRows = native.value(QStringLiteral("rows")).toArray();
  const QJsonArray referenceRows = reference.value(QStringLiteral("rows")).toArray();
  if (nativeRows.size() != referenceRows.size())
    out << QStringLiteral("%1: %2 rows, upstream %3").arg(prefix).arg(nativeRows.size()).arg(referenceRows.size());
  for (qsizetype row = 0; row < std::min<qsizetype>(nativeRows.size(), referenceRows.size()); ++row) {
    const QJsonObject nativeRow = nativeRows.at(row).toObject();
    const QJsonObject referenceRow = referenceRows.at(row).toObject();
    const QString rowPrefix = prefix + QStringLiteral("/row/%1").arg(row);
    compareRect(out, toRect(nativeRow.value(QStringLiteral("rect"))), referenceRow.value(QStringLiteral("rect")),
                centre, rowPrefix);
    for (const char* cell : {"type", "name", "keys", "comment"})
      compareText(out, nativeRow.value(QLatin1String(cell)), referenceRow.value(QLatin1String(cell)), centre,
                  rowPrefix + QLatin1Char('/') + QLatin1String(cell));
    // A row's paint is its own: a rule may restyle the even rows alone.
    compareColor(out, nativeRow.value(QStringLiteral("fill")).toString(), referenceRow.value(QStringLiteral("fill")),
                 rowPrefix + QStringLiteral("/fill"));
    compareColor(out, nativeRow.value(QStringLiteral("stroke")).toString(),
                 referenceRow.value(QStringLiteral("stroke")), rowPrefix + QStringLiteral("/stroke"));
    out += parity::compareNumber(nativeRow.value(QStringLiteral("strokeWidth")).toDouble(),
                                 referenceRow.value(QStringLiteral("strokeWidth")).toDouble(), kTier,
                                 rowPrefix + QStringLiteral("/strokeWidth"));
    compareDash(out, nativeRow.value(QStringLiteral("strokeDasharray")).toString(),
                referenceRow.value(QStringLiteral("strokeDasharray")), rowPrefix + QStringLiteral("/strokeDasharray"));
  }

  // erBox draws the line under the name twice; the scene holds a line once.
  const QJsonArray referenceDividers = reference.value(QStringLiteral("dividers")).toArray();
  const QJsonArray nativeDividers = native.value(QStringLiteral("dividers")).toArray();
  static const char* const keys[4] = {"x1", "y1", "x2", "y2"};
  std::vector<bool> drawn(referenceDividers.size(), false);
  qsizetype distinct = 0;
  for (qsizetype i = 0; i < referenceDividers.size(); ++i) {
    const qreal coordinates[4] = {referenceDividers.at(i).toObject().value(QStringLiteral("x1")).toDouble(),
                                  referenceDividers.at(i).toObject().value(QStringLiteral("y1")).toDouble(),
                                  referenceDividers.at(i).toObject().value(QStringLiteral("x2")).toDouble(),
                                  referenceDividers.at(i).toObject().value(QStringLiteral("y2")).toDouble()};
    const bool again =
        std::any_of(referenceDividers.cbegin(), referenceDividers.cbegin() + i,
                    [&](const QJsonValue& earlier) { return near(coordinates, earlier.toObject(), keys); });
    if (!again)
      ++distinct;
  }
  if (nativeDividers.size() != distinct)
    out << QStringLiteral("%1: %2 dividers, upstream %3").arg(prefix).arg(nativeDividers.size()).arg(distinct);
  // Each native line paired with an unused upstream one.
  for (const QJsonValue& value : nativeDividers) {
    const QJsonObject line = value.toObject();
    const qreal coordinates[4] = {line.value(QStringLiteral("x1")).toDouble() - centre.x(),
                                  line.value(QStringLiteral("y1")).toDouble() - centre.y(),
                                  line.value(QStringLiteral("x2")).toDouble() - centre.x(),
                                  line.value(QStringLiteral("y2")).toDouble() - centre.y()};
    qsizetype match = 0;
    while (match < referenceDividers.size() &&
           (drawn.at(match) || !near(coordinates, referenceDividers.at(match).toObject(), keys)))
      ++match;
    if (match == referenceDividers.size())
      out << prefix + QStringLiteral("/divider: native (%1,%2)-(%3,%4) is not drawn upstream")
                          .arg(QString::number(coordinates[0]), QString::number(coordinates[1]),
                               QString::number(coordinates[2]), QString::number(coordinates[3]));
    else
      drawn[match] = true;
  }

  compareColor(out, native.value(QStringLiteral("fill")).toString(), reference.value(QStringLiteral("fill")),
               prefix + QStringLiteral("/fill"));
  compareColor(out, native.value(QStringLiteral("stroke")).toString(), reference.value(QStringLiteral("stroke")),
               prefix + QStringLiteral("/stroke"));
  out += parity::compareNumber(native.value(QStringLiteral("strokeWidth")).toDouble(),
                               reference.value(QStringLiteral("strokeWidth")).toDouble(), kTier,
                               prefix + QStringLiteral("/strokeWidth"));
  compareDash(out, native.value(QStringLiteral("strokeDasharray")).toString(),
              reference.value(QStringLiteral("strokeDasharray")), prefix + QStringLiteral("/strokeDasharray"));
  compareColor(out, native.value(QStringLiteral("textColor")).toString(), reference.value(QStringLiteral("textColor")),
               prefix + QStringLiteral("/textColor"));
}

// The entities, each by its place in the diagram.
void compareEntities(QStringList& out, const QJsonObject& actual, const QJsonObject& expected, const QString& id) {
  const QJsonArray nativeEntities = actual.value(QStringLiteral("entities")).toArray();
  const QJsonArray referenceEntities = expected.value(QStringLiteral("entities")).toArray();
  if (nativeEntities.size() != referenceEntities.size())
    out << QStringLiteral("%1: %2 entities, upstream %3")
               .arg(id)
               .arg(nativeEntities.size())
               .arg(referenceEntities.size());
  for (qsizetype index = 0; index < std::min<qsizetype>(nativeEntities.size(), referenceEntities.size()); ++index) {
    const QJsonObject reference = referenceEntities.at(index).toObject();
    const QString name = reference.value(QStringLiteral("id")).toString();
    compareEntity(out, nativeEntities.at(index).toObject(), reference, id + QStringLiteral("/entity/%1").arg(name),
                  QStringLiteral("entity-%1-%2").arg(name).arg(index));
  }
}

// The copies the handDrawn look draws behind the entities, which the fixture
// records where the document holds them.
void compareBackgrounds(QStringList& out, const QJsonObject& actual, const QJsonObject& expected, const QString& id) {
  const QJsonArray reference = expected.value(QStringLiteral("backgrounds")).toArray();
  const QJsonArray native = actual.value(QStringLiteral("backgrounds")).toArray();
  if (reference.isEmpty()) {
    if (!native.isEmpty())
      out << id + QStringLiteral(": native draws copies behind the entities, upstream none");
    return;
  }
  if (native.size() != reference.size())
    out << QStringLiteral("%1: %2 copies behind the entities, upstream %3")
               .arg(id)
               .arg(native.size())
               .arg(reference.size());
  for (qsizetype index = 0; index < std::min<qsizetype>(native.size(), reference.size()); ++index) {
    const QJsonObject copy = reference.at(index).toObject();
    const QString name = copy.value(QStringLiteral("id")).toString();
    compareEntity(out, native.at(index).toObject(), copy, id + QStringLiteral("/background/%1").arg(name),
                  QStringLiteral("entity-") + name);
  }
}

void compareRelationships(QStringList& out, const QJsonObject& actual, const QJsonObject& expected, const QString& id) {
  const QJsonArray reference = expected.value(QStringLiteral("relationships")).toArray();
  const QJsonArray native = actual.value(QStringLiteral("relationships")).toArray();
  // The marker definitions, by the name of their kind and end.
  const auto markerOf = [](const QJsonArray& markers, const QString& name) {
    for (const QJsonValue& value : markers) {
      const QString id = value.toObject().value(QStringLiteral("id")).toString();
      if (id == name || id.endsWith(QLatin1Char('-') + name))
        return value.toObject();
    }
    return QJsonObject();
  };
  const QJsonArray nativeMarkers = actual.value(QStringLiteral("markers")).toArray();
  for (const QJsonValue& value : nativeMarkers) {
    const QJsonObject mine = value.toObject();
    const QString name = mine.value(QStringLiteral("id")).toString();
    const QJsonObject upstream = markerOf(expected.value(QStringLiteral("markers")).toArray(), name);
    const QString prefix = id + QStringLiteral("/marker/%1").arg(name);
    out += parity::compareNumber(mine.value(QStringLiteral("strokeWidth")).toDouble(),
                                 upstream.value(QStringLiteral("strokeWidth")).toDouble(), kTier,
                                 prefix + QStringLiteral("/strokeWidth"));
    compareColor(out, mine.value(QStringLiteral("stroke")).toString(), upstream.value(QStringLiteral("stroke")),
                 prefix + QStringLiteral("/stroke"));
    compareColor(out, mine.value(QStringLiteral("ringFill")).toString(), upstream.value(QStringLiteral("ringFill")),
                 prefix + QStringLiteral("/ringFill"));
    compareColor(out, mine.value(QStringLiteral("ringStroke")).toString(), upstream.value(QStringLiteral("ringStroke")),
                 prefix + QStringLiteral("/ringStroke"));
  }
  // Upstream names a loop's paths after the entity, so of several
  // relationships from an entity to itself it draws only the last; and the
  // native scene lists a loop's three paths in their place.
  QHash<QString, QJsonObject> nativeById;
  QStringList nativeIds;
  for (const QJsonValue& value : native) {
    const QString key = documentId(value.toObject());
    nativeIds << key;
    nativeById.insert(key, value.toObject());
  }
  for (const QJsonValue& value : reference) {
    const QJsonObject upstream = value.toObject();
    const QString key = upstream.value(QStringLiteral("dataId")).toString();
    const QString prefix = id + QStringLiteral("/relationship/%1").arg(key);
    if (!nativeById.contains(key)) {
      out << prefix + QStringLiteral(": no native path");
      continue;
    }
    nativeIds.removeAll(key);
    const QJsonObject mine = nativeById.value(key);
    if (cardinality(mine, QStringLiteral("cardA")) != cardinality(upstream, QStringLiteral("cardA")))
      out << prefix +
                 QStringLiteral("/cardA: native '%1', expected '%2'")
                     .arg(cardinality(mine, QStringLiteral("cardA")), cardinality(upstream, QStringLiteral("cardA")));
    if (cardinality(mine, QStringLiteral("cardB")) != cardinality(upstream, QStringLiteral("cardB")))
      out << prefix +
                 QStringLiteral("/cardB: native '%1', expected '%2'")
                     .arg(cardinality(mine, QStringLiteral("cardB")), cardinality(upstream, QStringLiteral("cardB")));
    out +=
        parity::comparePath(mine.value(QStringLiteral("path")).toString(),
                            upstream.value(QStringLiteral("path")).toString(), kTier, prefix + QStringLiteral("/path"));
    // The line's paint is its own; the fixture records what Chrome computed.
    compareDash(out, mine.value(QStringLiteral("strokeDasharray")).toString(),
                upstream.value(QStringLiteral("strokeDasharray")), prefix + QStringLiteral("/strokeDasharray"));
    QString nativeWidth = mine.value(QStringLiteral("strokeWidth")).toString();
    if (nativeWidth.endsWith(QLatin1String("px")))
      nativeWidth.chop(2);
    compareLineStrokeWidth(out, nativeWidth.toDouble(), upstream.value(QStringLiteral("strokeWidth")),
                           prefix + QStringLiteral("/strokeWidth"));
    compareColor(out, mine.value(QStringLiteral("stroke")).toString(), upstream.value(QStringLiteral("stroke")),
                 prefix + QStringLiteral("/stroke"));
    const QJsonValue label = upstream.value(QStringLiteral("label"));
    const bool nativeHasLabel = mine.contains(QStringLiteral("label"));
    if (label.isNull() == nativeHasLabel) {
      out << prefix + (label.isNull() ? QStringLiteral(": native label '%1', expected none")
                                            .arg(mine.value(QStringLiteral("label")).toString())
                                      : QStringLiteral(": label '%1' is missing natively")
                                            .arg(label.toObject().value(QStringLiteral("text")).toString()));
    } else if (!label.isNull()) {
      // An SVG label's lines are elements of their own: the text has no
      // space where one ends.
      const QString spaces = QStringLiteral("\\s+");
      if (mine.value(QStringLiteral("label")).toString().remove(QRegularExpression(spaces)) !=
          label.toObject().value(QStringLiteral("text")).toString().remove(QRegularExpression(spaces)))
        out << prefix + QStringLiteral("/label: native '%1', expected '%2'")
                            .arg(mine.value(QStringLiteral("label")).toString(),
                                 label.toObject().value(QStringLiteral("text")).toString());
      compareRect(out, toRect(mine.value(QStringLiteral("labelRect"))), label.toObject().value(QStringLiteral("rect")),
                  QPointF(), prefix + QStringLiteral("/label"));
      compareColor(out, mine.value(QStringLiteral("labelTextColor")).toString(),
                   label.toObject().value(QStringLiteral("textColor")), prefix + QStringLiteral("/label/textColor"));
      compareRgba(out, QColor(mine.value(QStringLiteral("labelBackground")).toString()),
                  label.toObject().value(QStringLiteral("background")), prefix + QStringLiteral("/label/background"));
    }
  }
  for (const QString& extra : nativeIds)
    out << id + QStringLiteral("/relationship/%1: not drawn upstream").arg(extra);
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected ER geometry fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("756e81627b903d27b475382503a06fd8e638cb3cd4ba69a539d8a9c3a6d7b74e"),
          QStringLiteral("ER geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("ER geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("erDiagram.render")),
          QStringLiteral("ER geometry: oracle contract drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("ER geometry: fontMode drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("e3a7d26dbe2d09d77ffcad81134447128f57445d46609d542c6478b1c3acf459"),
          QStringLiteral("ER geometry fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 54, QStringLiteral("ER geometry case count drifted: %1").arg(cases.size()));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    if (kOpenDivergences.contains(id)) {
      ++openCases;
      continue;
    }
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* erScene = dynamic_cast<const er::ErScene*>(entry.scene.get());
    if (!(entry.status == editor::MermaidRenderStatus::Ready && erScene != nullptr)) {
      failures << id + QStringLiteral(": native ER render failed: ") + entry.errorMessage;
      continue;
    }
    const QJsonObject actual = erScene->toJsonObject();
    QStringList divergences;
    compareEntities(divergences, actual, expected, id);
    compareBackgrounds(divergences, actual, expected, id);
    compareRelationships(divergences, actual, expected, id);

    // Canvas: the drawn diagram is as large as upstream's, and the image is
    // the browser's for its SVG element, erRasterExtent's: the viewBox to the
    // nearest pixel, through the cap on its width.
    const QJsonObject canvas = expected.value(QStringLiteral("canvas")).toObject();
    const QJsonArray viewBox = canvas.value(QStringLiteral("viewBox")).toArray();
    const QRectF bounds = erScene->bounds;
    const qreal padding = entry.metadata.diagramPadding;
    divergences += parity::compareNumber(bounds.width(), canvas.value(QStringLiteral("diagramWidth")).toDouble(), kTier,
                                         id + QStringLiteral("/canvas/width"));
    divergences += parity::compareNumber(bounds.height(), canvas.value(QStringLiteral("diagramHeight")).toDouble(),
                                         kTier, id + QStringLiteral("/canvas/height"));
    // The image is naturalSize, which the generic canvas has already padded.
    const QSize raster = entry.naturalSize;
    const QSize rule = er::erRasterExtent(bounds, padding);
    if (raster != rule)
      divergences << QStringLiteral("%1/canvas/size: native %2x%3, the browser's rule gives %4x%5")
                         .arg(id)
                         .arg(raster.width())
                         .arg(raster.height())
                         .arg(rule.width())
                         .arg(rule.height());
    if (viewBox.size() == 4) {
      // The browser's image, from the viewBox it recorded.
      const QRectF drawing(QPointF(),
                           QSizeF(viewBox.at(2).toDouble() - 2 * padding, viewBox.at(3).toDouble() - 2 * padding));
      const QSize browser = er::erRasterExtent(drawing, padding);
      if (raster != browser)
        divergences << QStringLiteral("%1/canvas/viewBox: native %2x%3, browser %4x%5")
                           .arg(id)
                           .arg(raster.width())
                           .arg(raster.height())
                           .arg(browser.width())
                           .arg(browser.height());
    }

    if (!divergences.isEmpty()) {
      std::fprintf(stderr, "[%s]\n", qPrintable(id));
      failures += divergences;
    }
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (openCases != kOpenDivergences.size()) {
    failures << QStringLiteral("%1 of the listed open divergences are not cases of the fixture")
                    .arg(kOpenDivergences.size() - openCases);
  }
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErGeometryOracleTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  if (openCases > 0)
    std::printf("MermaidErGeometryOracleTest: %lld case(s) listed as open divergences\n",
                static_cast<long long>(openCases));
  std::printf("MermaidErGeometryOracleTest passed: %lld cases, every table, path, label and canvas within %0.2f px\n",
              static_cast<long long>(cases.size()), kGeometry);
  return 0;
}
