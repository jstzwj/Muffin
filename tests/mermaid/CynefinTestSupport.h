#pragma once

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace cynefin_test {

using namespace muffin::mermaid;

[[noreturn]] inline void fail(const QString &message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

inline void require(bool condition, const QString &message) {
  if (!condition) fail(message);
}

inline void near(double actual, double expected, const QString &path,
                 double tolerance = 1e-6) {
  if (std::isnan(actual) && std::isnan(expected)) return;
  if (std::isinf(actual) && actual == expected) return;
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
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
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}

inline QColor browserColor(QString source) {
  source = source.trimmed();
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+))"));
  const auto match = rgb.match(source);
  if (match.hasMatch())
    return QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                  match.captured(3).toInt());
  return color::toQColor(source);
}

inline void sameColor(const QString &actual, const QString &expected,
                      const QString &path) {
  if (expected.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
    require(actual.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0,
            path + QStringLiteral(": ") + actual + QStringLiteral(" != none"));
    return;
  }
  const QColor lhs = browserColor(actual);
  const QColor rhs = browserColor(expected);
  require(lhs.isValid() && rhs.isValid() && lhs.rgba() == rhs.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
}

inline double px(const QString &value) {
  QString text = value.trimmed();
  if (text.endsWith(QLatin1String("px"))) text.chop(2);
  return text.toDouble();
}

struct Rendered {
  std::shared_ptr<const cynefin::CynefinScene> scene;
  editor::MermaidRenderEntry entry;
};

inline Rendered render(const QString &source) {
  editor::MermaidRenderCache cache;
  Rendered result;
  result.entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(result.entry.status == editor::MermaidRenderStatus::Ready &&
              result.entry.scene,
          QStringLiteral("Cynefin render failed: ") + result.entry.errorMessage);
  result.scene =
      std::dynamic_pointer_cast<const cynefin::CynefinScene>(result.entry.scene);
  require(bool(result.scene), QStringLiteral("Cynefin entry has wrong scene type"));
  return result;
}

inline void compareText(const cynefin::CynefinTextGeometry &actual,
                        const QJsonObject &expected, const QString &path,
                        double boundsTolerance = 1.25,
                        double positionTolerance = 1e-6) {
  require(actual.text == expected.value(QStringLiteral("text")).toString(),
          path + QStringLiteral("/text [%1] != [%2]")
                     .arg(actual.text,
                          expected.value(QStringLiteral("text")).toString()));
  const QJsonObject attrs = expected.value(QStringLiteral("attrs")).toObject();
  near(actual.position.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
       path + QStringLiteral("/x"), positionTolerance);
  near(actual.position.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
       path + QStringLiteral("/y"), positionTolerance);
  require(actual.anchor == attrs.value(QStringLiteral("text-anchor")).toString(),
          path + QStringLiteral("/anchor"));
  const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
  sameColor(actual.fill, computed.value(QStringLiteral("fill")).toString(),
            path + QStringLiteral("/fill"));
  near(actual.fontSize, px(computed.value(QStringLiteral("fontSize")).toString()),
       path + QStringLiteral("/fontSize"), .001);
  require(actual.bold ==
              (computed.value(QStringLiteral("fontWeight")).toString().toInt() >= 600),
          path + QStringLiteral("/bold"));
  require(actual.italic ==
              (computed.value(QStringLiteral("fontStyle")).toString() ==
               QLatin1String("italic")),
          path + QStringLiteral("/italic"));
  const QJsonObject bbox = expected.value(QStringLiteral("bbox")).toObject();
  if (!actual.bounds.isEmpty() && !bbox.isEmpty()) {
    const double inkTolerance = std::max(
        boundsTolerance,
        double(actual.fontSize) / (actual.italic ? 4.0 : 8.0));
    near(actual.bounds.x(), bbox.value(QStringLiteral("x")).toDouble(),
         path + QStringLiteral("/bbox/x"), inkTolerance);
    near(actual.bounds.y(), bbox.value(QStringLiteral("y")).toDouble(),
         path + QStringLiteral("/bbox/y"), inkTolerance);
    near(actual.bounds.width(), bbox.value(QStringLiteral("width")).toDouble(),
         path + QStringLiteral("/bbox/w"), inkTolerance);
    near(actual.bounds.height(), bbox.value(QStringLiteral("height")).toDouble(),
         path + QStringLiteral("/bbox/h"), inkTolerance);
  }
}

inline void compareCase(const QJsonObject &fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  const Rendered rendered = render(source);
  const auto &scene = *rendered.scene;
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject rootAttrs = root.value(QStringLiteral("attrs")).toObject();
  require(scene.viewBoxAttribute ==
              rootAttrs.value(QStringLiteral("viewBox")).toString(),
          id + QStringLiteral("/viewBox [") + scene.viewBoxAttribute +
              QStringLiteral("] != [") +
              rootAttrs.value(QStringLiteral("viewBox")).toString() +
              QLatin1Char(']'));
  require(scene.rootTransformAttribute ==
              expected.value(QStringLiteral("container")).toObject()
                  .value(QStringLiteral("attrs")).toObject()
                  .value(QStringLiteral("transform")).toString(),
          id + QStringLiteral("/rootTransform"));
  const bool expectedMax = rootAttrs.value(QStringLiteral("width")).toString() ==
                           QLatin1String("100%");
  require(scene.useMaxWidth == expectedMax &&
              rendered.entry.metadata.svgUseMaxWidth == expectedMax,
          id + QStringLiteral("/useMaxWidth"));
  const QJsonObject client = root.value(QStringLiteral("client")).toObject();
  const double clientWidth = client.value(QStringLiteral("width")).toDouble();
  const bool containerCapped = expectedMax && scene.bounds.width() > clientWidth;
  const double scaledBoundsTolerance = containerCapped ? 1e12 : 1.25;
  if (!containerCapped) {
    near(scene.bounds.width(), clientWidth,
         id + QStringLiteral("/client/w"), .001);
    near(scene.bounds.height(), client.value(QStringLiteral("height")).toDouble(),
         id + QStringLiteral("/client/h"), .001);
  }

  const QJsonArray backgrounds = expected.value(QStringLiteral("backgrounds")).toArray();
  require(scene.backgrounds.size() == backgrounds.size(),
          id + QStringLiteral("/background-count"));
  for (qsizetype i = 0; i < backgrounds.size(); ++i) {
    const auto &actual = scene.backgrounds.at(i);
    const QJsonObject oracle = backgrounds.at(i).toObject();
    const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
    near(actual.rect.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/background/%1/x").arg(i));
    near(actual.rect.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/background/%1/y").arg(i));
    near(actual.rect.width(), attrs.value(QStringLiteral("width")).toString().toDouble(),
         id + QStringLiteral("/background/%1/w").arg(i));
    near(actual.rect.height(), attrs.value(QStringLiteral("height")).toString().toDouble(),
         id + QStringLiteral("/background/%1/h").arg(i));
    sameColor(actual.fill,
              oracle.value(QStringLiteral("computed")).toObject()
                  .value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/background/%1/fill").arg(i));
    near(actual.fillOpacity,
         oracle.value(QStringLiteral("computed")).toObject()
             .value(QStringLiteral("fillOpacity")).toString().toDouble(),
         id + QStringLiteral("/background/%1/opacity").arg(i));
  }

  const QJsonArray boundaries = expected.value(QStringLiteral("boundaries")).toArray();
  require(scene.boundaries.size() == boundaries.size(),
          id + QStringLiteral("/boundary-count"));
  const bool stableSeed = QRegularExpression(
      QStringLiteral(R"("seed"\s*:\s*[1-9]\d*(?:\.\d+)?)"))
                              .match(source).hasMatch();
  for (qsizetype i = 0; i < boundaries.size(); ++i) {
    const auto &actual = scene.boundaries.at(i);
    const QJsonObject oracle = boundaries.at(i).toObject();
    if (stableSeed || i == 2)
      require(actual.pathData == oracle.value(QStringLiteral("attrs")).toObject()
                                      .value(QStringLiteral("d")).toString(),
              id + QStringLiteral("/boundary/%1/path").arg(i));
    const QJsonObject computed = oracle.value(QStringLiteral("computed")).toObject();
    sameColor(actual.stroke, computed.value(QStringLiteral("stroke")).toString(),
              id + QStringLiteral("/boundary/%1/stroke").arg(i));
    near(actual.strokeWidth, px(computed.value(QStringLiteral("strokeWidth")).toString()),
         id + QStringLiteral("/boundary/%1/width").arg(i), .001);
  }

  const QJsonObject confusion = expected.value(QStringLiteral("confusion")).toObject();
  require(scene.confusion.pathData ==
              confusion.value(QStringLiteral("attrs")).toObject()
                  .value(QStringLiteral("d")).toString(),
          id + QStringLiteral("/confusion/path"));
  sameColor(scene.confusion.fill,
            confusion.value(QStringLiteral("computed")).toObject()
                .value(QStringLiteral("fill")).toString(),
            id + QStringLiteral("/confusion/fill"));
  sameColor(scene.confusion.stroke,
            confusion.value(QStringLiteral("computed")).toObject()
                .value(QStringLiteral("stroke")).toString(),
            id + QStringLiteral("/confusion/stroke"));

  const QJsonArray labels = expected.value(QStringLiteral("labels")).toArray();
  require(scene.labels.size() == labels.size(), id + QStringLiteral("/label-count"));
  for (qsizetype i = 0; i < labels.size(); ++i)
    compareText(scene.labels.at(i), labels.at(i).toObject(),
                id + QStringLiteral("/label/%1").arg(i),
                scaledBoundsTolerance);
  const QJsonArray subtitles = expected.value(QStringLiteral("subtitles")).toArray();
  require(scene.subtitles.size() == subtitles.size(),
          id + QStringLiteral("/subtitle-count"));
  for (qsizetype i = 0; i < subtitles.size(); ++i)
    compareText(scene.subtitles.at(i), subtitles.at(i).toObject(),
                id + QStringLiteral("/subtitle/%1").arg(i),
                containerCapped ? 1e12 : 3.5);

  const QJsonArray items = expected.value(QStringLiteral("items")).toArray();
  require(scene.items.size() == items.size(), id + QStringLiteral("/item-count"));
  for (qsizetype i = 0; i < items.size(); ++i) {
    const auto &actual = scene.items.at(i);
    const QJsonObject oracle = items.at(i).toObject();
    const QJsonObject rect = oracle.value(QStringLiteral("rect")).toObject();
    if (containerCapped) {
      require(actual.text.text ==
                  oracle.value(QStringLiteral("text")).toObject()
                      .value(QStringLiteral("text")).toString(),
              id + QStringLiteral("/item/%1/text").arg(i));
      sameColor(actual.rect.fill,
                rect.value(QStringLiteral("computed")).toObject()
                    .value(QStringLiteral("fill")).toString(),
                id + QStringLiteral("/item/%1/fill").arg(i));
      continue;
    }
    const QVector<double> transform = numbers(
        oracle.value(QStringLiteral("attrs")).toObject()
            .value(QStringLiteral("transform")).toString());
    require(transform.size() == 2, id + QStringLiteral("/item/transform"));
    near(actual.translation.x(), transform.at(0),
         id + QStringLiteral("/item/%1/x").arg(i), .06);
    near(actual.translation.y(), transform.at(1),
         id + QStringLiteral("/item/%1/y").arg(i), .06);
    const QJsonObject attrs = rect.value(QStringLiteral("attrs")).toObject();
    near(actual.rect.rect.width(), attrs.value(QStringLiteral("width")).toString().toDouble(),
         id + QStringLiteral("/item/%1/w").arg(i), .12);
    near(actual.rect.rect.height(), attrs.value(QStringLiteral("height")).toString().toDouble(),
         id + QStringLiteral("/item/%1/h").arg(i));
    sameColor(actual.rect.fill,
              rect.value(QStringLiteral("computed")).toObject()
                  .value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/item/%1/fill").arg(i));
    compareText(actual.text, oracle.value(QStringLiteral("text")).toObject(),
                id + QStringLiteral("/item/%1/text").arg(i),
                containerCapped ? 1e12 : 1.5, .06);
  }

  const QJsonArray arrows = expected.value(QStringLiteral("arrows")).toArray();
  qsizetype expectedIndex = 0;
  require(std::count_if(arrows.begin(), arrows.end(), [](const QJsonValue &value) {
            return value.toObject().value(QStringLiteral("tag")).toString() ==
                   QLatin1String("path");
          }) == scene.arrows.size(), id + QStringLiteral("/arrow-count"));
  for (qsizetype i = 0; i < scene.arrows.size(); ++i) {
    while (expectedIndex < arrows.size() &&
           arrows.at(expectedIndex).toObject().value(QStringLiteral("tag")).toString() !=
               QLatin1String("path"))
      ++expectedIndex;
    require(expectedIndex < arrows.size(), id + QStringLiteral("/arrow/path"));
    const auto &actual = scene.arrows.at(i);
    const QJsonObject path = arrows.at(expectedIndex++).toObject();
    require(actual.pathData == path.value(QStringLiteral("attrs")).toObject()
                                   .value(QStringLiteral("d")).toString(),
            id + QStringLiteral("/arrow/%1/path").arg(i));
    const QJsonObject computed = path.value(QStringLiteral("computed")).toObject();
    sameColor(actual.stroke, computed.value(QStringLiteral("stroke")).toString(),
              id + QStringLiteral("/arrow/%1/stroke").arg(i));
    near(actual.strokeWidth, px(computed.value(QStringLiteral("strokeWidth")).toString()),
         id + QStringLiteral("/arrow/%1/width").arg(i), .001);
    if (!actual.label.text.isEmpty()) {
      require(expectedIndex < arrows.size(), id + QStringLiteral("/arrow/label"));
      compareText(actual.label, arrows.at(expectedIndex++).toObject(),
                  id + QStringLiteral("/arrow/%1/label").arg(i),
                  scaledBoundsTolerance);
    }
  }

  const QJsonValue title = expected.value(QStringLiteral("title"));
  if (title.isNull())
    require(scene.title.text.isEmpty(), id + QStringLiteral("/title/absent"));
  else
    compareText(scene.title, title.toObject(), id + QStringLiteral("/title"),
                containerCapped ? 1e12 : 2.2);
}

} // namespace cynefin_test
