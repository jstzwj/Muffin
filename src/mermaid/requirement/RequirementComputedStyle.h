#pragma once

#include <QString>
#include <QtTypes>

namespace muffin::mermaid::requirement {

// CSS used values for one element in Mermaid's generated requirementDiagram
// DOM. Geometry remains in RequirementScene; this type is shared by the
// pre-layout cascade projection and the immutable scene.
struct RequirementComputedElement {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString backgroundColor;
  QString color;
  qreal opacity = 1.0;
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  qreal effectiveOpacity = 1.0;
  qreal effectiveFillOpacity = 1.0;
  qreal effectiveStrokeOpacity = 1.0;
  QString display = QStringLiteral("inline");
  QString visibility = QStringLiteral("visible");
  bool displayed = true;
  bool hasBox = true;
  bool ancestorRenderable = true;
  bool ancestorHasBox = true;
  QString fontFamily;
  QString fontSize;
  QString fontWeight;
  QString fontStyle;
};

struct RequirementPaintedBackground {
  QString color = QStringLiteral("transparent");
  qreal effectiveOpacity = 1.0;
  bool displayed = true;
};

}  // namespace muffin::mermaid::requirement
