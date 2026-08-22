#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/treeview/TreeViewScene.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

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
QByteArray fileSha(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage decode(const QString& url) {
  QImage image;
  const qsizetype comma = url.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()), "PNG");
  return image;
}
qreal alphaIou(const QImage& a, const QImage& b) {
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const bool aa = a.pixelColor(x, y).alpha() >= 32;
      const bool bb = b.pixelColor(x, y).alpha() >= 32;
      intersection += aa && bb;
      united += aa || bb;
    }
  return united ? qreal(intersection) / united : 1.0;
}
qreal rgbaSimilarity(const QImage& a, const QImage& b) {
  qreal difference = 0.0;
  int count = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const QColor actual = a.pixelColor(x, y);
      const QColor expected = b.pixelColor(x, y);
      if (actual.alpha() < 32 && expected.alpha() < 32) continue;
      ++count;
      const auto premultiplied = [](int channel, int alpha) {
        return channel * alpha / 255;
      };
      difference += std::abs(premultiplied(actual.red(), actual.alpha()) -
                             premultiplied(expected.red(), expected.alpha()));
      difference += std::abs(premultiplied(actual.green(), actual.alpha()) -
                             premultiplied(expected.green(), expected.alpha()));
      difference += std::abs(premultiplied(actual.blue(), actual.alpha()) -
                             premultiplied(expected.blue(), expected.alpha()));
      difference += std::abs(actual.alpha() - expected.alpha());
    }
  return count ? 1.0 - difference / (count * 4.0 * 255.0) : 1.0;
}
QImage isolatedLabelImage(treeview::TreeViewScene scene) {
  scene.style.rootTextColor = QStringLiteral("#000000");
  for (treeview::TreeViewLineGeometry& line : scene.lines)
    line.visible = false;
  for (treeview::TreeViewNodeGeometry& node : scene.nodes) {
    node.highlightVisible = false;
    node.description.visible = false;
    node.label.fill = QStringLiteral("#000000");
    node.label.opacity = 1.0;
  }
  const QRectF bounds = scene.renderBounds();
  QImage image(qRound(bounds.width()), qRound(bounds.height()),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-bounds.left(), -bounds.top());
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}
}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_MACOS)
  // The raster goldens embed the Windows golden host's font stack and
  // rasterization; macOS (SF/Helvetica) resolves different faces with
  // different metrics. Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected TreeView pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("752cc259261abb0cd9ff03a84af4017847deede8570388a4b3ea484bdbc34c11"),
          QStringLiteral("TreeView pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("87c03fe0de09c4b4d7b420def1a458b28454e4e492de13109073ddae59778cae"),
          QStringLiteral("TreeView pixel fixture changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Expected three TreeView pixel cases"));
  const QDir dir = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString referencePath = dir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(fileSha(referencePath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/browser PNG hash"));
    const QImage reference(referencePath);
    const QImage native = decode(editor::MermaidRenderCache::renderMermaidSourceToPng(
                                     fixture.value(QStringLiteral("source")).toString(), 1.0)
                                     .dataUrl);
    require(!reference.isNull() && !native.isNull(), id + QStringLiteral("/decode"));
    if (qEnvironmentVariableIsSet("MUFFIN_SAVE_NATIVE"))
      native.save(QStringLiteral("native-treeview-%1.png").arg(id));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(reference.width()).arg(reference.height()));
    const qreal iou = alphaIou(native, reference);
    const qreal rgba = rgbaSimilarity(native, reference);
    std::fprintf(stderr, "%s alphaIoU=%.6f rgba=%.6f\n", qPrintable(id), iou, rgba);
    const bool iconCase = id == QLatin1String("built-in-icons");
    const qreal minimumIou = iconCase ? 0.82 : 0.95;
    require(iou >= minimumIou, id + QStringLiteral("/alpha IoU"));
    require(rgba >= 0.96, id + QStringLiteral("/foreground RGBA"));
    if (iconCase) {
      const QJsonObject contract =
          fixture.value(QStringLiteral("iconContract")).toObject();
      require(contract.value(QStringLiteral("uses")).toArray().isEmpty(),
              id + QStringLiteral("/strict output must strip icon uses"));
      const QJsonArray labels = contract.value(QStringLiteral("labels")).toArray();
      const QStringList expectedLabels = {QStringLiteral("/"),
          QStringLiteral("root"), QStringLiteral("file.txt"),
          QStringLiteral("hidden"), QStringLiteral("explicit")};
      const QStringList expectedXs = {QStringLiteral("23"), QStringLiteral("38"),
          QStringLiteral("53"), QStringLiteral("35"), QStringLiteral("53")};
      require(labels.size() == expectedLabels.size(), id + QStringLiteral("/labels"));
      for (qsizetype i = 0; i < labels.size(); ++i) {
        const QJsonObject label = labels.at(i).toObject();
        require(label.value(QStringLiteral("value")).toString() == expectedLabels.at(i) &&
                    label.value(QStringLiteral("x")).toString() == expectedXs.at(i) &&
                    label.value(QStringLiteral("y")).toString() ==
                        QString::number(16 + 32 * i) &&
                    !label.value(QStringLiteral("hasIconUse")).toBool(),
                id + QStringLiteral("/label icon reservation drifted"));
      }
      const QJsonArray defs = contract.value(QStringLiteral("defs")).toArray();
      require(defs.size() == 2, id + QStringLiteral("/two built-in defs"));
      const QStringList iconNames = {QStringLiteral("folder"), QStringLiteral("file")};
      const QStringList paths = {
          QStringLiteral("M10.59 4.59A2 2 0 0 0 9.17 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.17z"),
          QStringLiteral("M6 2a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8.83a2 2 0 0 0-.59-1.42l-4.82-4.82A2 2 0 0 0 13.17 2H6Zm7.5 1.9l4.6 4.6h-3.6a1 1 0 0 1-1-1V3.9Z")};
      for (qsizetype i = 0; i < defs.size(); ++i) {
        const QJsonObject definition = defs.at(i).toObject();
        const QJsonObject svg = definition.value(QStringLiteral("svg")).toObject();
        const QJsonObject path = definition.value(QStringLiteral("path")).toObject();
        require(definition.value(QStringLiteral("id")).toString().endsWith(
                    QStringLiteral("mermaid-treeview-") + iconNames.at(i)) &&
                    svg.value(QStringLiteral("width")).toString() == QLatin1String("14") &&
                    svg.value(QStringLiteral("height")).toString() == QLatin1String("14") &&
                    svg.value(QStringLiteral("viewBox")).toString() ==
                        QLatin1String("0 0 24 24") &&
                    path.value(QStringLiteral("fill")).toString() ==
                        QLatin1String("currentColor") &&
                    path.value(QStringLiteral("d")).toString() == paths.at(i) &&
                    path.value(QStringLiteral("fill-rule")).toString() ==
                        (i == 1 ? QLatin1String("evenodd") : QLatin1String("")) &&
                    path.value(QStringLiteral("clip-rule")).toString() ==
                        (i == 1 ? QLatin1String("evenodd") : QLatin1String("")),
                id + QStringLiteral("/built-in def drifted"));
      }

      editor::MermaidRenderCache cache;
      const QString source = fixture.value(QStringLiteral("source")).toString();
      const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
      const auto scene =
          std::dynamic_pointer_cast<const treeview::TreeViewScene>(entry.scene);
      require(bool(scene), id + QStringLiteral("/native scene"));
      require(scene->iconDefs ==
                  QStringList{QStringLiteral("mermaid-treeview:folder"),
                              QStringLiteral("mermaid-treeview:file")},
              id + QStringLiteral("/native defs"));
      const QVector<bool> expectedReserved = {true, true, true, false, true};
      const QStringList expectedNodeIcons = {
          QStringLiteral("mermaid-treeview:folder"),
          QStringLiteral("mermaid-treeview:folder"),
          QStringLiteral("mermaid-treeview:file"), QString(),
          QStringLiteral("mermaid-treeview:folder")};
      require(scene->nodes.size() == expectedReserved.size(),
              id + QStringLiteral("/native nodes"));
      for (qsizetype i = 0; i < scene->nodes.size(); ++i)
        require(scene->nodes.at(i).iconReserved == expectedReserved.at(i) &&
                    scene->nodes.at(i).iconName == expectedNodeIcons.at(i),
                id + QStringLiteral("/native icon reservation"));

      const QString labelMaskPath = dir.filePath(
          fixture.value(QStringLiteral("labelMaskFile")).toString());
      require(fileSha(labelMaskPath) ==
                  fixture.value(QStringLiteral("labelMaskSha256")).toString().toLatin1(),
              id + QStringLiteral("/browser label mask hash"));
      const QImage browserLabels(labelMaskPath);
      const QImage nativeLabels = isolatedLabelImage(*scene);
      if (qEnvironmentVariableIsSet("MUFFIN_SAVE_NATIVE"))
        nativeLabels.save(QStringLiteral("native-treeview-built-in-icons-label-mask.png"));
      const qreal labelIou = alphaIou(nativeLabels, browserLabels);
      std::fprintf(stderr, "%s isolated labels IoU=%.6f\n", qPrintable(id), labelIou);
      require(labelIou >= 0.80, id + QStringLiteral("/isolated label IoU"));
    }
  }
  return 0;
}
