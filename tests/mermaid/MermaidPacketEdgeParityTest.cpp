#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFile>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
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

struct Rendered {
  std::shared_ptr<const packet::PacketScene> scene;
  editor::MermaidRenderEntry entry;
};

Rendered render(const QString& source) {
  editor::MermaidRenderCache cache;
  Rendered result;
  result.entry = cache.getSync(cache.makeKey(source), source);
  require(result.entry.status == editor::MermaidRenderStatus::Ready &&
              result.entry.scene,
          QStringLiteral("Packet edge render failed: ") +
              result.entry.errorMessage);
  result.scene =
      std::dynamic_pointer_cast<const packet::PacketScene>(result.entry.scene);
  require(bool(result.scene), QStringLiteral("Packet edge wrong scene type"));
  return result;
}

QColor browserColor(const QString& value) {
  QString text = value.trimmed();
  if (text.startsWith(QLatin1String("rgb(")) && text.endsWith(QLatin1Char(')'))) {
    text = text.mid(4, text.size() - 5);
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() == 3)
      return QColor(parts.at(0).trimmed().toInt(), parts.at(1).trimmed().toInt(),
                    parts.at(2).trimmed().toInt());
  }
  return color::toQColor(value);
}

void compareColor(QStringList& errors, const QString& actual,
                  const QString& expected, color::SvgPaintKind kind,
                  const QColor& inherited, const QString& path) {
  const color::SvgPaint paint = color::resolveSvgPaint(actual, kind, inherited);
  const QColor oracle = browserColor(expected);
  if (paint.none || !oracle.isValid() || paint.color.rgba() != oracle.rgba())
    errors << path + QStringLiteral(": paint mismatch '") + actual +
                  QStringLiteral("' vs '") + expected + QLatin1Char('\'');
}

void compareConfigCase(const QJsonObject& fixture, QStringList& errors) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const Rendered rendered =
      render(fixture.value(QStringLiteral("source")).toString());
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                    .value(QStringLiteral("attrs")).toObject();
  if (rendered.scene->viewBoxAttribute !=
      rootAttrs.value(QStringLiteral("viewBox")).toString())
    errors << id + QStringLiteral("/viewBox");
  const QJsonObject client = expected.value(QStringLiteral("root")).toObject()
                                 .value(QStringLiteral("clientBox")).toObject();
  if (std::fabs(rendered.scene->bounds.width() -
                client.value(QStringLiteral("width")).toDouble()) > 0.001 ||
      std::fabs(rendered.scene->bounds.height() -
                client.value(QStringLiteral("height")).toDouble()) > 0.001)
    errors << id + QStringLiteral("/client viewport");
  const bool maxWidth = rootAttrs.value(QStringLiteral("width")).toString() ==
                        QLatin1String("100%");
  if (rendered.scene->useMaxWidth != maxWidth ||
      rendered.entry.metadata.svgUseMaxWidth != maxWidth)
    errors << id + QStringLiteral("/useMaxWidth");

  const QJsonArray rects = expected.value(QStringLiteral("rects")).toArray();
  const QJsonArray texts = expected.value(QStringLiteral("texts")).toArray();
  if (rendered.scene->words.isEmpty() || rects.isEmpty() || texts.isEmpty()) return;
  const auto& block = rendered.scene->words.front().blocks.front();
  const QColor inherited = browserColor(
      expected.value(QStringLiteral("root")).toObject()
          .value(QStringLiteral("computed")).toObject()
          .value(QStringLiteral("fill")).toString());
  const QJsonObject rectStyle = rects.at(0).toObject()
                                    .value(QStringLiteral("computed")).toObject();
  compareColor(errors, block.fill,
               rectStyle.value(QStringLiteral("fill")).toString(),
               color::SvgPaintKind::Fill, inherited,
               id + QStringLiteral("/blockFill"));
  compareColor(errors, block.stroke,
               rectStyle.value(QStringLiteral("stroke")).toString(),
               color::SvgPaintKind::Stroke, inherited,
               id + QStringLiteral("/blockStroke"));
  const auto textByClass = [&texts](const QString& cssClass) {
    for (const QJsonValue& value : texts) {
      const QJsonObject text = value.toObject();
      if (text.value(QStringLiteral("class")).toString() == cssClass)
        return text;
    }
    return QJsonObject{};
  };
  const QJsonObject labelStyle = textByClass(QStringLiteral("packetLabel"))
                                     .value(QStringLiteral("computed")).toObject();
  compareColor(errors, block.labelText.fill,
               labelStyle.value(QStringLiteral("fill")).toString(),
               color::SvgPaintKind::Text, inherited,
               id + QStringLiteral("/labelFill"));
  const qreal expectedSize =
      labelStyle.value(QStringLiteral("fontSize")).toString()
          .chopped(2).toDouble();
  const qreal fontTolerance =
      id == QLatin1String("font-fallback-ex-ch") ? 0.15 : 0.001;
  if (std::fabs(block.labelText.fontSize - expectedSize) > fontTolerance)
    errors << id + QStringLiteral("/labelFontSize: %1 != %2")
                       .arg(block.labelText.fontSize, 0, 'g', 17)
                       .arg(expectedSize, 0, 'g', 17);
  const QJsonObject titleStyle = textByClass(QStringLiteral("packetTitle"))
                                     .value(QStringLiteral("computed")).toObject();
  compareColor(errors, rendered.scene->titleText.fill,
               titleStyle.value(QStringLiteral("fill")).toString(),
               color::SvgPaintKind::Text, inherited,
               id + QStringLiteral("/titleFill"));
  const qreal expectedTitleSize =
      titleStyle.value(QStringLiteral("fontSize")).toString().chopped(2).toDouble();
  if (std::fabs(rendered.scene->titleText.fontSize - expectedTitleSize) >
      fontTolerance)
    errors << id + QStringLiteral("/titleFontSize: %1 != %2")
                       .arg(rendered.scene->titleText.fontSize, 0, 'g', 17)
                       .arg(expectedTitleSize, 0, 'g', 17);
  const auto compareBit = [&](const QString& cssClass, qsizetype index,
                              const QString& path) {
    const QJsonObject expectedText = textByClass(cssClass);
    if (expectedText.isEmpty() || block.bitTexts.size() <= index) return;
    const QJsonObject used = expectedText.value(QStringLiteral("computed")).toObject();
    compareColor(errors, block.bitTexts.at(index).fill,
                 used.value(QStringLiteral("fill")).toString(),
                 color::SvgPaintKind::Text, inherited, id + path + QStringLiteral("/fill"));
    const qreal size = used.value(QStringLiteral("fontSize")).toString()
                           .chopped(2).toDouble();
    if (std::fabs(block.bitTexts.at(index).fontSize - size) > 0.001)
      errors << id + path + QStringLiteral("/fontSize");
  };
  compareBit(QStringLiteral("packetByte start"), 0,
             QStringLiteral("/startByte"));
  compareBit(QStringLiteral("packetByte end"), 1,
             QStringLiteral("/endByte"));
  const qreal expectedStrokeWidth =
      rectStyle.value(QStringLiteral("strokeWidth")).toString()
          .chopped(2).toDouble();
  if (std::fabs(block.strokeWidth - expectedStrokeWidth) > 0.001)
    errors << id + QStringLiteral("/blockStrokeWidth");
}

QImage paintWide(const packet::PacketScene& scene, QSize size) {
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}

int opaquePixels(const QImage& image, const QRect& rect) {
  int result = 0;
  const QRect clipped = rect.intersected(image.rect());
  for (int y = clipped.top(); y <= clipped.bottom(); ++y)
    for (int x = clipped.left(); x <= clipped.right(); ++x)
      result += image.pixelColor(x, y).alpha() > 16;
  return result;
}

int rgbaDiffPixels(const QImage& lhs, const QImage& rhs) {
  require(lhs.size() == rhs.size(),
          QStringLiteral("Packet RGBA comparison size mismatch"));
  int result = 0;
  for (int y = 0; y < lhs.height(); ++y)
    for (int x = 0; x < lhs.width(); ++x)
      result += lhs.pixelColor(x, y).rgba() != rhs.pixelColor(x, y).rgba();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected packet config fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("eb396b8c27a3cdbf13ea819aedfae72899dd735baf9bab2c6b8fb489cc0f9d5e") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Packet config fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 49, QStringLiteral("Packet config case count"));
  QStringList errors;
  for (const QJsonValue& value : cases)
    compareConfigCase(value.toObject(), errors);
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));

  const auto cross = render(QStringLiteral("packet-beta\n0-39: \"Across\""));
  require(cross.scene->words.size() == 2 &&
              cross.scene->words.at(0).blocks.front().start == 0.0 &&
              cross.scene->words.at(0).blocks.front().end == 31.0 &&
              cross.scene->words.at(1).blocks.front().start == 32.0 &&
              cross.scene->words.at(1).blocks.front().end == 39.0 &&
              cross.scene->words.at(0).blocks.front().label == QLatin1String("Across") &&
              cross.scene->words.at(1).blocks.front().label == QLatin1String("Across"),
          QStringLiteral("Packet cross-row split/label repeat drifted"));

  const auto fractional = render(QStringLiteral(
      "%%{init:{\"packet\":{\"bitsPerRow\":8.5,\"bitWidth\":10}}}%%\n"
      "packet-beta\n0-19: \"F\""));
  require(fractional.scene->words.size() == 3 &&
              fractional.scene->words.at(0).blocks.front().end == 7.5 &&
              fractional.scene->words.at(1).blocks.front().start == 8.5 &&
              fractional.scene->words.at(2).blocks.front().start == 17.0,
          QStringLiteral("Packet fractional bitsPerRow drifted"));

  const auto strings = render(QStringLiteral(
      "%%{init:{\"packet\":{\"rowHeight\":\"40\",\"bitWidth\":\"20\","
      "\"bitsPerRow\":\"8\",\"paddingX\":\"2\",\"paddingY\":\"3\"}}}%%\n"
      "packet-beta\n0-7: \"A\""));
  require(strings.scene->viewBoxAttribute == QLatin1String("0 0 162 80580") &&
              strings.scene->words.front().yAttribute == QLatin1String("0310") &&
              strings.scene->words.front().blocks.front().labelText.yAttribute ==
                  QLatin1String("031020"),
          QStringLiteral("Packet JavaScript string concatenation drifted"));

  const auto zero = render(QStringLiteral(
      "%%{init:{\"packet\":{\"rowHeight\":0,\"bitWidth\":0,\"paddingX\":0,"
      "\"paddingY\":0,\"showBits\":false}}}%%\npacket-beta\n0-31: \"A\""));
  require(zero.scene->viewBoxAttribute == QLatin1String("0 0 2 0") &&
              zero.scene->words.front().blocks.front().bitTexts.isEmpty(),
          QStringLiteral("Packet zero/showBits=false contract drifted"));

  const auto fallbackFont = render(QStringLiteral(
      "%%{init:{\"fontFamily\":\"DefinitelyMissing, Noto Sans\","
      "\"themeVariables\":{\"fontFamily\":\"DefinitelyMissing, Noto Sans\","
      "\"packet\":{\"labelFontSize\":\"10ex\","
      "\"titleFontSize\":\"10ch\"}}}}%%\n"
      "packet-beta\ntitle Fallback\n0-7: \"A\""));
  QFont fallbackMetricsFont;
  fallbackMetricsFont.setFamilies(
      {QStringLiteral("DefinitelyMissing"), QStringLiteral("Noto Sans")});
  fallbackMetricsFont.setPixelSize(16);
  fallbackMetricsFont.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF fallbackMetrics(fallbackMetricsFont);
  require(fallbackFont.scene->style.fontFamily ==
                  QLatin1String("DefinitelyMissing, Noto Sans") &&
              std::fabs(fallbackFont.scene->words.front()
                            .blocks.front()
                            .labelText.fontSize -
                        fallbackMetrics.xHeight() * 10.0) < 0.001 &&
              std::fabs(fallbackFont.scene->titleText.fontSize -
                        fallbackMetrics.horizontalAdvance(QChar('0')) * 10.0) <
                  0.001,
          QStringLiteral("Packet ex/ch metrics ignored the CSS fallback list"));

  const auto longLabel = render(QStringLiteral(
      "packet-beta\n0: \"THIS IS AN EXTREMELY LONG LABEL THAT MUST CLIP\""));
  const int width = qCeil(longLabel.scene->bounds.width()) + 300;
  const int height = qMax(100, qCeil(longLabel.scene->bounds.height()));
  const QImage clipped = paintWide(*longLabel.scene, QSize(width, height));
  require(opaquePixels(clipped,
                       QRect(qCeil(longLabel.scene->bounds.right()) + 1, 0,
                             299, height)) == 0,
          QStringLiteral("Packet fixed root viewport did not clip label overflow"));

  const packet::PacketData cappedData = packet::PacketDiagram::parse(
      QStringLiteral("packet-beta\n+320032: \"B\""), QJsonValue(32.0));
  require(cappedData.words.size() == 10000,
          QStringLiteral("Packet 10k renderer resource cap drifted"));

  const Rendered inheritedNone = render(QStringLiteral(
      "%%{init:{\"themeVariables\":{\"textColor\":\"none\","
      "\"packet\":{\"labelColor\":\"inherit\",\"titleColor\":\"\"}},"
      "\"packet\":{\"showBits\":false}}}%%\n"
      "packet-beta\ntitle T\n0: \"A\""));
  require(inheritedNone.scene->style.inheritedColor == QLatin1String("none") &&
              inheritedNone.scene->words.front().blocks.front().labelText.fill ==
                  QLatin1String("inherit") &&
              inheritedNone.scene->titleText.fill.isEmpty(),
          QStringLiteral("Packet root/child paint declarations were not preserved"));
  packet::PacketScene paintOnly = *inheritedNone.scene;
  paintOnly.words.front().blocks.front().fill = QStringLiteral("none");
  paintOnly.words.front().blocks.front().stroke = QStringLiteral("none");
  paintOnly.words.front().blocks.front().bitTexts.clear();
  require(opaquePixels(paintWide(paintOnly,
                                 QSize(qCeil(paintOnly.bounds.width()),
                                       qCeil(paintOnly.bounds.height()))),
                       QRect(QPoint(), QSize(qCeil(paintOnly.bounds.width()),
                                            qCeil(paintOnly.bounds.height())))) == 0,
          QStringLiteral("Packet inherited/empty text paint ignored root none"));

  const Rendered asciiLabel = render(QStringLiteral(
      "%%{init:{\"fontFamily\":\"Noto Sans\",\"themeVariables\":{"
      "\"fontFamily\":\"Noto Sans\"}}}%%\npacket-beta\n0-7: \"A\""));
  const Rendered nbspLabel = render(QStringLiteral(
      "%%{init:{\"fontFamily\":\"Noto Sans\",\"themeVariables\":{"
      "\"fontFamily\":\"Noto Sans\"}}}%%\npacket-beta\n0-7: \"\u00a0A\""));
  require(nbspLabel.scene->words.front().blocks.front().label.startsWith(
              QChar(0x00a0)),
          QStringLiteral("Packet parser lost leading NBSP"));
  const QSize labelImageSize(qCeil(asciiLabel.scene->bounds.width()),
                             qCeil(asciiLabel.scene->bounds.height()));
  require(rgbaDiffPixels(paintWide(*asciiLabel.scene, labelImageSize),
                         paintWide(*nbspLabel.scene, labelImageSize)) > 0,
          QStringLiteral("Packet painter collapsed NBSP as ASCII whitespace"));

  std::fprintf(stderr, "Packet config/edge parity: 49 fixture + 8 focused passed\n");
  return 0;
}
