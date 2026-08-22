#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/venn/VennScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 1e-6) {
  require((std::isnan(actual) && std::isnan(expected)) ||
              (std::isfinite(actual) && std::fabs(actual - expected) <= tolerance),
          QStringLiteral("%1: %2 != %3").arg(path)
              .arg(actual, 0, 'g', 17).arg(expected, 0, 'g', 17));
}
QVector<qreal> numbers(const QString& text) {
  static const QRegularExpression re(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"));
  QVector<qreal> result;
  auto matches = re.globalMatch(text);
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}
QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(QStringLiteral(
      R"(^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\))"));
  const auto match = rgb.match(value);
  if (!match.hasMatch()) return color::toQColor(value);
  QColor result(match.captured(1).toInt(), match.captured(2).toInt(),
                match.captured(3).toInt());
  if (!match.captured(4).isEmpty()) result.setAlphaF(match.captured(4).toDouble());
  return result;
}
void samePaint(const QString& actual, const QString& expected,
               const QString& path) {
  if (expected == QLatin1String("none")) {
    require(actual.isEmpty() || actual == QLatin1String("none"), path + "/none");
    return;
  }
  const QColor a = computedColor(actual), b = computedColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
}
qreal px(QString value) { value.remove(QStringLiteral("px")); return value.toDouble(); }
}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; macOS
  // (SF/Helvetica) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Venn config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "236b612c4995ef2b112aab836587e070b950355bae7998f8cab9ae664c112ce7"),
          QStringLiteral("Venn config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "647cd5c6b1c6a22add6393874811b774f998426154fd40f5f5e6e482e98bc1b0"),
          QStringLiteral("Venn config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 34, QStringLiteral("Expected 34 config cases"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene = std::dynamic_pointer_cast<const venn::VennScene>(entry.scene);
    require(bool(scene), id + QStringLiteral("/scene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject rootObject = expected.value(QStringLiteral("root")).toObject();
    const QJsonObject attrs = rootObject.value(QStringLiteral("attrs")).toObject();
    require(scene->viewBoxAttribute == attrs.value(QStringLiteral("viewBox")).toString(),
            id + QStringLiteral("/viewBox"));
    require(scene->useMaxWidth ==
                (attrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));
    const QJsonObject client = rootObject.value(QStringLiteral("client")).toObject();
    require(entry.naturalSize == QSize(qRound(client.value(QStringLiteral("width")).toDouble()),
                                       qRound(client.value(QStringLiteral("height")).toDouble())),
            QStringLiteral("%1/natural %2x%3 expected %4x%5")
                .arg(id).arg(entry.naturalSize.width()).arg(entry.naturalSize.height())
                .arg(client.value(QStringLiteral("width")).toDouble())
                .arg(client.value(QStringLiteral("height")).toDouble()));

    const QJsonArray areas = expected.value(QStringLiteral("areas")).toArray();
    require(scene->areas.size() == areas.size(), id + QStringLiteral("/areas"));
    for (qsizetype i = 0; i < areas.size(); ++i) {
      const auto& actual = scene->areas.at(i);
      const QJsonObject area = areas.at(i).toObject();
      const QString path = QStringLiteral("%1/area/%2").arg(id).arg(i);
      const QJsonObject expectedPath = area.value(QStringLiteral("path")).toObject();
      const QJsonArray roughPaths = area.value(QStringLiteral("roughPaths")).toArray();
      if (expectedPath.isEmpty()) {
        require(actual.roughDrawable.sets.size() == roughPaths.size(), path + "/rough");
      } else {
        const QJsonObject computed = expectedPath.value(QStringLiteral("computed")).toObject();
        const qreal fillOpacity = computed.value(QStringLiteral("fillOpacity")).toString().toDouble();
        if (fillOpacity > 0.0)
          samePaint(actual.fill, computed.value(QStringLiteral("fill")).toString(), path + "/fill");
        samePaint(actual.stroke, computed.value(QStringLiteral("stroke")).toString(), path + "/stroke");
        near(actual.fillOpacity, fillOpacity, path + "/fillOpacity");
        near(actual.strokeWidth, px(computed.value(QStringLiteral("strokeWidth")).toString()), path + "/strokeWidth");
      }
      const QJsonObject text = area.value(QStringLiteral("textElement")).toObject();
      samePaint(actual.label.fill,
                text.value(QStringLiteral("computed")).toObject()
                    .value(QStringLiteral("fill")).toString(),
                path + "/label");
    }

    const QJsonObject expectedTitle = expected.value(QStringLiteral("title")).toObject();
    if (expectedTitle.isEmpty()) {
      require(scene->titleText.lines.isEmpty(), id + QStringLiteral("/no title"));
    } else {
      require(scene->titleText.source == expectedTitle.value(QStringLiteral("text")).toString(),
              id + QStringLiteral("/title text"));
      samePaint(scene->titleText.fill,
                expectedTitle.value(QStringLiteral("computed")).toObject()
                    .value(QStringLiteral("fill")).toString(),
                id + QStringLiteral("/title color"));
    }
    const QJsonArray foreign = expected.value(QStringLiteral("foreignObjects")).toArray();
    require(scene->textNodes.size() == foreign.size(), id + QStringLiteral("/text nodes"));
    for (qsizetype i = 0; i < foreign.size(); ++i) {
      const QJsonObject object = foreign.at(i).toObject();
      const QJsonObject objectAttrs = object.value(QStringLiteral("attrs")).toObject();
      const QRectF box = scene->textNodes.at(i).box;
      near(box.x(), objectAttrs.value(QStringLiteral("x")).toString().toDouble(), id + "/text/x");
      near(box.y(), objectAttrs.value(QStringLiteral("y")).toString().toDouble(), id + "/text/y");
      near(box.width(), objectAttrs.value(QStringLiteral("width")).toString().toDouble(), id + "/text/w");
      near(box.height(), objectAttrs.value(QStringLiteral("height")).toString().toDouble(), id + "/text/h");
    }
    require(scene->debugCircles.size() ==
                expected.value(QStringLiteral("debugCircles")).toArray().size(),
            id + QStringLiteral("/debug circles"));
    require(scene->debugCells.size() ==
                expected.value(QStringLiteral("debugCells")).toArray().size(),
            id + QStringLiteral("/debug cells"));
    require(entry.metadata.title.isEmpty() && !entry.metadata.svgEmitAccessibleTitle,
            id + QStringLiteral("/metadata"));
  }
  std::puts("MermaidVennEdgeParityTest: 34/34 passed");
  return 0;
}
