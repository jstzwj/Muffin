#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/architecture/ArchitectureDiagram.h"

#include <QJsonValue>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::architecture {

struct ArchitectureConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue padding = 40.0;
  QJsonValue iconSize = 80.0;
  QJsonValue fontSize = 16.0;
  QJsonValue randomize = false;
  QJsonValue nodeSeparation = 75.0;
  QJsonValue idealEdgeLengthMultiplier = 1.5;
  QJsonValue edgeElasticity = 0.45;
  QJsonValue numIter = 2500.0;
  QJsonValue seed = 1.0;
  QString svgId = QStringLiteral("architecture-native");
};

struct ArchitectureSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  QString textColor = QStringLiteral("#333");
  QString edgeColor = QStringLiteral("#333333");
  QString arrowColor = QStringLiteral("#333333");
  QString edgeWidth = QStringLiteral("3");
  QString groupBorderColor =
      QStringLiteral("hsl(240, 60%, 86.2745098039%)");
  QString groupBorderWidth = QStringLiteral("2px");
};

enum class ArchitectureNodeKind { Service, Junction };

struct ArchitectureNodeGeometry {
  ArchitectureNodeKind kind = ArchitectureNodeKind::Service;
  QString id;
  QString icon;
  QString iconText;
  QString title;
  QString parent;
  QPointF topLeft;
  QRectF localBounds;
  QRectF paintedBounds;
};

struct ArchitectureGroupGeometry {
  QString id;
  QString icon;
  QString title;
  QString parent;
  QRectF rect;
};

struct ArchitectureArrowGeometry {
  QChar direction;
  QPointF position;
  QPolygonF polygon;
};

struct ArchitectureEdgeGeometry {
  QString id;
  QString lhsId;
  QString rhsId;
  QString title;
  QVector<QPointF> points;
  QString pathData;
  QRectF bounds;
  QRectF labelBounds;
  QVector<ArchitectureArrowGeometry> arrows;
};

struct ArchitectureScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  ArchitectureConfig config;
  ArchitectureSceneStyle style;
  QVector<ArchitectureNodeGeometry> nodes;
  QVector<ArchitectureGroupGeometry> groups;
  QVector<ArchitectureEdgeGeometry> edges;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
};

ArchitectureScene buildArchitectureScene(const ArchitectureData& data,
                                           ArchitectureConfig config,
                                           ArchitectureSceneStyle style);

}  // namespace muffin::mermaid::architecture
