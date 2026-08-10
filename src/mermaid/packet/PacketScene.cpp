#include "mermaid/packet/PacketScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/packet/PacketScenePainter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QFont>
#include <QFontMetricsF>

#include <cmath>
#include <utility>

namespace muffin::mermaid::packet {

void PacketScene::paint(QPainter& painter,
                        const MermaidPaintOptions& options) const {
  paintPacketScene(*this, painter, options);
}

namespace {

struct JsScalar {
  bool string = false;
  QString text;
  qreal number = 0.0;
};

QJsonValue mergedScalar(const QJsonValue& value, const QJsonValue& fallback) {
  // cleanAndMerge keeps scalar replacements but the object/array/null forms
  // observed at the source entry retain the scalar default.
  if (value.isUndefined() || value.isNull() || value.isArray() || value.isObject())
    return fallback;
  return value;
}

JsScalar jsScalar(const QJsonValue& value) {
  if (value.isString()) return {true, value.toString(), 0.0};
  return {false, {}, editor::jsNumberValue(value)};
}

QString jsString(const JsScalar& value) {
  return value.string ? value.text : editor::jsNumberToString(value.number);
}

JsScalar jsAdd(JsScalar left, JsScalar right) {
  if (left.string || right.string)
    return {true, jsString(left) + jsString(right), 0.0};
  return {false, {}, left.number + right.number};
}

JsScalar jsAdd(JsScalar left, qreal right) {
  return jsAdd(std::move(left), JsScalar{false, {}, right});
}

qreal jsNumber(const JsScalar& value) {
  return value.string ? editor::jsNumberValue(QJsonValue(value.text))
                      : value.number;
}

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble()) return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

QString attr(qreal value) { return editor::jsNumberToString(value); }

QString scalarAttribute(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  return editor::jsNumberToString(editor::jsNumberValue(value));
}

qreal svgUsedNumber(qreal value) {
  return std::isfinite(value) ? value : 0.0;
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
}

QJsonObject textJson(const PacketTextGeometry& text) {
  QJsonObject result;
  result[QStringLiteral("class")] = text.cssClass;
  result[QStringLiteral("text")] = text.text;
  result[QStringLiteral("position")] = pointJson(text.position);
  result[QStringLiteral("xAttr")] = text.xAttribute;
  result[QStringLiteral("yAttr")] = text.yAttribute;
  result[QStringLiteral("fontSize")] = text.fontSize;
  result[QStringLiteral("fill")] = text.fill;
  result[QStringLiteral("anchor")] =
      text.anchor == PacketTextAnchor::Middle
          ? QStringLiteral("middle")
          : text.anchor == PacketTextAnchor::End ? QStringLiteral("end")
                                                  : QStringLiteral("start");
  result[QStringLiteral("baseline")] =
      text.baseline == PacketTextBaseline::Middle ? QStringLiteral("middle")
                                                   : QStringLiteral("auto");
  result[QStringLiteral("paintOrder")] = text.paintOrder;
  return result;
}

QStringList cssFontFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

}  // namespace

CssLengthContext packetCssLengthContext(const QString& cssFamilies,
                                        qreal emPx) {
  if (emPx <= 0.0)
    return {emPx, 16.0, 0.0, 0.0, QSizeF(800.0, 600.0)};
  constexpr qreal referencePx = 16.0;
  const QStringList families = cssFontFamilies(cssFamilies);
  QFont font(families.first());
  if (families.size() > 1) font.setFamilies(families);
  font.setPixelSize(int(referencePx));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  const qreal scale = emPx / referencePx;
  return {emPx, 16.0, metrics.xHeight() * scale,
          metrics.horizontalAdvance(QChar('0')) * scale,
          QSizeF(800.0, 600.0)};
}

PacketScene buildPacketScene(const PacketData& data, PacketConfig config,
                             PacketSceneStyle style) {
  PacketScene scene;
  scene.config = config;
  scene.style = std::move(style);
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;

  const QJsonValue rawRowHeight = mergedScalar(config.rowHeight, 32.0);
  const QJsonValue rawBitWidth = mergedScalar(config.bitWidth, 32.0);
  const QJsonValue rawBitsPerRow = mergedScalar(config.bitsPerRow, 32.0);
  const QJsonValue rawPaddingX = mergedScalar(config.paddingX, 5.0);
  const QJsonValue rawPaddingY = mergedScalar(config.paddingY, 5.0);
  const QJsonValue rawShowBits = mergedScalar(config.showBits, true);
  const QJsonValue rawUseMaxWidth = mergedScalar(config.useMaxWidth, true);

  scene.showBits = jsTruthy(rawShowBits);
  scene.useMaxWidth = jsTruthy(rawUseMaxWidth);

  const JsScalar rowHeight = jsScalar(rawRowHeight);
  const qreal rowHeightNumber = jsNumber(rowHeight);
  const qreal bitWidth = editor::jsNumberValue(rawBitWidth);
  const qreal bitsPerRow = editor::jsNumberValue(rawBitsPerRow);
  const qreal paddingX = editor::jsNumberValue(rawPaddingX);
  JsScalar paddingY = jsScalar(rawPaddingY);
  if (scene.showBits) paddingY = jsAdd(std::move(paddingY), 10.0);
  const JsScalar totalRowHeight = jsAdd(rowHeight, paddingY);
  const qreal totalRowHeightNumber = jsNumber(totalRowHeight);

  scene.svgWidth = bitWidth * bitsPerRow + 2.0;
  scene.svgHeight = totalRowHeightNumber * qreal(data.words.size() + 1) -
                    (data.title.isEmpty() ? rowHeightNumber : 0.0);
  scene.viewBoxAttribute = QStringLiteral("0 0 %1 %2")
                               .arg(attr(scene.svgWidth), attr(scene.svgHeight));
  scene.viewBoxBounds =
      QRectF(0.0, 0.0, std::isfinite(scene.svgWidth) ? scene.svgWidth : 0.0,
             std::isfinite(scene.svgHeight) ? scene.svgHeight : 0.0);
  constexpr qreal intrinsicSvgHeight = 150.0;
  qreal rasterWidth = 0.0;
  qreal rasterHeight = 0.0;
  if (scene.useMaxWidth) {
    rasterWidth = std::isfinite(scene.svgWidth) && scene.svgWidth >= 0.0
                      ? std::min(scene.svgWidth, scene.rasterViewport.width())
                      : scene.rasterViewport.width();
    const bool validViewBox = scene.svgWidth > 0.0 && scene.svgHeight > 0.0 &&
                              std::isfinite(scene.svgWidth) &&
                              std::isfinite(scene.svgHeight);
    rasterHeight = validViewBox
                       ? scene.svgHeight * rasterWidth / scene.svgWidth
                       : intrinsicSvgHeight;
  } else {
    // Fixed negative SVG dimensions have zero used size in Chromium. NaN is
    // an invalid declaration and falls back to the replaced-element viewport.
    rasterWidth = std::isfinite(scene.svgWidth)
                      ? qMax<qreal>(0.0, scene.svgWidth)
                      : scene.rasterViewport.width();
    rasterHeight = std::isfinite(scene.svgHeight)
                       ? qMax<qreal>(0.0, scene.svgHeight)
                       : intrinsicSvgHeight;
  }
  scene.bounds = QRectF(0.0, 0.0, rasterWidth, rasterHeight);

  const CssLengthContext rootCtx = packetCssLengthContext(
      scene.style.fontFamily, scene.style.rootFontSize);
  const qreal byteFontSize = editor::cssFontSizePx(scene.style.byteFontSize, rootCtx);
  const qreal labelFontSize = editor::cssFontSizePx(scene.style.labelFontSize, rootCtx);
  const qreal titleFontSize = editor::cssFontSizePx(scene.style.titleFontSize, rootCtx);
  const qreal diagonal = std::hypot(scene.svgWidth, scene.svgHeight) /
                         std::sqrt(2.0);
  const qreal strokeWidth = editor::cssStrokeWidthPx(
      scene.style.blockStrokeWidth, rootCtx, diagonal);

  for (qsizetype row = 0; row < data.words.size(); ++row) {
    PacketWordGeometry word;
    word.row = int(row);
    const JsScalar wordBase{false, {}, qreal(row) * totalRowHeightNumber};
    const JsScalar wordY = jsAdd(wordBase, paddingY);
    const qreal wordYNumber = jsNumber(wordY);
    word.yAttribute = jsString(wordY);

    for (const PacketBlock& source : data.words.at(row)) {
      PacketBlockGeometry block;
      block.start = source.start;
      block.end = source.end;
      block.label = source.label;
      const qreal blockX = std::fmod(source.start, bitsPerRow) * bitWidth + 1.0;
      const qreal width = (source.end - source.start + 1.0) * bitWidth - paddingX;
      const JsScalar labelY = jsAdd(wordY, rowHeightNumber / 2.0);
      const qreal labelYNumber = jsNumber(labelY);
      block.rect = QRectF(svgUsedNumber(blockX), svgUsedNumber(wordYNumber),
                          width, rowHeightNumber);
      block.xAttribute = attr(blockX);
      block.yAttribute = word.yAttribute;
      block.widthAttribute = attr(width);
      block.heightAttribute = scalarAttribute(rawRowHeight);
      block.fill = scene.style.blockFillColor;
      block.stroke = scene.style.blockStrokeColor;
      block.strokeWidth = strokeWidth;
      block.paintOrder = scene.nextPaintOrder++;

      block.labelText.cssClass = QStringLiteral("packetLabel");
      block.labelText.text = source.label;
      block.labelText.position =
          QPointF(svgUsedNumber(blockX + width / 2.0), svgUsedNumber(labelYNumber));
      block.labelText.xAttribute = attr(blockX + width / 2.0);
      block.labelText.yAttribute = jsString(labelY);
      block.labelText.fill = scene.style.labelColor;
      block.labelText.fontSize = labelFontSize;
      block.labelText.anchor = PacketTextAnchor::Middle;
      block.labelText.baseline = PacketTextBaseline::Middle;
      block.labelText.paintOrder = scene.nextPaintOrder++;

      if (scene.showBits) {
        const bool single = source.start == source.end;
        const qreal bitY = wordYNumber - 2.0;
        PacketTextGeometry start;
        start.cssClass = QStringLiteral("packetByte start");
        start.text = editor::jsNumberToString(source.start);
        start.position = QPointF(
            svgUsedNumber(blockX + (single ? width / 2.0 : 0.0)),
            svgUsedNumber(bitY));
        start.xAttribute = attr(blockX + (single ? width / 2.0 : 0.0));
        start.yAttribute = attr(bitY);
        start.fill = scene.style.startByteColor;
        start.fontSize = byteFontSize;
        start.anchor = single ? PacketTextAnchor::Middle : PacketTextAnchor::Start;
        start.baseline = PacketTextBaseline::Auto;
        start.paintOrder = scene.nextPaintOrder++;
        block.bitTexts.append(start);
        if (!single) {
          PacketTextGeometry end;
          end.cssClass = QStringLiteral("packetByte end");
          end.text = editor::jsNumberToString(source.end);
          end.position = QPointF(svgUsedNumber(blockX + width), svgUsedNumber(bitY));
          end.xAttribute = attr(blockX + width);
          end.yAttribute = attr(bitY);
          end.fill = scene.style.endByteColor;
          end.fontSize = byteFontSize;
          end.anchor = PacketTextAnchor::End;
          end.baseline = PacketTextBaseline::Auto;
          end.paintOrder = scene.nextPaintOrder++;
          block.bitTexts.append(end);
        }
      }
      word.blocks.append(block);
    }
    scene.words.append(word);
  }

  scene.titleText.cssClass = QStringLiteral("packetTitle");
  scene.titleText.text = data.title;
  scene.titleText.position = QPointF(
      svgUsedNumber(scene.svgWidth / 2.0),
      svgUsedNumber(scene.svgHeight - totalRowHeightNumber / 2.0));
  scene.titleText.xAttribute = attr(scene.svgWidth / 2.0);
  scene.titleText.yAttribute =
      attr(scene.svgHeight - totalRowHeightNumber / 2.0);
  scene.titleText.fill = scene.style.titleColor;
  scene.titleText.fontSize = titleFontSize;
  scene.titleText.anchor = PacketTextAnchor::Middle;
  scene.titleText.baseline = PacketTextBaseline::Middle;
  scene.titleText.paintOrder = scene.nextPaintOrder++;
  return scene;
}

QJsonObject PacketScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("type")] = QStringLiteral("packet");
  root[QStringLiteral("viewBox")] = viewBoxAttribute;
  root[QStringLiteral("width")] = svgWidth;
  root[QStringLiteral("height")] = svgHeight;
  root[QStringLiteral("rasterWidth")] = bounds.width();
  root[QStringLiteral("rasterHeight")] = bounds.height();
  root[QStringLiteral("useMaxWidth")] = useMaxWidth;
  root[QStringLiteral("showBits")] = showBits;
  root[QStringLiteral("title")] = title;
  QJsonArray wordArray;
  for (const PacketWordGeometry& word : words) {
    QJsonObject wordObject;
    wordObject[QStringLiteral("row")] = word.row;
    wordObject[QStringLiteral("yAttr")] = word.yAttribute;
    QJsonArray blockArray;
    for (const PacketBlockGeometry& block : word.blocks) {
      QJsonObject object;
      object[QStringLiteral("start")] = block.start;
      object[QStringLiteral("end")] = block.end;
      object[QStringLiteral("label")] = block.label;
      object[QStringLiteral("xAttr")] = block.xAttribute;
      object[QStringLiteral("yAttr")] = block.yAttribute;
      object[QStringLiteral("widthAttr")] = block.widthAttribute;
      object[QStringLiteral("heightAttr")] = block.heightAttribute;
      object[QStringLiteral("fill")] = block.fill;
      object[QStringLiteral("stroke")] = block.stroke;
      object[QStringLiteral("strokeWidth")] = block.strokeWidth;
      object[QStringLiteral("labelText")] = textJson(block.labelText);
      QJsonArray bits;
      for (const PacketTextGeometry& bit : block.bitTexts) bits.append(textJson(bit));
      object[QStringLiteral("bitTexts")] = bits;
      object[QStringLiteral("paintOrder")] = block.paintOrder;
      blockArray.append(object);
    }
    wordObject[QStringLiteral("blocks")] = blockArray;
    wordArray.append(wordObject);
  }
  root[QStringLiteral("words")] = wordArray;
  root[QStringLiteral("titleText")] = textJson(titleText);
  return root;
}

}  // namespace muffin::mermaid::packet
