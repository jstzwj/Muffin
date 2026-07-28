#pragma once

// ParityDiff — shared semantic-comparison vocabulary for mermaid parity /
// regression oracles. Header-only so it adds nothing to MuffinCore and is
// instantiated only by test TUs (resolved via MuffinCore's PUBLIC src/ include).
//
// Design rule: every comparator RETURNS the mismatch list (QStringList) instead
// of aborting, so a caller sees all diffs in one run and the helpers stay
// composable and unit-testable. Mirror the tolerance tiers already established
// in the per-family oracle tests (MermaidFlowchartLayoutTest.cpp / the class /
// state / sequence layout oracles):
//   kPathCoord   0.002  flowchart path coordinates (serialized at 0.001)
//   kCoordinate  0.01   class / state / sequence / er layout coordinates
//   kMeasurement 0.2    label box sizes
//
// comparePath is lifted from MermaidFlowchartLayoutTest::requirePathNear
// (numbers normalized to '#' for a structural equality check, then each
// coordinate compared numerically). compareJson is the net-new recursive
// walker used by scene-dump oracles.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::parity {

struct Tier {
  qreal value;
};

inline constexpr Tier kPathCoord{0.002};
inline constexpr Tier kCoordinate{0.01};
inline constexpr Tier kMeasurement{0.2};

// Empty list on match, else { "path: native X, expected Y" }.
inline QStringList compareNumber(qreal actual, qreal expected, Tier tol, const QString& path) {
  if (std::abs(actual - expected) <= tol.value + 1e-9) return {};
  return {QStringLiteral("%1: native %2, expected %3")
              .arg(path).arg(actual, 0, 'f', 3).arg(expected, 0, 'f', 3)};
}

// SVG path `d` compare: normalize numbers to '#' for a structural equality
// check, then compare each coordinate within `tol`. Modelled on
// MermaidFlowchartLayoutTest.cpp requirePathNear.
inline QStringList comparePath(const QString& actual, const QString& expected, Tier tol,
                               const QString& path) {
  static const QRegularExpression number(QStringLiteral("-?\\d+(?:\\.\\d+)?(?:e[-+]?\\d+)?"));
  QStringList errs;
  const QString actualSkeleton = QString(actual).replace(number, QStringLiteral("#"));
  const QString expectedSkeleton = QString(expected).replace(number, QStringLiteral("#"));
  if (actualSkeleton != expectedSkeleton) {
    errs << QStringLiteral("%1: path structure differs (numbers normalized to '#')").arg(path);
    return errs;
  }
  auto collect = [](const QString& s) {
    QVector<double> values;
    auto it = number.globalMatch(s);
    while (it.hasNext()) values.append(it.next().captured(0).toDouble());
    return values;
  };
  const QVector<double> actualNumbers = collect(actual);
  const QVector<double> expectedNumbers = collect(expected);
  for (int i = 0; i < actualNumbers.size() && i < expectedNumbers.size(); ++i) {
    errs += compareNumber(actualNumbers[i], expectedNumbers[i], tol,
                          QStringLiteral("%1 coord %2").arg(path).arg(i));
  }
  return errs;
}

inline QString jsonTypeName(const QJsonValue& value) {
  switch (value.type()) {
    case QJsonValue::Null:   return QStringLiteral("null");
    case QJsonValue::Bool:   return QStringLiteral("bool");
    case QJsonValue::Double: return QStringLiteral("number");
    case QJsonValue::String: return QStringLiteral("string");
    case QJsonValue::Array:  return QStringLiteral("array");
    case QJsonValue::Object: return QStringLiteral("object");
  }
  return QStringLiteral("undefined");
}

// Recursive JSON walker: object key sets must match (order-independent),
// array lengths exact, numbers via compareNumber, everything else exact.
// `ignoreKeys` skips volatile fields (e.g. "handDrawnSeed"). `pathValueKeys`
// names string-valued fields that hold SVG path `d` data (floating-point
// coordinates subject to run-to-run raster/order jitter); those are compared
// via comparePath (structural equality + numeric tolerance) instead of exact
// string equality.
inline QStringList compareJson(const QJsonValue& actual, const QJsonValue& expected, Tier tol,
                               const QString& path, const QSet<QString>& ignoreKeys = {},
                               const QSet<QString>& pathValueKeys = {}) {
  QStringList errs;
  if (actual.type() != expected.type()) {
    errs << QStringLiteral("%1: type mismatch (native %2, expected %3)")
                .arg(path).arg(jsonTypeName(actual)).arg(jsonTypeName(expected));
    return errs;
  }
  if (actual.isObject()) {
    const QJsonObject actualObject = actual.toObject();
    const QJsonObject expectedObject = expected.toObject();
    const QStringList actualKeys = actualObject.keys();
    // Call keys() ONCE: begin()/end() must come from the same container, else
    // the iterator-range constructor walks across two distinct temporaries (UB).
    const QStringList expectedKeys = expectedObject.keys();
    const QSet<QString> expectedSet(expectedKeys.cbegin(), expectedKeys.cend());
    for (const QString& key : actualKeys) {
      if (ignoreKeys.contains(key)) continue;
      if (!expectedSet.contains(key))
        errs << QStringLiteral("%1: unexpected key '%2'").arg(path).arg(key);
    }
    for (const QString& key : expectedKeys) {
      if (ignoreKeys.contains(key)) continue;
      if (!actualObject.contains(key))
        errs << QStringLiteral("%1: missing key '%2'").arg(path).arg(key);
    }
    for (const QString& key : actualKeys) {
      if (ignoreKeys.contains(key) || !expectedObject.contains(key)) continue;
      const QString childPath = QStringLiteral("%1/%2").arg(path).arg(key);
      const QJsonValue actualChild = actualObject.value(key);
      const QJsonValue expectedChild = expectedObject.value(key);
      if (pathValueKeys.contains(key) && actualChild.isString())
        errs += comparePath(actualChild.toString(), expectedChild.toString(), tol, childPath);
      else
        errs += compareJson(actualChild, expectedChild, tol, childPath, ignoreKeys, pathValueKeys);
    }
    return errs;
  }
  if (actual.isArray()) {
    const QJsonArray actualArray = actual.toArray();
    const QJsonArray expectedArray = expected.toArray();
    if (actualArray.size() != expectedArray.size()) {
      errs << QStringLiteral("%1: array length native %2, expected %3")
                  .arg(path).arg(actualArray.size()).arg(expectedArray.size());
      return errs;
    }
    for (int i = 0; i < actualArray.size(); ++i) {
      errs += compareJson(actualArray.at(i), expectedArray.at(i), tol,
                          QStringLiteral("%1[%2]").arg(path).arg(i), ignoreKeys, pathValueKeys);
    }
    return errs;
  }
  if (actual.isDouble()) return compareNumber(actual.toDouble(), expected.toDouble(), tol, path);
  if (actual == expected) return {};
  errs << QStringLiteral("%1: native '%2', expected '%3'")
              .arg(path).arg(actual.toVariant().toString()).arg(expected.toVariant().toString());
  return errs;
}

}  // namespace muffin::mermaid::parity
