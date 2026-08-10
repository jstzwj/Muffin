#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/treeview/TreeViewScene.h"

#include <QCryptographicHash>
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
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.001) {
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
qreal attrNumber(const QJsonObject& attrs, const char* name) {
  return attrs.value(QLatin1String(name)).toString().toDouble();
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected TreeView geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("b7cf9285a65e034943eb2e62e1d8312568c6092eb671392b6e96c788d71339a6"),
          QStringLiteral("TreeView geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("ec9783ecf6be9b126d9968fea67ef849697c82347933ab331369244f5d1b497f"),
          QStringLiteral("TreeView geometry fixture provenance changed"));
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("TreeView Mermaid version drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 10, QStringLiteral("Expected ten geometry cases"));

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
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox tokens"));
    near(scene->bounds.x(), viewBox[0], id + QStringLiteral("/viewBox/x"));
    near(scene->bounds.y(), viewBox[1], id + QStringLiteral("/viewBox/y"));
    near(scene->bounds.width(), viewBox[2], id + QStringLiteral("/viewBox/width"), 2.0);
    near(scene->bounds.height(), viewBox[3], id + QStringLiteral("/viewBox/height"));

    const QJsonArray labels = expected.value(QStringLiteral("labels")).toArray();
    require(scene->nodes.size() == labels.size(), id + QStringLiteral("/node count"));
    for (qsizetype i = 0; i < labels.size(); ++i) {
      const QString path = QStringLiteral("%1/node/%2").arg(id).arg(i);
      const treeview::TreeViewNodeGeometry& node = scene->nodes.at(i);
      const QJsonObject labelGroup = labels.at(i).toObject();
      const QJsonObject label = labelGroup.value(QStringLiteral("label")).toObject();
      const QJsonObject attrs = label.value(QStringLiteral("attrs")).toObject();
      require(node.label.text == label.value(QStringLiteral("value")).toString(),
              path + QStringLiteral("/text"));
      require(node.label.cssClass == attrs.value(QStringLiteral("class")).toString(),
              path + QStringLiteral("/class"));
      near(node.label.position.x(), attrNumber(attrs, "x"), path + QStringLiteral("/x"));
      near(node.label.position.y(), attrNumber(attrs, "y"), path + QStringLiteral("/y"));
      near(node.label.layoutWidth,
           label.value(QStringLiteral("bbox")).toObject()
               .value(QStringLiteral("width")).toDouble(),
           path + QStringLiteral("/layout-width"), 2.0);

      const QJsonValue descriptionValue = labelGroup.value(QStringLiteral("description"));
      require(node.hasDescription == descriptionValue.isObject(),
              path + QStringLiteral("/description presence"));
      if (descriptionValue.isObject()) {
        const QJsonObject description = descriptionValue.toObject();
        const QJsonObject descriptionAttrs = description.value(QStringLiteral("attrs")).toObject();
        require(node.description.text == description.value(QStringLiteral("value")).toString(),
                path + QStringLiteral("/description text"));
        near(node.description.position.x(), attrNumber(descriptionAttrs, "x"),
             path + QStringLiteral("/description x"), 2.0);
        near(node.description.position.y(), attrNumber(descriptionAttrs, "y"),
             path + QStringLiteral("/description y"));
        near(node.description.layoutWidth,
             description.value(QStringLiteral("bbox")).toObject()
                 .value(QStringLiteral("width")).toDouble(),
             path + QStringLiteral("/description width"), 2.0);
      }

      const QJsonValue highlightValue = labelGroup.value(QStringLiteral("highlight"));
      require(node.highlighted == highlightValue.isObject(),
              path + QStringLiteral("/highlight presence"));
      if (highlightValue.isObject()) {
        const QJsonObject highlightAttrs =
            highlightValue.toObject().value(QStringLiteral("attrs")).toObject();
        near(node.highlightRect.x(), attrNumber(highlightAttrs, "x"),
             path + QStringLiteral("/highlight x"));
        near(node.highlightRect.y(), attrNumber(highlightAttrs, "y"),
             path + QStringLiteral("/highlight y"));
        near(node.highlightRect.width(), attrNumber(highlightAttrs, "width"),
             path + QStringLiteral("/highlight width"), 2.0);
        near(node.highlightRect.height(), attrNumber(highlightAttrs, "height"),
             path + QStringLiteral("/highlight height"));
      }
    }

    const QJsonArray lines = expected.value(QStringLiteral("lines")).toArray();
    require(scene->lines.size() == lines.size(), id + QStringLiteral("/line count"));
    for (qsizetype i = 0; i < lines.size(); ++i) {
      const QString path = QStringLiteral("%1/line/%2").arg(id).arg(i);
      const treeview::TreeViewLineGeometry& line = scene->lines.at(i);
      const QJsonObject attrs = lines.at(i).toObject()
                                    .value(QStringLiteral("attrs")).toObject();
      require(line.x1Attribute == attrs.value(QStringLiteral("x1")).toString(),
              path + QStringLiteral("/x1 attr"));
      require(line.y1Attribute == attrs.value(QStringLiteral("y1")).toString(),
              path + QStringLiteral("/y1 attr"));
      require(line.x2Attribute == attrs.value(QStringLiteral("x2")).toString(),
              path + QStringLiteral("/x2 attr"));
      require(line.y2Attribute == attrs.value(QStringLiteral("y2")).toString(),
              path + QStringLiteral("/y2 attr"));
      require(line.strokeWidthAttribute ==
                  attrs.value(QStringLiteral("stroke-width")).toString(),
              path + QStringLiteral("/stroke width attr"));
    }

    const QJsonArray defs = expected.value(QStringLiteral("defs")).toArray();
    require(scene->iconDefs.size() == defs.size(), id + QStringLiteral("/icon defs"));
    for (qsizetype i = 0; i < defs.size(); ++i) {
      const QString expectedId = defs.at(i).toObject()
                                     .value(QStringLiteral("attrs")).toObject()
                                     .value(QStringLiteral("id")).toString();
      QString iconId = scene->iconDefs.at(i);
      iconId.replace(QLatin1Char(':'), QLatin1Char('-'));
      require(expectedId.endsWith(iconId),
              id + QStringLiteral("/icon def id"));
    }

    const QJsonObject metadata = expected.value(QStringLiteral("metadata")).toObject();
    require(entry.metadata.title.isEmpty(), id + QStringLiteral("/visual title"));
    require(entry.metadata.accessibleTitle == metadata.value(QStringLiteral("title")).toString(),
            id + QStringLiteral("/accessible title"));
    require(entry.metadata.accessibleDescription == metadata.value(QStringLiteral("desc")).toString(),
            id + QStringLiteral("/accessible description"));
    require(entry.naturalSize == QSize(qRound(viewBox[2]), qRound(viewBox[3])),
            QStringLiteral("%1/natural size %2x%3 (scene %6) != %4x%5")
                .arg(id).arg(entry.naturalSize.width()).arg(entry.naturalSize.height())
                .arg(qRound(viewBox[2])).arg(qRound(viewBox[3]))
                .arg(scene->bounds.width(), 0, 'g', 17));
  }
  std::fprintf(stderr, "TreeView geometry oracle: 10/10 passed\n");
  return 0;
}
