#pragma once

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/treemap/TreemapScene.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace treemap_test {
using namespace muffin::mermaid;

[[noreturn]] inline void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
inline void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}
inline void near(double actual, double expected, const QString& path, double tolerance = 1e-6) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual, 0, 'g', 17).arg(expected, 0, 'g', 17));
}
inline QVector<double> numbers(const QString& source) {
  static const QRegularExpression expression(QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[-+]?\d+)?)"),
                                             QRegularExpression::CaseInsensitiveOption);
  QVector<double> result;
  auto matches = expression.globalMatch(source);
  while (matches.hasNext())
    result.append(matches.next().captured().toDouble());
  return result;
}
inline QColor computedColor(const QString& source) {
  static const QRegularExpression rgb(QStringLiteral(R"(^rgba?\((\d+),\s*(\d+),\s*(\d+))"));
  const auto match = rgb.match(source);
  return match.hasMatch() ? QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt())
                          : color::toQColor(source);
}
inline void sameColor(const QString& actual, const QString& expected, const QString& path) {
  if (expected == QLatin1String("none") || expected == QLatin1String("transparent")) {
    require(actual.compare(expected, Qt::CaseInsensitive) == 0,
            path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
    return;
  }
  require(computedColor(actual).isValid() && computedColor(expected).isValid() &&
              computedColor(actual).rgb() == computedColor(expected).rgb(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
}
// A section or leaf rectangle in SVG user space. Upstream positions each one
// with its own transform inside one container group that sits below the title,
// so the two transforms compose; the scene holds the composed rectangle.
inline QRectF expectedGlobalRect(const QJsonObject& object, const QPointF& container) {
  const QVector<double> transform =
      numbers(object.value(QStringLiteral("attrs")).toObject().value(QStringLiteral("transform")).toString());
  const QJsonObject attrs = object.value("rect").toObject().value(QStringLiteral("attrs")).toObject();
  return QRectF(transform.value(0) + container.x(), transform.value(1) + container.y(),
                attrs.value(QStringLiteral("width")).toString().toDouble(),
                attrs.value(QStringLiteral("height")).toString().toDouble());
}
// Where upstream positions a label: its own coordinates are relative to the
// rectangle it sits in. Returns the label moved into the container's space.
inline QJsonObject relocateLabel(QJsonObject label, const QRectF& rect) {
  QJsonObject labelAttrs = label.value(QStringLiteral("attrs")).toObject();
  labelAttrs.insert(QStringLiteral("x"),
                    QString::number(rect.x() + labelAttrs.value(QStringLiteral("x")).toString().toDouble()));
  labelAttrs.insert(QStringLiteral("y"),
                    QString::number(rect.y() + labelAttrs.value(QStringLiteral("y")).toString().toDouble()));
  label.insert(QStringLiteral("attrs"), labelAttrs);
  QJsonObject labelBox = label.value(QStringLiteral("bbox")).toObject();
  labelBox.insert(QStringLiteral("x"), labelBox.value(QStringLiteral("x")).toDouble() + rect.x());
  labelBox.insert(QStringLiteral("y"), labelBox.value(QStringLiteral("y")).toDouble() + rect.y());
  label.insert(QStringLiteral("bbox"), labelBox);
  return label;
}
inline void compareText(const treemap::TreemapTextGeometry& actual, const QJsonObject& expected, const QString& path) {
  require(actual.text == expected.value(QStringLiteral("text")).toString(),
          path +
              QStringLiteral("/text [%1] != [%2]").arg(actual.text, expected.value(QStringLiteral("text")).toString()));
  const QJsonObject attrs = expected.value(QStringLiteral("attrs")).toObject();
  near(actual.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(), path + QStringLiteral("/x"));
  near(actual.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(), path + QStringLiteral("/y"));
  const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
  sameColor(actual.fill, computed.value(QStringLiteral("fill")).toString(), path + QStringLiteral("/fill"));
  near(actual.fontSize, computed.value(QStringLiteral("fontSize")).toString().chopped(2).toDouble(),
       path + QStringLiteral("/fontSize"));
  const QJsonObject bbox = expected.value(QStringLiteral("bbox")).toObject();
  if (actual.visible && !actual.bounds.isEmpty()) {
    near(actual.bounds.x(), bbox.value(QStringLiteral("x")).toDouble(), path + QStringLiteral("/bbox/x"), 2.2);
    near(actual.bounds.y(), bbox.value(QStringLiteral("y")).toDouble(), path + QStringLiteral("/bbox/y"), 1.1);
    near(actual.bounds.width(), bbox.value(QStringLiteral("width")).toDouble(), path + QStringLiteral("/bbox/w"), 3.2);
    near(actual.bounds.height(), bbox.value(QStringLiteral("height")).toDouble(), path + QStringLiteral("/bbox/h"),
         1.1);
  }
}
inline void compareCase(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready, id + QStringLiteral(": ") + entry.errorMessage);
  const auto scene = std::dynamic_pointer_cast<const treemap::TreemapScene>(entry.scene);
  require(bool(scene), id + QStringLiteral("/scene"));
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject attrs = root.value(QStringLiteral("attrs")).toObject();
  const QVector<double> viewBox = numbers(attrs.value(QStringLiteral("viewBox")).toString());
  require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
  // Upstream groups every section and leaf under one container; a titled diagram
  // moves that container down by the height of the title.
  const QVector<double> container =
      numbers(expected.value(QStringLiteral("container")).toObject().value(QStringLiteral("transform")).toString());
  require(container.size() == 2, id + QStringLiteral("/container"));
  const QPointF containerOffset(container.at(0), container.at(1));
  near(scene->bounds.x(), viewBox.at(0), id + QStringLiteral("/vb/x"), .02);
  near(scene->bounds.y(), viewBox.at(1), id + QStringLiteral("/vb/y"), .02);
  near(scene->bounds.width(), viewBox.at(2), id + QStringLiteral("/vb/w"), .03);
  near(scene->bounds.height(), viewBox.at(3), id + QStringLiteral("/vb/h"), .03);
  require(scene->useMaxWidth == (attrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
          id + QStringLiteral("/useMaxWidth"));

  const QJsonValue titleValue = expected.value(QStringLiteral("title"));
  if (titleValue.isNull())
    require(scene->title.text.isEmpty(), id + QStringLiteral("/title/absent"));
  else
    compareText(scene->title, titleValue.toObject(), id + QStringLiteral("/title"));

  const QJsonArray sections = expected.value(QStringLiteral("sections")).toArray();
  require(scene->sections.size() == sections.size(), id + QStringLiteral("/sections"));
  for (qsizetype i = 0; i < sections.size(); ++i) {
    const auto& actual = scene->sections.at(i);
    const QJsonObject object = sections.at(i).toObject();
    const QRectF rect = expectedGlobalRect(object, containerOffset);
    near(actual.rect.x(), rect.x(), id + QStringLiteral("/section/%1/x").arg(i));
    near(actual.rect.y(), rect.y(), id + QStringLiteral("/section/%1/y").arg(i));
    near(actual.rect.width(), rect.width(), id + QStringLiteral("/section/%1/w").arg(i));
    near(actual.rect.height(), rect.height(), id + QStringLiteral("/section/%1/h").arg(i));
    const QJsonObject rectObject = object.value(QStringLiteral("rect")).toObject();
    sameColor(actual.fill,
              rectObject.value(QStringLiteral("computed")).toObject().value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/section/%1/fill").arg(i));
    if (actual.depth > 0) {
      compareText(actual.label, relocateLabel(object.value(QStringLiteral("label")).toObject(), rect),
                  id + QStringLiteral("/section/%1/label").arg(i));
    }
  }

  const QJsonArray leaves = expected.value(QStringLiteral("leaves")).toArray();
  require(scene->leaves.size() == leaves.size(), id + QStringLiteral("/leaves"));
  for (qsizetype i = 0; i < leaves.size(); ++i) {
    const auto& actual = scene->leaves.at(i);
    const QJsonObject object = leaves.at(i).toObject();
    const QRectF rect = expectedGlobalRect(object, containerOffset);
    near(actual.rect.x(), rect.x(), id + QStringLiteral("/leaf/%1/x").arg(i));
    near(actual.rect.y(), rect.y(), id + QStringLiteral("/leaf/%1/y").arg(i));
    near(actual.rect.width(), rect.width(), id + QStringLiteral("/leaf/%1/w").arg(i));
    near(actual.rect.height(), rect.height(), id + QStringLiteral("/leaf/%1/h").arg(i));
    const QJsonObject rectObject = object.value(QStringLiteral("rect")).toObject();
    sameColor(actual.fill,
              rectObject.value(QStringLiteral("computed")).toObject().value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/leaf/%1/fill").arg(i));
    const QJsonObject labelExpected = object.value(QStringLiteral("label")).toObject();
    const bool labelVisible =
        labelExpected.value(QStringLiteral("computed")).toObject().value(QStringLiteral("display")).toString() !=
        QLatin1String("none");
    require(actual.label.visible == labelVisible, id + QStringLiteral("/leaf/%1/label-visible").arg(i));
    require(actual.label.text == labelExpected.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/leaf/%1/label-text").arg(i));
    near(actual.label.fontSize,
         labelExpected.value(QStringLiteral("computed"))
             .toObject()
             .value(QStringLiteral("fontSize"))
             .toString()
             .chopped(2)
             .toDouble(),
         id + QStringLiteral("/leaf/%1/label-size").arg(i));
    const QJsonValue valueExpected = object.value(QStringLiteral("value"));
    if (valueExpected.isNull())
      require(actual.value.text.isEmpty(), id + QStringLiteral("/leaf/value-absent"));
    else {
      require(actual.value.text == valueExpected.toObject().value(QStringLiteral("text")).toString(),
              id + QStringLiteral("/leaf/%1/value-text").arg(i));
      near(actual.value.fontSize,
           valueExpected.toObject()
               .value(QStringLiteral("computed"))
               .toObject()
               .value(QStringLiteral("fontSize"))
               .toString()
               .chopped(2)
               .toDouble(),
           id + QStringLiteral("/leaf/%1/value-size").arg(i));
    }
  }
}

} // namespace treemap_test
