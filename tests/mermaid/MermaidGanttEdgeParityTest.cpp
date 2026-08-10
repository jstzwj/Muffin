#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QDate>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
void near(qreal actual, qreal expected, const QString& path, qreal tolerance = 0.01) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual).arg(expected));
}
void colorEqual(const QString& actual, const QString& expected, const QString& path) {
  const QColor a = color::toQColor(actual), b = color::toQColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}
std::shared_ptr<const gantt::GanttScene> render(const QString& source) {
  static editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Gantt config render: ") + entry.errorMessage);
  auto scene = std::dynamic_pointer_cast<const gantt::GanttScene>(entry.scene);
  require(bool(scene), QStringLiteral("Expected GanttScene"));
  return scene;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Gantt config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("7b0ffdba46af19361bf6758be2f4b8c7339f67b997157d54bd4df12eff64ffb9"),
          QStringLiteral("Gantt config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("17642b642110e60de004a6e0d27ac4f24d11641c142a489e9877f3b60fb72ebc"),
          QStringLiteral("Gantt config fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 46, QStringLiteral("Expected 46 Gantt config cases"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const auto scene = render(fixture.value(QStringLiteral("source")).toString());
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject conf = expected.value(QStringLiteral("ganttConfig")).toObject();
    near(scene->config.useWidth, conf.value(QStringLiteral("useWidth")).toDouble(1200.0),
         id + QStringLiteral("/useWidth"));
    const QJsonValue expectedUseMaxWidth = conf.value(QStringLiteral("useMaxWidth"));
    const bool expectedUseMaxWidthBool = expectedUseMaxWidth.isBool()
                                             ? expectedUseMaxWidth.toBool()
                                             : !expectedUseMaxWidth.isNull() &&
                                                   !expectedUseMaxWidth.isUndefined() &&
                                                   (!expectedUseMaxWidth.isDouble() ||
                                                    expectedUseMaxWidth.toDouble() != 0.0) &&
                                                   (!expectedUseMaxWidth.isString() ||
                                                    !expectedUseMaxWidth.toString().isEmpty());
    require(scene->config.useMaxWidth == expectedUseMaxWidthBool,
            id + QStringLiteral("/useMaxWidth"));
    near(scene->config.titleTopMargin, conf.value(QStringLiteral("titleTopMargin")).toDouble(), id + QStringLiteral("/titleTopMargin"));
    near(scene->config.barHeight, conf.value(QStringLiteral("barHeight")).toVariant().toDouble(), id + QStringLiteral("/barHeight"));
    near(scene->config.barGap, conf.value(QStringLiteral("barGap")).toVariant().toDouble(), id + QStringLiteral("/barGap"));
    near(scene->config.topPadding, conf.value(QStringLiteral("topPadding")).toVariant().toDouble(), id + QStringLiteral("/topPadding"));
    near(scene->config.rightPadding, conf.value(QStringLiteral("rightPadding")).toVariant().toDouble(), id + QStringLiteral("/rightPadding"));
    near(scene->config.leftPadding, conf.value(QStringLiteral("leftPadding")).toVariant().toDouble(), id + QStringLiteral("/leftPadding"));
    near(scene->config.gridLineStartPadding, conf.value(QStringLiteral("gridLineStartPadding")).toVariant().toDouble(), id + QStringLiteral("/gridLineStartPadding"));
    near(scene->config.fontSize, conf.value(QStringLiteral("fontSize")).toVariant().toDouble(), id + QStringLiteral("/fontSize"));
    near(scene->config.sectionFontSize, conf.value(QStringLiteral("sectionFontSize")).toVariant().toDouble(), id + QStringLiteral("/sectionFontSize"));
    require(scene->config.numberSectionStyles == conf.value(QStringLiteral("numberSectionStyles")).toVariant().toInt(), id + QStringLiteral("/numberSectionStyles"));
    const QJsonObject theme = expected.value(QStringLiteral("themeVariables")).toObject();
    const auto field = [&](const QString& name, const QString& native) {
      colorEqual(native, theme.value(name).toString(), id + QLatin1Char('/') + name);
    };
    field(QStringLiteral("sectionBkgColor"), scene->style.sectionBkgColor);
    field(QStringLiteral("sectionBkgColor2"), scene->style.sectionBkgColor2);
    field(QStringLiteral("altSectionBkgColor"), scene->style.altSectionBkgColor);
    field(QStringLiteral("excludeBkgColor"), scene->style.excludeBkgColor);
    field(QStringLiteral("taskBkgColor"), scene->style.taskBkgColor);
    field(QStringLiteral("taskBorderColor"), scene->style.taskBorderColor);
    field(QStringLiteral("taskTextColor"), scene->style.taskTextColor);
    field(QStringLiteral("taskTextDarkColor"), scene->style.taskTextDarkColor);
    field(QStringLiteral("taskTextOutsideColor"), scene->style.taskTextOutsideColor);
    field(QStringLiteral("taskTextClickableColor"), scene->style.taskTextClickableColor);
    field(QStringLiteral("activeTaskBkgColor"), scene->style.activeTaskBkgColor);
    field(QStringLiteral("activeTaskBorderColor"), scene->style.activeTaskBorderColor);
    field(QStringLiteral("doneTaskBkgColor"), scene->style.doneTaskBkgColor);
    field(QStringLiteral("doneTaskBorderColor"), scene->style.doneTaskBorderColor);
    field(QStringLiteral("critBkgColor"), scene->style.critBkgColor);
    field(QStringLiteral("critBorderColor"), scene->style.critBorderColor);
    field(QStringLiteral("gridColor"), scene->style.gridColor);
    field(QStringLiteral("todayLineColor"), scene->style.todayLineColor);
    field(QStringLiteral("titleColor"), scene->style.titleColor);
  }

  const QDate today = QDate::currentDate();
  const QString todaySource = QStringLiteral(
      "gantt\ndateFormat YYYY-MM-DD\ntodayMarker "
      "stroke:#123456,stroke-width:3px,opacity:0.4\n"
      "Before :before, %1, 1d\nAfter :after, %2, 1d")
                                  .arg(today.addDays(-1).toString(Qt::ISODate),
                                       today.toString(Qt::ISODate));
  const auto todayScene = render(todaySource);
  require(todayScene->todayLines.size() == 1,
          QStringLiteral("Gantt current-date marker missing"));
  const auto& todayLine = todayScene->todayLines.first();
  colorEqual(todayLine.stroke, QStringLiteral("#123456"),
             QStringLiteral("today-marker/stroke"));
  near(todayLine.strokeWidth, 3.0, QStringLiteral("today-marker/stroke-width"));
  near(todayLine.opacity, 0.4, QStringLiteral("today-marker/opacity"));
  require(todayLine.line.x1() == todayLine.line.x2() &&
              todayLine.line.x1() >= 0.0 &&
              todayLine.line.x1() <= todayScene->bounds.width(),
          QStringLiteral("Gantt current-date marker geometry drifted"));
  const auto offScene = render(QStringLiteral(
      "gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\n"
      "Task :task, 2024-01-01, 1d"));
  require(offScene->todayLines.isEmpty(),
          QStringLiteral("Gantt todayMarker off must suppress the marker"));
  std::fprintf(stderr, "Gantt config parity: 46/46 passed\n");
  return 0;
}
