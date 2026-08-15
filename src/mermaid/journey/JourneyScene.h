#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/journey/JourneyDiagram.h"

#include <QRectF>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;

namespace muffin::mermaid::journey {

struct JourneyConfig {
  bool useMaxWidth = true;
  qreal diagramMarginX = 50.0;
  qreal diagramMarginY = 10.0;
  qreal leftMargin = 150.0;
  qreal maxLabelWidth = 360.0;
  qreal width = 150.0;
  qreal height = 50.0;
  qreal boxTextMargin = 5.0;
  qreal taskFontSize = 14.0;
  qreal taskFontLineStep = 14.0;
  QString taskFontFamily = QStringLiteral("Open Sans");
  qreal taskMargin = 50.0;
  qreal rectWidth = 150.0;
  qreal rectHeight = 50.0;
  QJsonValue diagramMarginXRaw = 50.0;
  QJsonValue diagramMarginYRaw = 10.0;
  QJsonValue leftMarginRaw = 150.0;
  QJsonValue taskMarginRaw = 50.0;
  QString textPlacement = QStringLiteral("fo");
  QString titleColor;
  QString titleFontFamily = QStringLiteral("trebuchet ms");
  qreal titleFontSize = 64.0;
};

struct JourneySceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString faceColor = QStringLiteral("#FFF8DC");
  QStringList fillTypes;
};

// One journey DOM element's resolved themeCSS outcome. Strings keep their CSS
// spelling (keywords, inheritance forms) so the painter applies the same
// used-value contract as Chrome; an empty string means "no CSS opinion" and
// keeps the pre-themeCSS base behaviour. fontSize is a resolved px value.
struct JourneyElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString color;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  qreal opacity = -1.0;
  bool visible = true;
  bool hasBox = true;
};

struct JourneyActor {
  QString name;
  QStringList lines;
  QString color;
  int position = 0;
  qreal y = 60.0;
  qreal maxLineWidth = 0.0;
  JourneyElementCss circle;  // legend circle.actor-N
  JourneyElementCss text;    // legend text.legend lines
};

struct JourneySectionGeometry {
  QRectF rect;
  QPointF oldTextAnchor;
  QPointF tspanTextAnchor;
  QString text;
  QString presentationFill;
  QString fill;
  bool cssFillActive = false;
  int colorIndex = 0;
  JourneyElementCss box;    // rect.journey-section.section-type-N
  JourneyElementCss label;  // foreignObject div.label (HTML color semantics)
  JourneyElementCss svgText;  // switch > text fallback (SVG fill semantics)
};

struct JourneyTaskGeometry {
  QRectF rect;
  QPointF oldTextAnchor;
  QPointF tspanTextAnchor;
  QVector<qreal> actorCenters;
  QString text;
  QString section;
  QString presentationFill;
  QString fill;
  bool cssFillActive = false;
  int colorIndex = 0;
  double score = 0.0;
  QPointF faceCenter;
  QStringList people;
  JourneyElementCss box;      // rect.task.task-type-N
  JourneyElementCss label;    // foreignObject div.label
  JourneyElementCss svgText;  // switch > text fallback
  JourneyElementCss line;     // line.task-line
  JourneyElementCss face;     // circle.face
  JourneyElementCss mouth;    // path.mouth / line.mouth
  QVector<JourneyElementCss> peopleCircles;  // person circle.actor-N
};

// themeCSS resolutions keyed to the DOM enumeration the renderer emits:
// actors in legend display order, sections/tasks in draw order, people in
// task order. `root` carries the svg-level cascade (the #id root rule sets
// fill/font on the svg element; user `svg {}` rules cannot reach it because
// Stylis scopes them to `#id svg`, a descendant selector).
struct JourneyCssOverrides {
  bool active = false;
  JourneyElementCss root;
  JourneyElementCss measureText;  // wrap/maxWidth probe text (classless <text>)
  JourneyElementCss title;
  JourneyElementCss axis;
  QVector<JourneyElementCss> actorCircles;
  QVector<JourneyElementCss> actorTexts;
  struct Section {
    JourneyElementCss box, label, svgText;
  };
  QVector<Section> sections;
  struct Task {
    JourneyElementCss box, label, svgText, line, face, mouth;
    QVector<JourneyElementCss> people;
  };
  QVector<Task> tasks;
};

struct JourneyScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  QRectF upstreamViewBox;
  qreal upstreamRootHeight = 0.0;
  qreal leftMarginResolved = 0.0;
  qreal canvasWidth = 0.0;
  qreal baseHeight = 0.0;
  QString title;
  QString accTitle;
  QString accDescr;
  JourneyConfig config;
  JourneySceneStyle style;
  QVector<JourneyActor> actors;
  JourneyActor prototypeActor;
  bool hasPrototypeActor = false;
  QVector<JourneySectionGeometry> sections;
  QVector<JourneyTaskGeometry> tasks;
  QVector<InteractionRegion> interactions;
  JourneyElementCss rootCss;   // svg element (fill inheritance source)
  JourneyElementCss titleCss;  // title text element
  JourneyElementCss axisCss;   // bottom axis line element
};

JourneyScene buildJourneyScene(const JourneyData& data, JourneyConfig config,
                               JourneySceneStyle style,
                               const JourneyCssOverrides* css = nullptr);

// The actor enumeration upstream renders: legend display order is the JS
// object key order of the actors map (which drops __proto__), circle classes
// use the sorted-name position. Shared by the scene builder and the
// themeCSS adapter so both enumerate the exact same DOM.
struct JourneyActorRosterEntry {
  QString name;
  int position = 0;
  QString color;
};
struct JourneyActorRoster {
  QVector<JourneyActorRosterEntry> display;
  JourneyActorRosterEntry prototype;
  bool hasPrototype = false;
  const JourneyActorRosterEntry* entryFor(const QString& name) const;
};
JourneyActorRoster journeyActorRoster(const JourneyData& data);

// Line wrapping for one actor name at the measurement font (upstream
// drawActorLegend's hidden probe <text>: classless, so plain `text` rules
// apply but .legend does not).
QStringList wrapJourneyActorLabel(const QString& actor, qreal maxLabelWidth,
                                  qreal fontPixelSize, const QString& fontFamily);

// The rect presentation-attribute fill for section/task color index N
// (config journey.sectionFills palette) — the value the CSS cascade falls
// back to when the theme emits no fillType rules.
QString journeySectionPresentationFill(int colorIndex);

}  // namespace muffin::mermaid::journey
