#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"
#include "mermaid/rough/RoughOps.h"

#include <QJsonValue>
#include <QFont>
#include <QLineF>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::ishikawa {

struct IshikawaConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue diagramPadding = 20.0;
  QJsonValue handDrawnSeed = 0.0;
};

struct IshikawaSceneStyle {
  QString look;
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal layoutFontSize = 16.0;
  qreal fontSize = 16.0;
  QString lineColor = QStringLiteral("#333");
  QString mainBkg = QStringLiteral("#ECECFF");
  QString textColor = QStringLiteral("#333");
};

enum class IshikawaTextAnchor { Start, Middle, End };
enum class IshikawaTextBaseline { Auto, Middle, Hanging };

struct IshikawaTextGeometry {
  QString className;
  QString source;
  QStringList lines;
  QPointF anchor;
  qreal firstY = 0.0;
  qreal lineStep = 0.0;
  qreal fontSize = 16.0;
  QFont::Weight weight = QFont::Normal;
  IshikawaTextAnchor textAnchor = IshikawaTextAnchor::Start;
  IshikawaTextBaseline baseline = IshikawaTextBaseline::Auto;
  QPointF translation;
  QRectF layoutBounds;
  QRectF bounds;
};

struct IshikawaLineGeometry {
  QString className;
  QLineF line;
  bool markerStart = false;
  bool rough = false;
  rough::Drawable roughDrawable;
};

struct IshikawaPathGeometry {
  QString className;
  QPainterPath path;
  bool rough = false;
  rough::Drawable roughDrawable;
};

struct IshikawaRectGeometry {
  QString className;
  QRectF rect;
  bool rough = false;
  rough::Drawable roughDrawable;
};

enum class IshikawaPrimitiveKind { Line, Path, Rect, Text };

struct IshikawaPaintEntry {
  IshikawaPrimitiveKind kind = IshikawaPrimitiveKind::Line;
  int index = -1;
};

struct IshikawaScene final : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override {
    return QRectF(bounds.topLeft(), QSizeF(qRound(bounds.width()),
                                           qRound(bounds.height())));
  }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QRectF contentBounds;
  bool useMaxWidth = true;
  qreal padding = 20.0;
  IshikawaConfig config;
  IshikawaSceneStyle style;
  QVector<IshikawaLineGeometry> lines;
  QVector<IshikawaPathGeometry> paths;
  QVector<IshikawaRectGeometry> rects;
  QVector<IshikawaTextGeometry> texts;
  QVector<IshikawaPaintEntry> paintOrder;
};

IshikawaScene buildIshikawaScene(const IshikawaData& data,
                                  IshikawaConfig config,
                                  IshikawaSceneStyle style);

}  // namespace muffin::mermaid::ishikawa
