#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/timeline/TimelineScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

double number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  bool ok = false;
  const double result = value.toString().toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + value.toString());
  return result;
}

void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.25) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3 (tol %4)")
              .arg(path)
              .arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17)
              .arg(tolerance));
}

QPointF translate(const QString& value) {
  static const QRegularExpression re(
      QStringLiteral(R"(^translate\(\s*([^,\s]+)[,\s]+([^\)\s]+)\s*\)$)"));
  const auto match = re.match(value);
  require(match.hasMatch(), QStringLiteral("Invalid translate: ") + value);
  return QPointF(number(match.captured(1)), number(match.captured(2)));
}

QRectF viewBox(const QString& value) {
  const QStringList fields = value.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("Invalid viewBox: ") + value);
  return QRectF(number(fields[0]), number(fields[1]), number(fields[2]),
                number(fields[3]));
}

std::shared_ptr<const timeline::TimelineScene> renderScene(const QString& source) {
  static editor::MermaidRenderCache cache;
  const editor::MermaidRenderKey key = editor::MermaidRenderCache::makeKey(source);
  const editor::MermaidRenderEntry entry = cache.getSync(key, source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Timeline must render: ") + entry.errorMessage);
  auto scene = std::dynamic_pointer_cast<const timeline::TimelineScene>(entry.scene);
  require(bool(scene), QStringLiteral("Expected TimelineScene"));
  return scene;
}

QString computed(const QJsonObject& element, const char* key) {
  return element.value(QStringLiteral("computed"))
      .toObject()
      .value(QLatin1String(key))
      .toString();
}

void compareColor(const QString& actual, const QString& expected,
                  const QString& path) {
  const QColor a = color::toQColor(actual);
  const QColor b = color::toQColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}

QStringList pathTokens(const QString& path) {
  static const QRegularExpression token(
      QStringLiteral(R"(([A-Za-z]|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?))"));
  QStringList result;
  auto it = token.globalMatch(path);
  while (it.hasNext()) {
    const QString value = it.next().captured();
    bool ok = false;
    const qreal number = value.toDouble(&ok);
    result.append(ok ? QString::number(std::round(number * 1000.0) / 1000.0,
                                       'g', 15)
                     : value);
  }
  return result;
}

void compareCase(const QString& id, const timeline::TimelineScene& scene,
                 const QJsonObject& expected) {
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject rootAttrs = root.value(QStringLiteral("attrs")).toObject();
  const QRectF expectedView = viewBox(rootAttrs.value(QStringLiteral("viewBox")).toString());
  near(scene.bounds.x(), expectedView.x(), id + QStringLiteral("/viewBox/x"));
  near(scene.bounds.y(), expectedView.y(), id + QStringLiteral("/viewBox/y"));
  near(scene.bounds.width(), expectedView.width(), id + QStringLiteral("/viewBox/w"), 0.5);
  near(scene.bounds.height(), expectedView.height(), id + QStringLiteral("/viewBox/h"));

  const QJsonArray nodes = expected.value(QStringLiteral("nodes")).toArray();
  require(scene.nodes.size() == nodes.size(),
          id + QStringLiteral(": node count mismatch"));
  for (qsizetype i = 0; i < nodes.size(); ++i) {
    const QJsonObject oracle = nodes.at(i).toObject();
    const timeline::TimelineNodeGeometry& native = scene.nodes.at(i);
    const QPointF position = translate(oracle.value(QStringLiteral("outerTransform")).toString());
    const QJsonArray background = oracle.value(QStringLiteral("background")).toArray();
    require(!background.isEmpty(), id + QStringLiteral(": node background missing"));
    const QJsonObject shapeBox =
        background.first().toObject().value(QStringLiteral("bbox")).toObject();
    near(native.position.x(), position.x(), id + QStringLiteral("/node/x"));
    near(native.position.y(), position.y(), id + QStringLiteral("/node/y"));
    near(native.width, number(shapeBox.value(QStringLiteral("width"))),
         id + QStringLiteral("/node/w"));
    near(native.height, number(shapeBox.value(QStringLiteral("height"))),
         id + QStringLiteral("/node/h"));
    require(oracle.value(QStringLiteral("class")).toString().contains(native.sectionClass),
            id + QStringLiteral(": section class mismatch"));

    if (!background.isEmpty()) {
      const QJsonObject path = background.first().toObject();
      const QString expectedPath = path.value(QStringLiteral("attrs"))
                                       .toObject()
                                       .value(QStringLiteral("d"))
                                       .toString();
      require(pathTokens(native.pathData) == pathTokens(expectedPath),
              id + QStringLiteral(": path formula mismatch"));
      const QString fill = computed(path, "fill");
      if (color::isParsableColor(native.fill) && color::isParsableColor(fill))
        compareColor(native.fill, fill, id + QStringLiteral("/node/fill"));
    }

    const QPointF textTransform =
        translate(oracle.value(QStringLiteral("textTransform")).toString());
    near(native.textOffset.x(), textTransform.x(), id + QStringLiteral("/text/x"));
    near(native.textOffset.y(), textTransform.y(), id + QStringLiteral("/text/y"));
    const QJsonArray tspans = oracle.value(QStringLiteral("tspans")).toArray();
    require(native.textLines.size() == tspans.size(),
            id + QStringLiteral(": tspan count mismatch"));
    for (qsizetype line = 0; line < tspans.size(); ++line)
      require(native.textLines.at(line).sourceText ==
                  tspans.at(line).toObject().value(QStringLiteral("text")).toString(),
              id + QStringLiteral(": wrapped tspan mismatch"));
  }

  const QJsonArray connectors = expected.value(QStringLiteral("connectors")).toArray();
  require(scene.lines.size() == connectors.size(),
          id + QStringLiteral(": connector count mismatch"));
  QVector<const timeline::TimelineLineGeometry*> available;
  for (const auto& line : scene.lines) available.append(&line);
  for (const QJsonValue& connectorValue : connectors) {
    const QJsonObject connector = connectorValue.toObject();
    const QJsonObject attrs = connector.value(QStringLiteral("attrs")).toObject();
    const qreal x1 = number(attrs.value(QStringLiteral("x1")));
    const qreal y1 = number(attrs.value(QStringLiteral("y1")));
    const qreal x2 = number(attrs.value(QStringLiteral("x2")));
    const qreal y2 = number(attrs.value(QStringLiteral("y2")));
    auto it = std::find_if(available.begin(), available.end(), [&](const auto* line) {
      return std::fabs(line->start.x() - x1) <= 0.5 &&
             std::fabs(line->start.y() - y1) <= 0.5 &&
             std::fabs(line->end.x() - x2) <= 0.5 &&
             std::fabs(line->end.y() - y2) <= 0.5;
    });
    require(it != available.end(), id + QStringLiteral(": connector geometry missing"));
    const qreal expectedWidth = computed(connector, "stroke-width")
                                    .chopped(2)
                                    .toDouble();
    near((*it)->strokeWidth, expectedWidth,
         id + QStringLiteral("/connector/strokeWidth"), 0.001);
    const bool unresolved = attrs.value(QStringLiteral("marker-end")).toString() ==
                            QStringLiteral("url(#arrowhead)");
    require((*it)->markerResolved != unresolved,
            id + QStringLiteral(": marker resolution mismatch"));
    available.erase(it);
  }

  const QJsonArray titles = expected.value(QStringLiteral("titles")).toArray();
  require(scene.titleGeometry.visible == !titles.isEmpty(),
          id + QStringLiteral(": title visibility mismatch"));
  if (!titles.isEmpty()) {
    const QJsonObject attrs = titles.first().toObject().value(QStringLiteral("attrs")).toObject();
    near(scene.titleGeometry.baseline.x(), number(attrs.value(QStringLiteral("x"))),
         id + QStringLiteral("/title/x"), 0.5);
    near(scene.titleGeometry.baseline.y(), number(attrs.value(QStringLiteral("y"))),
         id + QStringLiteral("/title/y"), 0.001);
  }
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected timeline geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  QByteArray canonicalBytes = bytes;
  canonicalBytes.replace("\r\n", "\n");
  canonicalBytes.replace('\r', '\n');
  require(QCryptographicHash::hash(canonicalBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral(
                  "e8d3908312919b399b4e2a2c0133d363e182f0000b77da70ad833a0ae47e37dd"),
          QStringLiteral("Timeline geometry fixture bytes changed; audit its digest"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("moduleSha256")).toString() ==
                  QLatin1String(
                      "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "f3c6080afd8a7f3fffa255ce9fe9531b257256928293af238fcd880f84a29399"),
          QStringLiteral("Timeline geometry provenance changed; audit the fixture"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 16, QStringLiteral("Timeline geometry fixture must have 16 cases"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const auto scene = renderScene(fixture.value(QStringLiteral("source")).toString());
    compareCase(id, *scene, fixture.value(QStringLiteral("expected")).toObject());
  }
  std::fprintf(stderr, "Timeline geometry oracle: 16/16 passed\n");
  return 0;
}
