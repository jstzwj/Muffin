#pragma once

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace sankey_test {
using namespace muffin::mermaid;

[[noreturn]] inline void fail(const QString &message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
inline void require(bool condition, const QString &message) {
  if (!condition)
    fail(message);
}
inline void near(double actual, double expected, const QString &path,
                 double tolerance = 1e-8) {
  require(
      (std::isnan(actual) && std::isnan(expected)) ||
          (std::isfinite(actual) && std::fabs(actual - expected) <= tolerance),
      QStringLiteral("%1: %2 != %3")
          .arg(path)
          .arg(actual, 0, 'g', 17)
          .arg(expected, 0, 'g', 17));
}
inline QVector<double> numbers(const QString &source) {
  static const QRegularExpression expression(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[-+]?\d+)?)"),
      QRegularExpression::CaseInsensitiveOption);
  QVector<double> result;
  auto matches = expression.globalMatch(source);
  while (matches.hasNext())
    result.append(matches.next().captured().toDouble());
  return result;
}
inline QColor computedColor(const QString &source) {
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\((\d+),\s*(\d+),\s*(\d+))"));
  const auto match = rgb.match(source);
  return match.hasMatch()
             ? QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                      match.captured(3).toInt())
             : color::toQColor(source);
}
inline void sameColor(const QString &actual, const QString &expected,
                      const QString &path) {
  require(
      computedColor(actual).isValid() && computedColor(expected).isValid() &&
          computedColor(actual).rgb() == computedColor(expected).rgb(),
      path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
}
inline void compareCase(const QJsonObject &fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  editor::MermaidRenderCache cache;
  const auto entry =
      cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          id + QStringLiteral(": ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const sankey::SankeyScene>(entry.scene);
  require(bool(scene), id + QStringLiteral("/scene"));
  const QJsonObject expected =
      fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject attrs = expected.value(QStringLiteral("root"))
                                .toObject()
                                .value(QStringLiteral("attrs"))
                                .toObject();
  const QVector<double> viewBox =
      numbers(attrs.value(QStringLiteral("viewBox")).toString());
  near(scene->bounds.x(), viewBox.at(0), id + QStringLiteral("/vb/x"), 1e-6);
  near(scene->bounds.y(), viewBox.at(1), id + QStringLiteral("/vb/y"), 1e-6);
  near(scene->bounds.width(), viewBox.at(2), id + QStringLiteral("/vb/w"), .05);
  near(scene->bounds.height(), viewBox.at(3), id + QStringLiteral("/vb/h"),
       .05);
  require(scene->useMaxWidth ==
              (attrs.value(QStringLiteral("width")).toString() ==
               QLatin1String("100%")),
          id + QStringLiteral("/useMaxWidth"));

  const QJsonArray nodes = expected.value(QStringLiteral("nodes")).toArray();
  require(scene->nodes.size() == nodes.size(), id + QStringLiteral("/nodes"));
  for (qsizetype i = 0; i < nodes.size(); ++i) {
    const auto &actual = scene->nodes.at(i);
    const QJsonObject object = nodes.at(i).toObject();
    const QJsonObject groupAttrs =
        object.value(QStringLiteral("attrs")).toObject();
    const QJsonObject rectAttrs = object.value(QStringLiteral("rect"))
                                      .toObject()
                                      .value(QStringLiteral("attrs"))
                                      .toObject();
    near(actual.x0, groupAttrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/node/x"));
    near(actual.y0, groupAttrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/node/y"));
    near(actual.x1 - actual.x0,
         rectAttrs.value(QStringLiteral("width")).toString().toDouble(),
         id + QStringLiteral("/node/w"));
    near(actual.y1 - actual.y0,
         rectAttrs.value(QStringLiteral("height")).toString().toDouble(),
         id + QStringLiteral("/node/h"));
    sameColor(actual.color, rectAttrs.value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/node/fill"));
  }

  const QJsonArray links = expected.value(QStringLiteral("links")).toArray();
  require(scene->links.size() == links.size(), id + QStringLiteral("/links"));
  for (qsizetype i = 0; i < links.size(); ++i) {
    const auto &actual = scene->links.at(i);
    const QJsonObject pathAttrs = links.at(i)
                                      .toObject()
                                      .value(QStringLiteral("path"))
                                      .toObject()
                                      .value(QStringLiteral("attrs"))
                                      .toObject();
    const QVector<double> left = numbers(actual.pathData);
    const QVector<double> right =
        numbers(pathAttrs.value(QStringLiteral("d")).toString());
    require(left.size() == right.size(), id + QStringLiteral("/path/count"));
    for (qsizetype k = 0; k < left.size(); ++k)
      near(left.at(k), right.at(k), id + QStringLiteral("/path/%1").arg(k),
           1e-7);
    near(actual.width,
         pathAttrs.value(QStringLiteral("stroke-width")).toString().toDouble(),
         id + QStringLiteral("/link/width"), 1e-7);
  }

  const QJsonArray labels = expected.value(QStringLiteral("labels")).toArray();
  require(scene->labels.size() == labels.size(),
          id + QStringLiteral("/labels"));
  for (qsizetype i = 0; i < labels.size(); ++i) {
    const auto &actual = scene->labels.at(i);
    const QJsonObject object = labels.at(i).toObject();
    const QJsonObject labelAttrs =
        object.value(QStringLiteral("attrs")).toObject();
    require(actual.text == object.value(QStringLiteral("text")).toString(),
            id + QStringLiteral("/label/text"));
    near(actual.position.x(),
         labelAttrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/label/x"));
    near(actual.position.y(),
         labelAttrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/label/y"));
    require(actual.anchor ==
                labelAttrs.value(QStringLiteral("text-anchor")).toString(),
            id + QStringLiteral("/label/anchor"));
  }
}

} // namespace sankey_test
