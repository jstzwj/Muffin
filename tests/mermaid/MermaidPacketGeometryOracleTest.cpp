#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/packet/PacketScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

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

std::shared_ptr<const packet::PacketScene> renderScene(
    const QString& source, editor::MermaidRenderEntry* output = nullptr) {
  editor::MermaidRenderCache cache;
  const editor::MermaidRenderEntry entry =
      cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("Packet render failed: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const packet::PacketScene>(entry.scene);
  require(bool(scene), QStringLiteral("Packet entry has wrong scene type"));
  if (output) *output = entry;
  return scene;
}

QVector<const packet::PacketBlockGeometry*> blocks(
    const packet::PacketScene& scene) {
  QVector<const packet::PacketBlockGeometry*> result;
  for (const auto& word : scene.words)
    for (const auto& block : word.blocks) result.append(&block);
  return result;
}

QVector<const packet::PacketTextGeometry*> texts(
    const packet::PacketScene& scene) {
  QVector<const packet::PacketTextGeometry*> result;
  for (const auto& word : scene.words) {
    for (const auto& block : word.blocks) {
      result.append(&block.labelText);
      for (const auto& bit : block.bitTexts) result.append(&bit);
    }
  }
  result.append(&scene.titleText);
  return result;
}

QString anchor(packet::PacketTextAnchor value) {
  if (value == packet::PacketTextAnchor::Middle) return QStringLiteral("middle");
  if (value == packet::PacketTextAnchor::End) return QStringLiteral("end");
  return QStringLiteral("start");
}

QString baseline(packet::PacketTextBaseline value) {
  return value == packet::PacketTextBaseline::Middle ? QStringLiteral("middle")
                                                      : QStringLiteral("auto");
}

void compareCase(const QJsonObject& fixture, QStringList& errors) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene(
      fixture.value(QStringLiteral("source")).toString(), &entry);
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject rootAttrs = root.value(QStringLiteral("attrs")).toObject();
  if (scene->viewBoxAttribute != rootAttrs.value(QStringLiteral("viewBox")).toString())
    errors << id + QStringLiteral("/viewBox: '") + scene->viewBoxAttribute +
                  QStringLiteral("' != '") +
                  rootAttrs.value(QStringLiteral("viewBox")).toString() +
                  QLatin1Char('\'');
  const QJsonObject client = root.value(QStringLiteral("clientBox")).toObject();
  if (std::fabs(scene->bounds.width() -
                client.value(QStringLiteral("width")).toDouble()) > 0.001 ||
      std::fabs(scene->bounds.height() -
                client.value(QStringLiteral("height")).toDouble()) > 0.001)
    errors << id + QStringLiteral("/replaced-element viewport mismatch");
  const bool expectedMax = rootAttrs.value(QStringLiteral("width")).toString() ==
                           QLatin1String("100%");
  if (scene->useMaxWidth != expectedMax || entry.metadata.svgUseMaxWidth != expectedMax)
    errors << id + QStringLiteral("/useMaxWidth mismatch");

  const QVector<const packet::PacketBlockGeometry*> actualBlocks = blocks(*scene);
  const QJsonArray expectedRects = expected.value(QStringLiteral("rects")).toArray();
  if (actualBlocks.size() != expectedRects.size()) {
    errors << id + QStringLiteral("/rect count %1 != %2")
                       .arg(actualBlocks.size()).arg(expectedRects.size());
  } else {
    for (qsizetype i = 0; i < actualBlocks.size(); ++i) {
      const auto& actual = *actualBlocks.at(i);
      const QJsonObject attrs = expectedRects.at(i).toObject()
                                    .value(QStringLiteral("attrs")).toObject();
      const QString prefix = id + QStringLiteral("/rect/%1/").arg(i);
      if (actual.xAttribute != attrs.value(QStringLiteral("x")).toString())
        errors << prefix + QStringLiteral("x");
      if (actual.yAttribute != attrs.value(QStringLiteral("y")).toString())
        errors << prefix + QStringLiteral("y");
      if (actual.widthAttribute != attrs.value(QStringLiteral("width")).toString())
        errors << prefix + QStringLiteral("width");
      if (actual.heightAttribute != attrs.value(QStringLiteral("height")).toString())
        errors << prefix + QStringLiteral("height");
    }
  }

  const QVector<const packet::PacketTextGeometry*> actualTexts = texts(*scene);
  const QJsonArray expectedTexts = expected.value(QStringLiteral("texts")).toArray();
  if (actualTexts.size() != expectedTexts.size()) {
    errors << id + QStringLiteral("/text count %1 != %2")
                       .arg(actualTexts.size()).arg(expectedTexts.size());
  } else {
    for (qsizetype i = 0; i < actualTexts.size(); ++i) {
      const auto& actual = *actualTexts.at(i);
      const QJsonObject oracle = expectedTexts.at(i).toObject();
      const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
      const QString prefix = id + QStringLiteral("/text/%1/").arg(i);
      if (actual.cssClass != oracle.value(QStringLiteral("class")).toString())
        errors << prefix + QStringLiteral("class");
      if (actual.text != oracle.value(QStringLiteral("text")).toString())
        errors << prefix + QStringLiteral("text");
      if (actual.xAttribute != attrs.value(QStringLiteral("x")).toString())
        errors << prefix + QStringLiteral("x");
      if (actual.yAttribute != attrs.value(QStringLiteral("y")).toString())
        errors << prefix + QStringLiteral("y");
      if (anchor(actual.anchor) !=
          attrs.value(QStringLiteral("text-anchor")).toString())
        errors << prefix + QStringLiteral("anchor");
      if (baseline(actual.baseline) !=
          attrs.value(QStringLiteral("dominant-baseline")).toString())
        errors << prefix + QStringLiteral("baseline");
    }
  }

  int previous = -1;
  for (const auto& word : scene->words) {
    for (const auto& block : word.blocks) {
      require(block.paintOrder > previous, id + QStringLiteral("/paint order rect"));
      previous = block.paintOrder;
      require(block.labelText.paintOrder > previous,
              id + QStringLiteral("/paint order label"));
      previous = block.labelText.paintOrder;
      for (const auto& bit : block.bitTexts) {
        require(bit.paintOrder > previous,
                id + QStringLiteral("/paint order bit"));
        previous = bit.paintOrder;
      }
    }
  }
  require(scene->titleText.paintOrder > previous,
          id + QStringLiteral("/paint order title"));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected packet geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  require(parseError.error == QJsonParseError::NoError,
          QStringLiteral("Packet geometry JSON: ") + parseError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("29e6e2c64c06cbf7e4cc48907ba41ba7eb3313d6b24969e6a150a0c1aa693a8e"),
          QStringLiteral("Packet geometry fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 29, QStringLiteral("Packet geometry case count"));
  QStringList errors;
  for (const QJsonValue& value : cases) compareCase(value.toObject(), errors);
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));
  std::fprintf(stderr, "Packet geometry parity: 29/29 passed\n");
  return 0;
}
