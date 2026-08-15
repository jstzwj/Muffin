#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/gantt/GanttDiagram.h"

#include <QLineF>
#include <QPair>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid::gantt {

struct GanttConfig {
  bool useMaxWidth = true;
  qreal useWidth = 1200.0;
  qreal titleTopMargin = 25.0;
  qreal barHeight = 20.0;
  qreal barGap = 4.0;
  qreal topPadding = 50.0;
  qreal rightPadding = 75.0;
  qreal leftPadding = 75.0;
  qreal gridLineStartPadding = 35.0;
  qreal fontSize = 11.0;
  qreal sectionFontSize = 11.0;
  int numberSectionStyles = 4;
  QString axisFormat = QStringLiteral("%Y-%m-%d");
  QString tickInterval;
  bool topAxis = false;
  QString displayMode;
  QString weekday = QStringLiteral("sunday");
};

struct GanttSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString titleColor = QStringLiteral("#333");
  QString sectionBkgColor;
  QString altSectionBkgColor;
  QString sectionBkgColor2;
  QString excludeBkgColor;
  QString taskBorderColor;
  QString taskBkgColor;
  QString taskTextColor;
  QString taskTextDarkColor;
  QString taskTextOutsideColor;
  QString taskTextClickableColor;
  QString activeTaskBorderColor;
  QString activeTaskBkgColor;
  QString gridColor;
  QString doneTaskBkgColor;
  QString doneTaskBorderColor;
  QString critBorderColor;
  QString critBkgColor;
  QString todayLineColor;
  QString vertLineColor = QStringLiteral("navy");
};

enum class GanttTextAnchor { Start, Middle, End };

// themeCSS overlay for one gantt DOM element. Empty strings keep the
// geometry's own value. Gantt layout is config-driven: the only CSS feedback
// is the task-label getBBox, which runs while the <text> still has no class
// (only the font-size presentation attribute), so class rules never feed
// measurement — only tag/ancestor selectors do (measureText carries that
// probe font). `measures` follows the shared rule: own display:none
// collapses the Chrome bbox to 0x0 while ancestor-only hiding keeps it.
struct GanttElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  QString textAnchor;
  qreal opacity = -1.0;
  bool visible = true;
  bool measures = true;
};

// Slots are indexed by emission order: excludes, ticks, section rows, tasks
// (draw order), section titles, then the shared title. The grid domain path
// (`.grid path { stroke-width: 0 }`) never paints but carries a slot so its
// resolved style stays comparable.
struct GanttCssOverrides {
  struct Tick {
    GanttElementCss line;
    GanttElementCss text;
  };
  struct Task {
    GanttElementCss rect;
    GanttElementCss text;
  };
  bool active = false;
  GanttElementCss measureText;
  GanttElementCss gridDomain;
  GanttElementCss today;
  QVector<GanttElementCss> excludes;
  QVector<Tick> ticks;
  QVector<GanttElementCss> sections;
  QVector<Task> tasks;
  QVector<GanttElementCss> sectionTitles;
  GanttElementCss title;
};

struct GanttRectGeometry {
  QString id;
  QString cssClass;
  QRectF rect;
  QString fill;
  QString stroke;
  qreal strokeWidth = 0.0;
  qreal opacity = 1.0;
  qreal radius = 0.0;
  bool milestone = false;
  QPointF transformOrigin;
  GanttElementCss css;
};

struct GanttLineGeometry {
  QString id;
  QString cssClass;
  QLineF line;
  QString stroke;
  qreal strokeWidth = 1.0;
  qreal opacity = 1.0;
  GanttElementCss css;
};

struct GanttTextGeometry {
  QString id;
  QString cssClass;
  QString text;
  QStringList lines;
  QPointF position;
  qreal fontSize = 11.0;
  QString fill;
  GanttTextAnchor anchor = GanttTextAnchor::Start;
  bool italic = false;
  bool bold = false;
  qreal lineStep = 0.0;
  // Grid labels inherit the d3 tick group's `.grid .tick { opacity: .8 }`;
  // every other text paints at 1 unless themeCSS moves it.
  qreal opacity = 1.0;
  GanttElementCss css;
};

// Shared pre-draw layout inputs: task draw order, section categories, the
// exclude-day runs, axis ticks and the time domain. Both the scene builder
// and the themeCSS DOM model consume it so css slots stay aligned with the
// emission order.
struct GanttPreparedLayout {
  QVector<GanttTask> tasks;
  QStringList categories;
  QHash<QString, int> categoryHeights;
  QVector<int> uniqueOrders;
  int rowCount = 0;
  QDateTime minTime;
  QDateTime maxTime;
  QVector<QPair<QDate, QDate>> excludeRuns;
  QVector<QDateTime> ticks;
};

GanttPreparedLayout ganttPrepareLayout(const GanttData& data,
                                       const GanttConfig& config);

// The task-label placement upstream decides inside the d3 attr callbacks: a
// getBBox() on the still-classless <text> yields the width, the class (and
// its width-N token) is assigned afterwards. Shared by the builder and the
// themeCSS DOM model so both agree on the emitted class.
struct GanttTaskTextPlacement {
  qreal textWidth = 0.0;
  qreal textX = 0.0;
  bool outside = false;
  bool outsideLeft = false;
};

GanttTaskTextPlacement ganttTaskTextPlacement(const GanttTask& task,
                                              const GanttPreparedLayout& layout,
                                              const GanttConfig& config,
                                              const QString& measureFamily,
                                              qreal measureSize);

// Upstream class attribute strings (byte-for-byte after DOMPurify's
// leading/trailing whitespace trim), shared with the themeCSS DOM model.
QString ganttTaskRectClass(const GanttTask& task, int secNum);
QString ganttTaskTextClass(const GanttTask& task, int secNum, qreal textWidth,
                           bool outside, bool outsideLeft);

struct GanttScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  QString title;
  QString accTitle;
  QString accDescr;
  GanttConfig config;
  GanttSceneStyle style;
  QVector<GanttRectGeometry> excludes;
  QVector<GanttLineGeometry> gridLines;
  QVector<GanttTextGeometry> gridLabels;
  QVector<GanttRectGeometry> sections;
  QVector<GanttRectGeometry> tasks;
  QVector<GanttTextGeometry> taskLabels;
  QVector<GanttTextGeometry> sectionLabels;
  QVector<GanttLineGeometry> todayLines;
  // The d3 axis domain path: stroke-width 0 upstream, kept for style parity.
  GanttLineGeometry gridDomain;
  GanttTextGeometry titleGeometry;
  QVector<InteractionRegion> interactions;
};

GanttScene buildGanttScene(const GanttData& data, GanttConfig config,
                           GanttSceneStyle style,
                           const GanttCssOverrides* css = nullptr);

}  // namespace muffin::mermaid::gantt
