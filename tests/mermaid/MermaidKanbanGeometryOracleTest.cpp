#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/kanban/KanbanScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

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

std::shared_ptr<const kanban::KanbanScene> render(const QString& source,
                                                  editor::MermaidRenderEntry* out = nullptr) {
  editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(cache.makeKey(source), source);
  if (out) *out = entry;
  if (entry.status != editor::MermaidRenderStatus::Ready || !entry.scene) return {};
  return std::dynamic_pointer_cast<const kanban::KanbanScene>(entry.scene);
}

QVector<qreal> numbers(const QString& value) {
  static const QRegularExpression re(
      QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
  QVector<qreal> result;
  auto it = re.globalMatch(value);
  while (it.hasNext()) result.append(it.next().captured().toDouble());
  return result;
}

bool close(qreal a, qreal b, qreal tolerance) {
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}

void compareCase(const QJsonObject& fixture, QStringList& errors) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = render(fixture.value(QStringLiteral("source")).toString(), &entry);
  if (fixture.value(QStringLiteral("status")).toString() == QLatin1String("error")) {
    if (entry.status == editor::MermaidRenderStatus::Ready)
      errors << id + QStringLiteral(": expected parse error");
    return;
  }
  if (!scene) {
    errors << id + QStringLiteral(": native render failed: ") + entry.errorMessage;
    return;
  }
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject attrs = expected.value(QStringLiteral("root")).toObject()
                                .value(QStringLiteral("attrs")).toObject();
  const QVector<qreal> viewBox = numbers(attrs.value(QStringLiteral("viewBox")).toString());
  const qreal tol = id.contains(QStringLiteral("hand-drawn")) ? 4.0 : 1.25;
  if (viewBox.size() == 4 &&
      (!close(scene->bounds.x(), viewBox[0], tol) ||
       !close(scene->bounds.y(), viewBox[1], tol) ||
       !close(scene->bounds.width(), viewBox[2], tol) ||
       !close(scene->bounds.height(), viewBox[3], tol)))
    errors << id + QStringLiteral(": viewBox mismatch native=%1,%2 %3x%4 font=%5 html=%6")
                       .arg(scene->bounds.x()).arg(scene->bounds.y())
                       .arg(scene->bounds.width()).arg(scene->bounds.height())
                       .arg(scene->style.fontSize).arg(scene->config.htmlLabels) +
              QStringLiteral(" item0h=%1 titleh=%2")
                  .arg(scene->items.isEmpty() ? -1.0 : scene->items.first().localBounds.height())
                  .arg(scene->items.isEmpty() ? -1.0 : scene->items.first().title.bounds.height());
  const QJsonArray sections = expected.value(QStringLiteral("sections")).toArray();
  const QJsonArray items = expected.value(QStringLiteral("items")).toArray();
  if (scene->sections.size() != sections.size())
    errors << id + QStringLiteral(": section count");
  if (scene->items.size() != items.size())
    errors << id + QStringLiteral(": item count %1 != %2")
                       .arg(scene->items.size()).arg(items.size());
  for (qsizetype i = 0; i < std::min(scene->sections.size(), sections.size()); ++i) {
    const QJsonObject sectionOracle = sections.at(i).toObject();
    const QJsonValue shapeValue = sectionOracle.value(QStringLiteral("shape"));
    const QJsonObject expectedShape = shapeValue.isObject()
        ? shapeValue.toObject().value(QStringLiteral("bbox")).toObject()
        : sectionOracle.value(QStringLiteral("bbox")).toObject();
    const QRectF actual = scene->sections.at(i).shapeBounds;
    if (!close(actual.width(), expectedShape.value(QStringLiteral("width")).toDouble(), tol) ||
        !close(actual.height(), expectedShape.value(QStringLiteral("height")).toDouble(), tol))
      errors << id + QStringLiteral(": section %1 shape %2x%3 != %4x%5")
                         .arg(i).arg(actual.width()).arg(actual.height())
                         .arg(expectedShape.value(QStringLiteral("width")).toDouble())
                         .arg(expectedShape.value(QStringLiteral("height")).toDouble());
  }
  for (qsizetype i = 0; i < std::min(scene->items.size(), items.size()); ++i) {
    const QJsonObject oracle = items.at(i).toObject();
    const QJsonObject expectedShape = oracle.value(QStringLiteral("shape")).toObject()
                                          .value(QStringLiteral("bbox")).toObject();
    const QRectF actual = scene->items.at(i).localBounds;
    if (!close(actual.x(), expectedShape.value(QStringLiteral("x")).toDouble(), tol) ||
        !close(actual.y(), expectedShape.value(QStringLiteral("y")).toDouble(), tol) ||
        !close(actual.width(), expectedShape.value(QStringLiteral("width")).toDouble(), tol) ||
        !close(actual.height(), expectedShape.value(QStringLiteral("height")).toDouble(), tol))
      errors << id + QStringLiteral(": item %1 shape").arg(i);
    const QVector<qreal> transform = numbers(
        oracle.value(QStringLiteral("attrs")).toObject()
            .value(QStringLiteral("transform")).toString());
    if (transform.size() >= 2 &&
        (!close(scene->items.at(i).position.x(), transform[0], tol) ||
         !close(scene->items.at(i).position.y(), transform[1], tol)))
      errors << id + QStringLiteral(": item %1 transform %2,%3 != %4,%5")
                         .arg(i).arg(scene->items.at(i).position.x())
                         .arg(scene->items.at(i).position.y())
                         .arg(transform[0]).arg(transform[1]);
  }
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Kanban geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("d793609066e73c77055b6467650ecf649d1daadb87310ab20895b42f171c81b8"),
          QStringLiteral("Kanban geometry fixture provenance drifted"));
  QStringList errors;
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray())
    compareCase(value.toObject(), errors);
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));
  return 0;
}
