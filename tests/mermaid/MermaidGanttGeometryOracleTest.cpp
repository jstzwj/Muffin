#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/MermaidColor.h"

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
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
double number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  bool ok = false;
  const double result = value.toString().toDouble(&ok);
  require(ok, QStringLiteral("Invalid number: ") + value.toString());
  return result;
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.51) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}
QRectF viewBox(const QString& value) {
  const QStringList fields = value.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("Invalid viewBox"));
  return QRectF(number(fields[0]), number(fields[1]), number(fields[2]),
                number(fields[3]));
}
qreal translatedX(const QString& value) {
  static const QRegularExpression re(
      QStringLiteral(R"(^translate\(([^,\s]+)[,\s]+[^\)]+\)$)"));
  const auto match = re.match(value);
  require(match.hasMatch(), QStringLiteral("Invalid translate: ") + value);
  return number(match.captured(1));
}
void colorNear(const QString& actual, const QString& expected,
               const QString& path) {
  const QColor a = color::toQColor(actual);
  const QColor b = color::toQColor(expected);
  require(a.isValid() && b.isValid() &&
              std::abs(a.red() - b.red()) <= 1 &&
              std::abs(a.green() - b.green()) <= 1 &&
              std::abs(a.blue() - b.blue()) <= 1 &&
              std::abs(a.alpha() - b.alpha()) <= 1,
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}
std::shared_ptr<const gantt::GanttScene> render(const QString& source) {
  static editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Gantt must render: ") + entry.errorMessage);
  auto scene = std::dynamic_pointer_cast<const gantt::GanttScene>(entry.scene);
  require(bool(scene), QStringLiteral("Expected GanttScene"));
  return scene;
}
QVector<QJsonObject> elements(const QJsonObject& expected, const QString& tag,
                              const QRegularExpression& classPattern) {
  QVector<QJsonObject> result;
  for (const QJsonValue& value : expected.value(QStringLiteral("elements")).toArray()) {
    const QJsonObject element = value.toObject();
    const QString cssClass = element.value(QStringLiteral("attrs")).toObject()
                                 .value(QStringLiteral("class")).toString();
    if (element.value(QStringLiteral("tag")).toString() == tag &&
        classPattern.match(cssClass).hasMatch())
      result.append(element);
  }
  return result;
}
void compareRects(const QString& id, const QVector<gantt::GanttRectGeometry>& native,
                  const QVector<QJsonObject>& oracle, const QString& kind) {
  require(native.size() == oracle.size(),
          QStringLiteral("%1/%2 count %3 != %4").arg(id, kind)
              .arg(native.size()).arg(oracle.size()));
  for (qsizetype i = 0; i < native.size(); ++i) {
    const QJsonObject attrs = oracle.at(i).value(QStringLiteral("attrs")).toObject();
    near(native.at(i).rect.x(), number(attrs.value(QStringLiteral("x"))),
         QStringLiteral("%1/%2/%3/x").arg(id, kind).arg(i));
    near(native.at(i).rect.y(), number(attrs.value(QStringLiteral("y"))),
         QStringLiteral("%1/%2/%3/y").arg(id, kind).arg(i));
    near(native.at(i).rect.width(), number(attrs.value(QStringLiteral("width"))),
         QStringLiteral("%1/%2/%3/w").arg(id, kind).arg(i));
    near(native.at(i).rect.height(), number(attrs.value(QStringLiteral("height"))),
         QStringLiteral("%1/%2/%3/h").arg(id, kind).arg(i));
    const QString expectedFill = oracle.at(i).value(QStringLiteral("computed"))
                                     .toObject().value(QStringLiteral("fill")).toString();
    colorNear(native.at(i).fill, expectedFill,
              QStringLiteral("%1/%2/%3/fill").arg(id, kind).arg(i));
  }
}
void compareTexts(const QString& id, const QVector<gantt::GanttTextGeometry>& native,
                  const QVector<QJsonObject>& oracle, const QString& kind) {
  require(native.size() == oracle.size(),
          QStringLiteral("%1/%2 count %3 != %4").arg(id, kind)
              .arg(native.size()).arg(oracle.size()));
  for (qsizetype i = 0; i < native.size(); ++i) {
    const QJsonObject attrs = oracle.at(i).value(QStringLiteral("attrs")).toObject();
    const QString nativeText = native.at(i).lines.isEmpty()
                                   ? native.at(i).text
                                   : native.at(i).lines.join(QString());
    require(nativeText == oracle.at(i).value(QStringLiteral("text")).toString(),
            QStringLiteral("%1/%2/%3 text").arg(id, kind).arg(i));
    near(native.at(i).position.x(), number(attrs.value(QStringLiteral("x"))),
         QStringLiteral("%1/%2/%3/x").arg(id, kind).arg(i));
    near(native.at(i).position.y(), number(attrs.value(QStringLiteral("y"))),
         QStringLiteral("%1/%2/%3/y").arg(id, kind).arg(i));
    colorNear(native.at(i).fill,
              oracle.at(i).value(QStringLiteral("computed")).toObject()
                  .value(QStringLiteral("fill")).toString(),
              QStringLiteral("%1/%2/%3/fill").arg(id, kind).arg(i));
  }
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Gantt geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("e094a61f7fab09f647160bceef1eef7169b0a52cf848b433fdc4f659df2f4feb"),
          QStringLiteral("Gantt geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("d459e653e38e6e226d4bfd856c88d6f35c54d90f0729ebe61c7a6076ebdb8d3f"),
          QStringLiteral("Gantt geometry fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 18, QStringLiteral("Expected 18 Gantt geometry cases"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto scene = render(fixture.value(QStringLiteral("source")).toString());
    const QRectF vb = viewBox(expected.value(QStringLiteral("root")).toObject()
                                  .value(QStringLiteral("attrs")).toObject()
                                  .value(QStringLiteral("viewBox")).toString());
    near(scene->bounds.width(), vb.width(), id + QStringLiteral("/viewBox/w"), 0.01);
    near(scene->bounds.height(), vb.height(), id + QStringLiteral("/viewBox/h"), 0.01);
    compareRects(id, scene->excludes,
                 elements(expected, QStringLiteral("rect"),
                          QRegularExpression(QStringLiteral("^exclude-range$"))),
                 QStringLiteral("exclude"));
    compareRects(id, scene->sections,
                 elements(expected, QStringLiteral("rect"),
                          QRegularExpression(QStringLiteral("^section\\s"))),
                 QStringLiteral("section"));
    compareRects(id, scene->tasks,
                 elements(expected, QStringLiteral("rect"),
                          QRegularExpression(QStringLiteral("^task(?:\\s|$)"))),
                 QStringLiteral("task"));
    compareTexts(id, scene->taskLabels,
                 elements(expected, QStringLiteral("text"),
                          QRegularExpression(QStringLiteral("(?:^|\\s)taskText"))),
                 QStringLiteral("taskText"));
    compareTexts(id, scene->sectionLabels,
                 elements(expected, QStringLiteral("text"),
                          QRegularExpression(QStringLiteral("^sectionTitle"))),
                 QStringLiteral("sectionText"));
    const QVector<QJsonObject> ticks = elements(
        expected, QStringLiteral("g"),
        QRegularExpression(QStringLiteral("^tick$")));
    require(scene->gridLabels.size() == ticks.size(),
            id + QStringLiteral("/tick count"));
    require(scene->gridLines.size() == ticks.size(),
            id + QStringLiteral("/grid line count"));
    for (qsizetype tick = 0; tick < ticks.size(); ++tick) {
      const QJsonObject attrs = ticks.at(tick).value(QStringLiteral("attrs")).toObject();
      require(scene->gridLabels.at(tick).text ==
                  ticks.at(tick).value(QStringLiteral("text")).toString(),
              QStringLiteral("%1/tick/%2/text").arg(id).arg(tick));
      near(scene->gridLabels.at(tick).position.x() - scene->config.leftPadding,
           translatedX(attrs.value(QStringLiteral("transform")).toString()),
           QStringLiteral("%1/tick/%2/x").arg(id).arg(tick), 0.01);
    }
    const auto titles = elements(expected, QStringLiteral("text"),
                                 QRegularExpression(QStringLiteral("^titleText$")));
    require(titles.size() == 1, id + QStringLiteral(" title missing"));
    compareTexts(id, QVector<gantt::GanttTextGeometry>{scene->titleGeometry},
                 titles, QStringLiteral("title"));
  }
  std::fprintf(stderr, "Gantt geometry oracle: 18/18 passed\n");
  return 0;
}
