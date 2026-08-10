#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/treeview/TreeViewScene.h"

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
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 1.25) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}
QVector<qreal> numbers(const QString& text) {
  QVector<qreal> result;
  for (const QString& part : text.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    result.append(part.toDouble());
  return result;
}
QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?)"));
  const auto match = rgb.match(value);
  if (match.hasMatch()) {
    QColor result(match.captured(1).toInt(), match.captured(2).toInt(),
                  match.captured(3).toInt());
    if (!match.captured(4).isEmpty()) result.setAlphaF(match.captured(4).toDouble());
    return result;
  }
  return color::toQColor(value);
}
void sameColor(const QString& native, const QString& browser,
               const QString& path) {
  const QColor a = computedColor(native);
  const QColor b = computedColor(browser);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          path + QStringLiteral(": ") + native + QStringLiteral(" != ") + browser);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected TreeView config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("5e30e11120b8f0dc42c200ea652dafb045a143f080c33816d4b025aac993a577"),
          QStringLiteral("TreeView config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("f48b1fb3eb4234176c7b44d19e3cc083ad4583756bba2b9aac8afd26f1f01203"),
          QStringLiteral("TreeView config fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 24, QStringLiteral("Expected 24 config cases"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    auto scene = std::dynamic_pointer_cast<const treeview::TreeViewScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected TreeViewScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox[0], id + QStringLiteral("/x"), 0.001);
    near(scene->bounds.width(), viewBox[2], id + QStringLiteral("/width"));
    near(scene->bounds.height(), viewBox[3], id + QStringLiteral("/height"), 0.001);
    const bool expectedMaxWidth = rootAttrs.value(QStringLiteral("width")).toString() ==
                                  QLatin1String("100%");
    require(scene->useMaxWidth == expectedMaxWidth,
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray labels = expected.value(QStringLiteral("labels")).toArray();
    require(scene->nodes.size() == labels.size(), id + QStringLiteral("/labels"));
    for (qsizetype i = 0; i < labels.size(); ++i) {
      const QJsonObject expectedLabel = labels.at(i).toObject()
                                            .value(QStringLiteral("label")).toObject();
      const QJsonObject computed = expectedLabel.value(QStringLiteral("computed")).toObject();
      const auto& actual = scene->nodes.at(i).label;
      sameColor(actual.fill, computed.value(QStringLiteral("fill")).toString(),
                QStringLiteral("%1/label/%2/fill").arg(id).arg(i));
      near(actual.fontSize,
           computed.value(QStringLiteral("fontSize")).toString().chopped(2).toDouble(),
           QStringLiteral("%1/label/%2/fontSize").arg(id).arg(i), 0.001);
      near(actual.position.x(),
           expectedLabel.value(QStringLiteral("attrs")).toObject()
               .value(QStringLiteral("x")).toString().toDouble(),
           QStringLiteral("%1/label/%2/x").arg(id).arg(i), 0.001);

      const QJsonValue descriptionValue = labels.at(i).toObject()
                                               .value(QStringLiteral("description"));
      if (descriptionValue.isObject()) {
        sameColor(scene->nodes.at(i).description.fill,
                  descriptionValue.toObject().value(QStringLiteral("computed")).toObject()
                      .value(QStringLiteral("fill")).toString(),
                  QStringLiteral("%1/description/%2/fill").arg(id).arg(i));
      }
      const QJsonValue highlightValue = labels.at(i).toObject()
                                             .value(QStringLiteral("highlight"));
      if (highlightValue.isObject()) {
        const QJsonObject highlightComputed = highlightValue.toObject()
                                                  .value(QStringLiteral("computed")).toObject();
        sameColor(scene->style.highlightBg,
                  highlightComputed.value(QStringLiteral("fill")).toString(),
                  id + QStringLiteral("/highlight fill"));
        sameColor(scene->style.highlightStroke,
                  highlightComputed.value(QStringLiteral("stroke")).toString(),
                  id + QStringLiteral("/highlight stroke"));
      }
    }
    const QJsonArray lines = expected.value(QStringLiteral("lines")).toArray();
    require(scene->lines.size() == lines.size(), id + QStringLiteral("/lines"));
    if (!lines.isEmpty()) {
      const QJsonObject lineComputed = lines.at(0).toObject()
                                           .value(QStringLiteral("computed")).toObject();
      sameColor(scene->style.lineColor,
                lineComputed.value(QStringLiteral("stroke")).toString(),
                id + QStringLiteral("/line color"));
    }
    require(scene->iconDefs.size() == expected.value(QStringLiteral("defs")).toArray().size(),
            id + QStringLiteral("/defs"));
  }
  std::fprintf(stderr, "TreeView config parity: 24/24 passed\n");
  return 0;
}
