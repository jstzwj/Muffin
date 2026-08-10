#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/packet/PacketDiagram.h"
#include "theme/CssCalc.h"

#include <QJsonValue>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid::packet {

// Raw values are intentional. Mermaid performs JavaScript arithmetic directly
// on packet config, so strings, booleans, zero and fractional values have
// observable geometry that an eager numeric conversion would erase.
struct PacketConfig {
  QJsonValue rowHeight = 32.0;
  QJsonValue bitWidth = 32.0;
  QJsonValue bitsPerRow = 32.0;
  QJsonValue showBits = true;
  QJsonValue paddingX = 5.0;
  QJsonValue paddingY = 5.0;
  QJsonValue useMaxWidth = true;
};

struct PacketSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString inheritedColor = QStringLiteral("#333");
  QString byteFontSize = QStringLiteral("10px");
  QString startByteColor = QStringLiteral("black");
  QString endByteColor = QStringLiteral("black");
  QString labelColor = QStringLiteral("black");
  QString labelFontSize = QStringLiteral("12px");
  QString titleColor = QStringLiteral("black");
  QString titleFontSize = QStringLiteral("14px");
  QString blockStrokeColor = QStringLiteral("black");
  QString blockStrokeWidth = QStringLiteral("1");
  QString blockFillColor = QStringLiteral("#efefef");
};

enum class PacketTextAnchor { Start, Middle, End };
enum class PacketTextBaseline { Auto, Middle };

struct PacketTextGeometry {
  QString cssClass;
  QString text;
  QPointF position;
  QString xAttribute;
  QString yAttribute;
  QString fill;
  qreal fontSize = 0.0;
  PacketTextAnchor anchor = PacketTextAnchor::Start;
  PacketTextBaseline baseline = PacketTextBaseline::Auto;
  int paintOrder = -1;
};

struct PacketBlockGeometry {
  qreal start = 0.0;
  qreal end = 0.0;
  QString label;
  QRectF rect;
  QString xAttribute;
  QString yAttribute;
  QString widthAttribute;
  QString heightAttribute;
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  PacketTextGeometry labelText;
  QVector<PacketTextGeometry> bitTexts;
  int paintOrder = -1;
};

struct PacketWordGeometry {
  int row = 0;
  QString yAttribute;
  QVector<PacketBlockGeometry> blocks;
};

struct PacketScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QRectF viewBoxBounds;
  QSizeF rasterViewport{1400.0, 1000.0};
  qreal svgWidth = 0.0;
  qreal svgHeight = 0.0;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  bool showBits = true;
  QString title;
  QString accTitle;
  QString accDescr;
  PacketConfig config;
  PacketSceneStyle style;
  QVector<PacketWordGeometry> words;
  PacketTextGeometry titleText;
  int nextPaintOrder = 0;
};

PacketScene buildPacketScene(const PacketData& data, PacketConfig config,
                             PacketSceneStyle style);

// CSS length metrics built from the same complete family fallback list used
// by the painter. The Adapter also uses this for the inherited root font-size.
CssLengthContext packetCssLengthContext(const QString& cssFamilies,
                                        qreal emPx);

}  // namespace muffin::mermaid::packet
