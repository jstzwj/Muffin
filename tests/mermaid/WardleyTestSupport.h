#pragma once

#include "mermaid/wardley/WardleyScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace wardley_test {

using namespace muffin::mermaid;

inline void ensureWardleyFonts() {
  static const QStringList files = {
      QStringLiteral("C:/Windows/Fonts/times.ttf"),
      QStringLiteral("C:/Windows/Fonts/timesbd.ttf"),
      QStringLiteral("C:/Windows/Fonts/timesi.ttf"),
      QStringLiteral("C:/Windows/Fonts/timesbi.ttf")};
  for (const QString& file : files)
    if (QFileInfo::exists(file)) QFontDatabase::addApplicationFont(file);
}

[[noreturn]] inline void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

inline void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

inline void near(qreal actual, qreal expected, const QString& path,
                 qreal tolerance = 1e-6) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3")
              .arg(path)
              .arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}

inline qreal numberAttribute(const QJsonObject& attrs, const QString& key,
                             qreal fallback = 0.0) {
  const QJsonValue value = attrs.value(key);
  return value.isNull() || value.isUndefined() ? fallback
                                                : value.toString().toDouble();
}

inline qreal px(QString value) {
  value = value.trimmed();
  if (value.endsWith(QLatin1String("px"))) value.chop(2);
  return value.toDouble();
}

inline QVector<qreal> numbers(const QString& source) {
  static const QRegularExpression expression(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[-+]?\d+)?)"),
      QRegularExpression::CaseInsensitiveOption);
  QVector<qreal> result;
  auto matches = expression.globalMatch(source);
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}

inline QColor browserColor(QString source) {
  source = source.trimmed();
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?)"));
  const auto match = rgb.match(source);
  if (match.hasMatch()) {
    QColor value(match.captured(1).toInt(), match.captured(2).toInt(),
                 match.captured(3).toInt());
    if (!match.captured(4).isEmpty()) value.setAlphaF(match.captured(4).toDouble());
    return value;
  }
  return color::toQColor(source);
}

inline void samePaint(const QString& actual, const QString& expected,
                      const QString& path) {
  if (expected.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
    require(actual.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0,
            path + QStringLiteral(": ") + actual + QStringLiteral(" != none"));
    return;
  }
  const QColor lhs = browserColor(actual);
  const QColor rhs = browserColor(expected);
  require(lhs.isValid() && rhs.isValid() && lhs.rgba() == rhs.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") +
              expected);
}

struct Rendered {
  std::shared_ptr<const wardley::WardleyScene> scene;
  editor::MermaidRenderEntry entry;
};

inline Rendered render(const QString& source) {
  editor::MermaidRenderCache cache;
  Rendered result;
  result.entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(result.entry.status == editor::MermaidRenderStatus::Ready &&
              result.entry.scene,
          QStringLiteral("Wardley render failed: ") + result.entry.errorMessage);
  result.scene =
      std::dynamic_pointer_cast<const wardley::WardleyScene>(result.entry.scene);
  require(bool(result.scene), QStringLiteral("Wardley entry has wrong scene type"));
  return result;
}

inline QString tag(wardley::WardleyPrimitiveType type) {
  switch (type) {
    case wardley::WardleyPrimitiveType::Rect: return QStringLiteral("rect");
    case wardley::WardleyPrimitiveType::Line: return QStringLiteral("line");
    case wardley::WardleyPrimitiveType::Circle: return QStringLiteral("circle");
    case wardley::WardleyPrimitiveType::Path: return QStringLiteral("path");
    case wardley::WardleyPrimitiveType::Text: return QStringLiteral("text");
  }
  return {};
}

inline void comparePrimitive(const wardley::WardleyPrimitive& actual,
                             const QJsonObject& oracle,
                             const QString& path) {
  const QString expectedTag = oracle.value(QStringLiteral("tag")).toString();
  require(tag(actual.type) == expectedTag, path + QStringLiteral("/tag"));
  require(actual.parentClass ==
              oracle.value(QStringLiteral("parentClass")).toString(),
          path + QStringLiteral("/parent"));
  const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
  require(actual.role == attrs.value(QStringLiteral("class")).toString(),
          path + QStringLiteral("/class [") + actual.role + QStringLiteral("]"));
  const QJsonObject computed = oracle.value(QStringLiteral("computed")).toObject();
  near(actual.opacity, computed.value(QStringLiteral("opacity")).toString().toDouble(),
       path + QStringLiteral("/opacity"));

  if (actual.type == wardley::WardleyPrimitiveType::Text) {
    require(actual.text == oracle.value(QStringLiteral("text")).toString(),
            path + QStringLiteral("/text"));
    near(actual.position.x(), numberAttribute(attrs, QStringLiteral("x")), path + "/x");
    near(actual.position.y(), numberAttribute(attrs, QStringLiteral("y")), path + "/y");
    near(actual.fontSize, px(computed.value(QStringLiteral("fontSize")).toString()),
         path + QStringLiteral("/fontSize"), .001);
    require(actual.bold ==
                (computed.value(QStringLiteral("fontWeight")).toString().toInt() >= 600),
            path + QStringLiteral("/bold"));
    require(actual.anchor == computed.value(QStringLiteral("textAnchor")).toString(),
            path + QStringLiteral("/anchor"));
    samePaint(actual.fill, computed.value(QStringLiteral("fill")).toString(),
              path + QStringLiteral("/fill"));
    const QString transform = attrs.value(QStringLiteral("transform")).toString();
    const QVector<qreal> transformNumbers = numbers(transform);
    const qreal expectedRotation = transform.startsWith(QLatin1String("rotate"))
                                       ? transformNumbers.value(0)
                                       : 0.0;
    near(actual.rotation, expectedRotation, path + QStringLiteral("/rotation"));
    const QJsonObject box = oracle.value(QStringLiteral("bbox")).toObject();
    if (!box.isEmpty() && !actual.bounds.isEmpty()) {
      near(actual.bounds.x(), box.value(QStringLiteral("x")).toDouble(), path + "/bbox/x", 2.0);
      near(actual.bounds.y(), box.value(QStringLiteral("y")).toDouble(), path + "/bbox/y", 2.0);
      near(actual.bounds.width(), box.value(QStringLiteral("width")).toDouble(), path + "/bbox/w", 2.0);
      near(actual.bounds.height(), box.value(QStringLiteral("height")).toDouble(), path + "/bbox/h", 2.0);
    }
    return;
  }

  if (actual.type == wardley::WardleyPrimitiveType::Rect) {
    near(actual.rect.x(), numberAttribute(attrs, QStringLiteral("x")), path + "/x");
    near(actual.rect.y(), numberAttribute(attrs, QStringLiteral("y")), path + "/y");
    near(actual.rect.width(), numberAttribute(attrs, QStringLiteral("width")), path + "/w");
    near(actual.rect.height(), numberAttribute(attrs, QStringLiteral("height")), path + "/h");
    near(actual.rx, numberAttribute(attrs, QStringLiteral("rx")), path + "/rx");
  } else if (actual.type == wardley::WardleyPrimitiveType::Line) {
    near(actual.line.x1(), numberAttribute(attrs, QStringLiteral("x1")), path + "/x1");
    near(actual.line.y1(), numberAttribute(attrs, QStringLiteral("y1")), path + "/y1");
    near(actual.line.x2(), numberAttribute(attrs, QStringLiteral("x2")), path + "/x2");
    near(actual.line.y2(), numberAttribute(attrs, QStringLiteral("y2")), path + "/y2");
  } else if (actual.type == wardley::WardleyPrimitiveType::Circle) {
    near(actual.center.x(), numberAttribute(attrs, QStringLiteral("cx")), path + "/cx");
    near(actual.center.y(), numberAttribute(attrs, QStringLiteral("cy")), path + "/cy");
    near(actual.radius, numberAttribute(attrs, QStringLiteral("r")), path + "/r");
  } else {
    const QVector<qreal> lhs = numbers(actual.pathData);
    const QVector<qreal> rhs = numbers(attrs.value(QStringLiteral("d")).toString());
    require(lhs.size() == rhs.size(), path + QStringLiteral("/path/number-count"));
    for (qsizetype i = 0; i < lhs.size(); ++i)
      near(lhs.at(i), rhs.at(i), path + QStringLiteral("/path/%1").arg(i));
  }

  if (expectedTag == QLatin1String("rect") || expectedTag == QLatin1String("circle") ||
      expectedTag == QLatin1String("path"))
    samePaint(actual.fill, computed.value(QStringLiteral("fill")).toString(),
              path + QStringLiteral("/fill"));
  samePaint(actual.stroke, computed.value(QStringLiteral("stroke")).toString(),
            path + QStringLiteral("/stroke"));
  const bool visibleStroke =
      computed.value(QStringLiteral("stroke")).toString().compare(
          QLatin1String("none"), Qt::CaseInsensitive) != 0;
  if (visibleStroke)
    near(actual.strokeWidth,
         px(computed.value(QStringLiteral("strokeWidth")).toString()),
         path + QStringLiteral("/strokeWidth"), .001);
  const QVector<qreal> dash = numbers(computed.value(QStringLiteral("strokeDasharray")).toString());
  require(actual.dash.size() == dash.size(), path + QStringLiteral("/dash-count"));
  for (qsizetype i = 0; i < dash.size(); ++i)
    near(actual.dash.at(i), dash.at(i), path + QStringLiteral("/dash/%1").arg(i));
  require(actual.markerStart == !attrs.value(QStringLiteral("marker-start")).isNull(),
          path + QStringLiteral("/markerStart"));
  require(actual.markerEnd == !attrs.value(QStringLiteral("marker-end")).isNull(),
          path + QStringLiteral("/markerEnd"));
}

inline void compareCase(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const Rendered rendered = render(fixture.value(QStringLiteral("source")).toString());
  const wardley::WardleyScene& scene = *rendered.scene;
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject attrs = root.value(QStringLiteral("attrs")).toObject();
  require(scene.viewBoxAttribute == attrs.value(QStringLiteral("viewBox")).toString(),
          id + QStringLiteral("/viewBox [") + scene.viewBoxAttribute + QStringLiteral("]"));
  near(scene.bounds.width(), root.value(QStringLiteral("client")).toObject()
                                 .value(QStringLiteral("width")).toDouble(),
       id + QStringLiteral("/client/w"), .001);
  near(scene.bounds.height(), root.value(QStringLiteral("client")).toObject()
                                  .value(QStringLiteral("height")).toDouble(),
       id + QStringLiteral("/client/h"), .001);
  const bool expectedMax = attrs.value(QStringLiteral("width")).toString() ==
                           QLatin1String("100%");
  require(scene.useMaxWidth == expectedMax &&
              rendered.entry.metadata.svgUseMaxWidth == expectedMax,
          id + QStringLiteral("/useMaxWidth"));
  const QJsonArray primitives = expected.value(QStringLiteral("primitives")).toArray();
  require(scene.primitives.size() == primitives.size(),
          id + QStringLiteral("/primitive-count %1 != %2")
                   .arg(scene.primitives.size())
                   .arg(primitives.size()));
  for (qsizetype i = 0; i < primitives.size(); ++i)
    comparePrimitive(scene.primitives.at(i), primitives.at(i).toObject(),
                     id + QStringLiteral("/primitive/%1").arg(i));
}

}  // namespace wardley_test
