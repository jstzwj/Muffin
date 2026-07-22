#pragma once

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/classdiagram/ClassScene.h"

#include <QImage>

class QPainter;

namespace muffin::mermaid::classdiagram {

enum class ClassPaintMode { Color, SemanticMask, TextMask };

constexpr QRgb kClassMaskNode = 0xFFFF0000u;
constexpr QRgb kClassMaskEdge = 0xFF00FF00u;
constexpr QRgb kClassMaskEdgeLabel = 0xFF0000FFu;
constexpr QRgb kClassMaskText = 0xFFFF00FFu;
constexpr QRgb kClassMaskMarker = 0xFF00FFFFu;
constexpr QRgb kClassMaskCluster = 0xFFFFFF00u;

void paintClassScene(const ClassScene& scene, QPainter& painter,
                     ClassPaintMode mode = ClassPaintMode::Color,
                     const MermaidPaintOptions& options = {});
QImage renderClassSceneToImage(const ClassScene& scene, qreal dpr = 1.0,
                               qreal padding = 8.0,
                               ClassPaintMode mode = ClassPaintMode::Color);

}  // namespace muffin::mermaid::classdiagram
