#include "mermaid/gantt/GanttScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttScenePainter.h"
#include "mermaid/text/LabelText.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::gantt {
namespace {

qreal roundedScale(const QDateTime& value, const QDateTime& start,
                   const QDateTime& end, qreal width) {
  if (!value.isValid() || !start.isValid() || !end.isValid()) return 0.0;
  const qint64 lo = start.toMSecsSinceEpoch();
  const qint64 hi = end.toMSecsSinceEpoch();
  if (lo == hi) return std::round(width / 2.0);
  return std::round(double(value.toMSecsSinceEpoch() - lo) /
                    double(hi - lo) * width);
}

QString formatTick(const QDateTime& date, const QString& format) {
  QString result;
  for (qsizetype i = 0; i < format.size(); ++i) {
    if (format.at(i) != QLatin1Char('%') || i + 1 >= format.size()) {
      result += format.at(i);
      continue;
    }
    const QChar token = format.at(++i);
    if (token == QLatin1Char('Y')) result += date.toString(QStringLiteral("yyyy"));
    else if (token == QLatin1Char('m')) result += date.toString(QStringLiteral("MM"));
    else if (token == QLatin1Char('d')) result += date.toString(QStringLiteral("dd"));
    else if (token == QLatin1Char('H')) result += date.toString(QStringLiteral("HH"));
    else if (token == QLatin1Char('M')) result += date.toString(QStringLiteral("mm"));
    else if (token == QLatin1Char('S')) result += date.toString(QStringLiteral("ss"));
    else if (token == QLatin1Char('%')) result += QLatin1Char('%');
    else result += QLatin1Char('%') + token;
  }
  return result;
}

// dayjs startOf() floors the LOCAL wall clock. Gantt values are stored as
// UTC-spec wall clocks, so floor those walls directly — setTimeZone would
// convert the instant across zones and skew the floor by the local offset.
QDateTime floorLocal(QDateTime value, const QString& unit, int weekday) {
  value.setTimeSpec(Qt::UTC);
  if (unit == QLatin1String("millisecond")) return value;
  if (unit == QLatin1String("second"))
    return QDateTime(value.date(), QTime(value.time().hour(), value.time().minute(),
                                         value.time().second()), Qt::UTC);
  if (unit == QLatin1String("minute"))
    return QDateTime(value.date(), QTime(value.time().hour(), value.time().minute()),
                     Qt::UTC);
  if (unit == QLatin1String("hour"))
    return QDateTime(value.date(), QTime(value.time().hour(), 0), Qt::UTC);
  QDateTime result(value.date(), QTime(0, 0), Qt::UTC);
  if (unit == QLatin1String("week")) {
    int delta = result.date().dayOfWeek() - weekday;
    if (delta < 0) delta += 7;
    result = result.addDays(-delta);
  } else if (unit == QLatin1String("month")) {
    result.setDate(QDate(result.date().year(), result.date().month(), 1));
  } else if (unit == QLatin1String("year")) {
    result.setDate(QDate(result.date().year(), 1, 1));
  }
  return result;
}

QDateTime addInterval(const QDateTime& value, const QString& unit, int every) {
  if (unit == QLatin1String("millisecond")) return value.addMSecs(every);
  if (unit == QLatin1String("second")) return value.addSecs(every);
  if (unit == QLatin1String("minute")) return value.addSecs(every * 60);
  if (unit == QLatin1String("hour")) return value.addSecs(every * 3600);
  if (unit == QLatin1String("day")) return value.addDays(every);
  if (unit == QLatin1String("week")) return value.addDays(every * 7);
  if (unit == QLatin1String("month")) return value.addMonths(every);
  return value.addYears(every);
}

bool intervalIndexMatches(const QDateTime& value, const QString& unit, int every) {
  if (every <= 1) return true;
  if (unit == QLatin1String("millisecond")) return value.time().msec() % every == 0;
  if (unit == QLatin1String("second")) return value.time().second() % every == 0;
  if (unit == QLatin1String("minute")) return value.time().minute() % every == 0;
  if (unit == QLatin1String("hour")) return value.time().hour() % every == 0;
  if (unit == QLatin1String("day")) return (value.date().day() - 1) % every == 0;
  if (unit == QLatin1String("month")) return value.date().month() % every == 0;
  if (unit == QLatin1String("year")) return value.date().year() % every == 0;
  return true;
}

QVector<QDateTime> explicitTicks(const QDateTime& min, const QDateTime& max,
                                 const QString& source, const QString& weekdayName) {
  static const QRegularExpression re(QStringLiteral(
      R"(^([1-9]\d*)(millisecond|second|minute|hour|day|week|month)$)"));
  const auto match = re.match(source);
  if (!match.hasMatch()) return {};
  const int every = match.captured(1).toInt();
  const QString unit = match.captured(2);
  static const QStringList weekdays = {
      QStringLiteral("monday"), QStringLiteral("tuesday"),
      QStringLiteral("wednesday"), QStringLiteral("thursday"),
      QStringLiteral("friday"), QStringLiteral("saturday"),
      QStringLiteral("sunday")};
  int weekday = weekdays.indexOf(weekdayName.toLower()) + 1;
  if (weekday <= 0) weekday = 7;
  QDateTime cursor = floorLocal(min, unit, weekday);
  if (unit != QLatin1String("week")) {
    while (!intervalIndexMatches(cursor, unit, every)) cursor = addInterval(cursor, unit, 1);
  }
  while (cursor < min) cursor = addInterval(cursor, unit, every);
  QVector<QDateTime> ticks;
  for (int guard = 0; cursor <= max && guard < 10000; ++guard) {
    ticks.append(cursor);
    cursor = addInterval(cursor, unit, every);
  }
  return ticks;
}

QVector<QDateTime> automaticTicks(const QDateTime& min, const QDateTime& max) {
  struct Candidate { const char* unit; int every; qint64 duration; };
  static const Candidate candidates[] = {
      {"second", 1, 1000}, {"second", 5, 5000}, {"second", 15, 15000},
      {"second", 30, 30000}, {"minute", 1, 60000}, {"minute", 5, 300000},
      {"minute", 15, 900000}, {"minute", 30, 1800000}, {"hour", 1, 3600000},
      {"hour", 3, 10800000}, {"hour", 6, 21600000}, {"hour", 12, 43200000},
      {"day", 1, 86400000}, {"day", 2, 172800000}, {"week", 1, 604800000},
      {"month", 1, 2592000000LL}, {"month", 3, 7776000000LL},
      {"year", 1, 31536000000LL}};
  const qint64 target = std::max<qint64>(1, (max.toMSecsSinceEpoch() -
                                             min.toMSecsSinceEpoch()) / 10);
  const Candidate* best = &candidates[0];
  double bestScore = std::numeric_limits<double>::infinity();
  for (const Candidate& candidate : candidates) {
    const double score = std::abs(std::log(double(target) / candidate.duration));
    if (score < bestScore) { best = &candidate; bestScore = score; }
  }
  return explicitTicks(min, max,
                       QString::number(best->every) + QString::fromLatin1(best->unit),
                       QStringLiteral("sunday"));
}

QString sectionFill(int section, const GanttSceneStyle& style) {
  const int index = ((section % 4) + 4) % 4;
  if (index == 0) return style.sectionBkgColor;
  if (index == 2) return style.sectionBkgColor2;
  return style.altSectionBkgColor;
}

QString taskFill(const GanttTask& task, const GanttSceneStyle& style) {
  if (task.done) return style.doneTaskBkgColor;
  if (task.active) return style.activeTaskBkgColor;
  if (task.crit) return style.critBkgColor;
  return style.taskBkgColor;
}

QString taskStroke(const GanttTask& task, const GanttSceneStyle& style) {
  if (task.crit) return style.critBorderColor;
  if (task.done) return style.doneTaskBorderColor;
  if (task.active) return style.activeTaskBorderColor;
  if (task.vert) return style.vertLineColor;
  return style.taskBorderColor;
}

QString taskTextFill(const GanttTask& task, bool outside,
                     const GanttSceneStyle& style) {
  if (task.classes.contains(QStringLiteral("clickable")))
    return style.taskTextClickableColor;
  if (task.vert) return style.vertLineColor;
  if (outside && (task.done || (task.done && task.crit)))
    return style.taskTextOutsideColor;
  if (outside) return style.taskTextOutsideColor;
  if (task.active || task.done || (task.active && task.crit) ||
      (task.done && task.crit))
    return style.taskTextDarkColor;
  return style.taskTextColor;
}

QJsonObject rectJson(const GanttRectGeometry& rect) {
  return {{QStringLiteral("id"), rect.id},
          {QStringLiteral("class"), rect.cssClass},
          {QStringLiteral("x"), rect.rect.x()},
          {QStringLiteral("y"), rect.rect.y()},
          {QStringLiteral("width"), rect.rect.width()},
          {QStringLiteral("height"), rect.rect.height()},
          {QStringLiteral("fill"), rect.fill},
          {QStringLiteral("stroke"), rect.stroke},
          {QStringLiteral("strokeWidth"), rect.strokeWidth},
          {QStringLiteral("opacity"), rect.opacity},
          {QStringLiteral("milestone"), rect.milestone}};
}

QJsonObject textJson(const GanttTextGeometry& text) {
  return {{QStringLiteral("id"), text.id},
          {QStringLiteral("class"), text.cssClass},
          {QStringLiteral("text"), text.text},
          {QStringLiteral("x"), text.position.x()},
          {QStringLiteral("y"), text.position.y()},
          {QStringLiteral("fontSize"), text.fontSize},
          {QStringLiteral("fill"), text.fill},
          {QStringLiteral("anchor"), int(text.anchor)}};
}

}  // namespace

void GanttScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintGanttScene(*this, painter, options);
}

QJsonObject GanttScene::toJsonObject() const {
  QJsonArray excludeArray, sectionArray, taskArray, labelArray, sectionLabelArray,
      gridLineArray, gridLabelArray, todayLineArray;
  for (const auto& value : excludes) excludeArray.append(rectJson(value));
  for (const auto& value : sections) sectionArray.append(rectJson(value));
  for (const auto& value : tasks) taskArray.append(rectJson(value));
  for (const auto& value : taskLabels) labelArray.append(textJson(value));
  for (const auto& value : sectionLabels) sectionLabelArray.append(textJson(value));
  for (const auto& value : gridLabels) gridLabelArray.append(textJson(value));
  for (const auto& value : gridLines)
    gridLineArray.append(QJsonObject{{QStringLiteral("x1"), value.line.x1()},
                                     {QStringLiteral("y1"), value.line.y1()},
                                     {QStringLiteral("x2"), value.line.x2()},
                                     {QStringLiteral("y2"), value.line.y2()},
                                     {QStringLiteral("stroke"), value.stroke}});
  for (const auto& value : todayLines)
    todayLineArray.append(QJsonObject{{QStringLiteral("x1"), value.line.x1()},
                                      {QStringLiteral("y1"), value.line.y1()},
                                      {QStringLiteral("x2"), value.line.x2()},
                                      {QStringLiteral("y2"), value.line.y2()},
                                      {QStringLiteral("stroke"), value.stroke},
                                      {QStringLiteral("strokeWidth"), value.strokeWidth},
                                      {QStringLiteral("opacity"), value.opacity}});
  return {{QStringLiteral("bounds"), QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()}},
          {QStringLiteral("title"), title},
          {QStringLiteral("excludes"), excludeArray},
          {QStringLiteral("gridLines"), gridLineArray},
          {QStringLiteral("todayLines"), todayLineArray},
          {QStringLiteral("gridLabels"), gridLabelArray},
          {QStringLiteral("sections"), sectionArray},
          {QStringLiteral("tasks"), taskArray},
          {QStringLiteral("taskLabels"), labelArray},
          {QStringLiteral("sectionLabels"), sectionLabelArray},
          {QStringLiteral("titleGeometry"), textJson(titleGeometry)}};
}

// Upstream task rect class, byte-for-byte: DOMPurify (mermaid's sanitize
// step) trims leading/trailing class-attribute whitespace, so the upstream
// trailing join space disappears while interior double spaces (milestone /
// vert prefixes) survive.
QString ganttTaskRectClass(const GanttTask& task, int secNum) {
  QString taskClass;
  if (task.active) {
    if (task.crit) taskClass += QStringLiteral(" activeCrit");
    else taskClass = QStringLiteral(" active");
  } else if (task.done) {
    if (task.crit) taskClass = QStringLiteral(" doneCrit");
    else taskClass = QStringLiteral(" done");
  } else if (task.crit) {
    taskClass += QStringLiteral(" crit");
  }
  if (taskClass.isEmpty()) taskClass = QStringLiteral(" task");
  if (task.milestone) taskClass = QStringLiteral(" milestone ") + taskClass;
  if (task.vert) taskClass = QStringLiteral(" vert ") + taskClass;
  taskClass += QString::number(secNum);
  if (!task.classes.isEmpty())
    taskClass += QStringLiteral(" ") + task.classes.join(QLatin1Char(' '));
  return (QStringLiteral("task") + taskClass).trimmed();
}

// Upstream task text class (taskType quirks included: the active prefix has
// no leading space so an active+done task joins with a single space, while
// every other branch keeps its leading space and doubles it). The width-N
// token carries the raw classless getBBox() measurement.
QString ganttTaskTextClass(const GanttTask& task, int secNum, qreal textWidth,
                           bool outside, bool outsideLeft) {
  QString taskType;
  if (task.active) {
    if (task.crit) taskType = QStringLiteral("activeCritText%1").arg(secNum);
    else taskType = QStringLiteral("activeText%1").arg(secNum);
  }
  if (task.done) {
    if (task.crit)
      taskType += QStringLiteral(" doneCritText%1").arg(secNum);
    else
      taskType += QStringLiteral(" doneText%1").arg(secNum);
  } else if (task.crit) {
    taskType += QStringLiteral(" critText%1").arg(secNum);
  }
  if (task.milestone) taskType += QStringLiteral(" milestoneText");
  if (task.vert) taskType += QStringLiteral(" vertText");
  const QString classStr = task.classes.join(QLatin1Char(' '));
  if (!outside) {
    return (classStr + QStringLiteral(" taskText taskText%1 ").arg(secNum) +
            taskType +
            QStringLiteral(" width-%1").arg(QString::number(textWidth)))
        .trimmed();
  }
  if (outsideLeft) {
    return (classStr +
            QStringLiteral(" taskTextOutsideLeft taskTextOutside%1 ").arg(secNum) +
            taskType)
        .trimmed();
  }
  return (classStr +
          QStringLiteral(" taskTextOutsideRight taskTextOutside%1 ").arg(secNum) +
          taskType +
          QStringLiteral(" width-%1").arg(QString::number(textWidth)))
      .trimmed();
}

GanttPreparedLayout ganttPrepareLayout(const GanttData& data,
                                       const GanttConfig& config) {
  GanttPreparedLayout layout;
  QVector<GanttTask>& tasks = layout.tasks = data.tasks;
  for (const GanttTask& task : tasks)
    if (!task.vert && !layout.categories.contains(task.type))
      layout.categories.append(task.type);

  if (data.displayMode == QLatin1String("compact") ||
      config.displayMode == QLatin1String("compact")) {
    for (const QString& category : layout.categories) {
      QVector<GanttTask*> categoryTasks;
      for (GanttTask& task : tasks)
        if (!task.vert && task.section == category) categoryTasks.append(&task);
      std::stable_sort(categoryTasks.begin(), categoryTasks.end(),
                       [](const GanttTask* a, const GanttTask* b) {
                         return a->startTime < b->startTime;
                       });
      QVector<QDateTime> timeline(categoryTasks.size());
      int maxRow = 0;
      for (GanttTask* task : categoryTasks) {
        for (int row = 0; row < timeline.size(); ++row) {
          if (!timeline.at(row).isValid() || task->startTime >= timeline.at(row)) {
            timeline[row] = task->endTime;
            task->order = layout.rowCount + row;
            maxRow = std::max(maxRow, row);
            break;
          }
        }
      }
      layout.categoryHeights.insert(category, maxRow + 1);
      layout.rowCount += maxRow + 1;
    }
  } else {
    for (const GanttTask& task : tasks)
      if (!task.vert) ++layout.categoryHeights[task.type];
    layout.rowCount = int(std::count_if(tasks.cbegin(), tasks.cend(),
                                        [](const GanttTask& task) {
                                          return !task.vert;
                                        }));
  }

  for (const GanttTask& task : tasks) {
    if (!layout.minTime.isValid() || task.startTime < layout.minTime)
      layout.minTime = task.startTime;
    if (!layout.maxTime.isValid() || task.endTime > layout.maxTime)
      layout.maxTime = task.endTime;
  }

  // Excluded local calendar-day runs are painted before the grid.
  if (layout.minTime.isValid() && layout.maxTime.isValid() &&
      (!data.excludes.isEmpty() || !data.includes.isEmpty()) &&
      layout.minTime.daysTo(layout.maxTime) <= 366 * 5 + 2) {
    auto isExcluded = [&](const QDate& date) {
      const QString iso = date.toString(QStringLiteral("yyyy-MM-dd"));
      if (data.includes.contains(iso)) return false;
      const int day = date.dayOfWeek();
      if (data.excludes.contains(QStringLiteral("weekends"))) {
        const int start = data.weekend == QLatin1String("friday") ? 5 : 6;
        if (day == start || day == start + 1) return true;
      }
      static const QStringList names = {
          QStringLiteral("monday"), QStringLiteral("tuesday"),
          QStringLiteral("wednesday"), QStringLiteral("thursday"),
          QStringLiteral("friday"), QStringLiteral("saturday"),
          QStringLiteral("sunday")};
      return data.excludes.contains(iso) ||
             data.excludes.contains(names.at(day - 1));
    };
    QDate runStart, runEnd;
    for (QDate date = layout.minTime.date(); date <= layout.maxTime.date();
         date = date.addDays(1)) {
      if (isExcluded(date)) {
        if (!runStart.isValid()) runStart = date;
        runEnd = date;
      } else if (runStart.isValid()) {
        layout.excludeRuns.append({runStart, runEnd});
        runStart = {};
        runEnd = {};
      }
    }
    if (runStart.isValid()) layout.excludeRuns.append({runStart, runEnd});
  }

  const QString tickSource = !data.tickInterval.isEmpty()
                                 ? data.tickInterval : config.tickInterval;
  layout.ticks = explicitTicks(layout.minTime, layout.maxTime, tickSource,
                               !data.weekday.isEmpty() ? data.weekday
                                                       : config.weekday);
  if (layout.ticks.isEmpty() && layout.minTime.isValid() &&
      layout.maxTime.isValid())
    layout.ticks = automaticTicks(layout.minTime, layout.maxTime);

  std::stable_sort(tasks.begin(), tasks.end(),
                   [](const GanttTask& a, const GanttTask& b) {
                     return a.startTime < b.startTime;
                   });
  std::stable_sort(tasks.begin(), tasks.end(),
                   [](const GanttTask& a, const GanttTask& b) {
                     return a.vert == b.vert ? false : !a.vert;
                   });
  for (const GanttTask& task : tasks)
    if (!task.vert && !layout.uniqueOrders.contains(task.order))
      layout.uniqueOrders.append(task.order);
  return layout;
}

GanttTaskTextPlacement ganttTaskTextPlacement(const GanttTask& task,
                                              const GanttPreparedLayout& layout,
                                              const GanttConfig& config,
                                              const QString& measureFamily,
                                              qreal measureSize) {
  GanttTaskTextPlacement out;
  const qreal width = config.useWidth;
  const qreal scaleWidth = width - config.leftPadding - config.rightPadding;
  const auto scale = [&layout, scaleWidth](const QDateTime& value) {
    return roundedScale(value, layout.minTime, layout.maxTime, scaleWidth);
  };
  const editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(measureFamily, measureSize);
  QFontMetricsF metrics(font.font);
  out.textWidth =
      metrics.horizontalAdvance(text::collapsedSvgText(task.task)) * font.scale;

  qreal startX = scale(task.startTime);
  qreal endX = scale(task.renderEndTime.isValid() ? task.renderEndTime
                                                  : task.endTime);
  if (task.milestone) {
    startX += 0.5 * (scale(task.endTime) - startX) - 0.5 * config.barHeight;
    endX = startX + config.barHeight;
  }
  // Upstream's x callback compares against renderEndTime||endTime while the
  // class callback compares against the raw endTime; the two only diverge
  // for excluded trailing days, where this unification matches the observed
  // upstream output.
  qreal classEndX = scale(task.endTime);
  if (task.milestone) classEndX = scale(task.startTime) + config.barHeight;
  if (task.vert) {
    out.textX = scale(task.startTime) + config.leftPadding;
    return out;
  }
  out.outside = out.textWidth > classEndX - startX;
  out.outsideLeft =
      out.outside &&
      classEndX + out.textWidth + 1.5 * config.leftPadding > width;
  if (out.outsideLeft)
    out.textX = startX + config.leftPadding - 5.0;
  else if (out.outside)
    out.textX = endX + config.leftPadding + 5.0;
  else
    out.textX = (endX - startX) / 2.0 + startX + config.leftPadding;
  return out;
}

GanttScene buildGanttScene(const GanttData& data, GanttConfig config,
                           GanttSceneStyle style,
                           const GanttCssOverrides* css) {
  GanttScene scene;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  scene.config = std::move(config);
  scene.style = std::move(style);

  const GanttPreparedLayout layout = ganttPrepareLayout(data, scene.config);
  const QVector<GanttTask>& tasks = layout.tasks;
  const QStringList& categories = layout.categories;

  const qreal gap = scene.config.barHeight + scene.config.barGap;
  const qreal width = scene.config.useWidth;
  const qreal height =
      2.0 * scene.config.topPadding + layout.rowCount * gap;
  scene.bounds = QRectF(0.0, 0.0, width, height);
  const qreal scaleWidth = width - scene.config.leftPadding - scene.config.rightPadding;

  const auto scale = [&](const QDateTime& value) {
    return roundedScale(value, layout.minTime, layout.maxTime, scaleWidth);
  };
  const GanttElementCss neutral;
  const auto rectSlot = [&](qsizetype index) -> const GanttElementCss& {
    return css && index >= 0 && index < css->excludes.size()
               ? css->excludes.at(index)
               : neutral;
  };

  for (qsizetype run = 0; run < layout.excludeRuns.size(); ++run) {
    // Qt::UTC wall clocks, matching the task times' spec: the x mapping goes
    // through toMSecsSinceEpoch, so a LocalTime-spec exclude run would skew by
    // the local offset against the UTC-spec task axis.
    const QDateTime start(layout.excludeRuns.at(run).first, QTime(0, 0), Qt::UTC);
    const QDateTime end(layout.excludeRuns.at(run).second, QTime(23, 59, 59, 999),
                        Qt::UTC);
    GanttRectGeometry rect;
    rect.id = QStringLiteral("exclude-") +
              layout.excludeRuns.at(run).first.toString(Qt::ISODate);
    rect.cssClass = QStringLiteral("exclude-range");
    rect.rect = QRectF(scale(start) + scene.config.leftPadding,
                       scene.config.gridLineStartPadding,
                       scale(end) - scale(start),
                       height - scene.config.topPadding -
                           scene.config.gridLineStartPadding);
    rect.fill = scene.style.excludeBkgColor;
    // CSS initial stroke-width is 1px; the unset stroke keeps it unpainted.
    rect.strokeWidth = 1.0;
    rect.css = rectSlot(run);
    scene.excludes.append(std::move(rect));
  }

  const QString axisFormat = !data.axisFormat.isEmpty()
                                 ? data.axisFormat
                                 : (!scene.config.axisFormat.isEmpty()
                                        ? scene.config.axisFormat
                                        : QStringLiteral("%Y-%m-%d"));
  const auto addAxis = [&](qreal y, bool top) {
    const qreal tickEnd = top
                              ? y + height - scene.config.topPadding -
                                        scene.config.gridLineStartPadding
                              : y - height + scene.config.topPadding +
                                        scene.config.gridLineStartPadding;
    for (const QDateTime& tick : layout.ticks) {
      const qsizetype slot = scene.gridLines.size();
      const GanttElementCss& lineCss =
          css && slot < css->ticks.size() ? css->ticks.at(slot).line : neutral;
      const GanttElementCss& textCss =
          css && slot < css->ticks.size() ? css->ticks.at(slot).text : neutral;
      // d3-axis offsets one-pixel strokes by 0.5 CSS px for a 1x display and
      // stamps stroke=currentColor on every tick line, which resolves against
      // the inherited `color` (initial black). The `.grid .tick { stroke }`
      // declaration lands on the <g> and loses to the line's own presentation
      // attribute, so gridColor never reaches the painted line.
      const qreal x = scene.config.leftPadding + scale(tick) + 0.5;
      GanttLineGeometry line;
      line.cssClass = QStringLiteral("tick");
      line.line = QLineF(x, y, x, tickEnd);
      line.stroke = QStringLiteral("#000000");
      line.strokeWidth = 1.0;
      line.opacity = 0.8;
      line.css = lineCss;
      scene.gridLines.append(std::move(line));
      GanttTextGeometry label;
      // Upstream grid labels carry no class attribute.
      label.text = formatTick(tick, axisFormat);
      // d3 places the baseline at k*spacing (±3px); mermaid stamps dy=1em on
      // the bottom axis only, and the em resolves against the *CSS* font
      // size, so a themeCSS font-size override moves the baseline too.
      const qreal labelSize = textCss.fontSize >= 0.0 ? textCss.fontSize : 10.0;
      label.position = QPointF(x, top ? y - 3.0 : y + 3.0 + labelSize);
      label.fontSize = 10.0;
      label.fill = scene.style.textColor;
      label.anchor = GanttTextAnchor::Middle;
      label.opacity = 0.8;
      label.css = textCss;
      scene.gridLabels.append(std::move(label));
    }
  };
  addAxis(height - 50.0, false);
  if (data.topAxis || scene.config.topAxis) addAxis(scene.config.topPadding, true);
  {
    // The d3 domain path: `.grid path { stroke-width: 0 }` keeps it invisible;
    // its stroke resolves through the same currentColor chain as the ticks.
    GanttLineGeometry domain;
    domain.cssClass = QStringLiteral("domain");
    domain.line = QLineF(scene.config.leftPadding + 0.5, height - 50.0,
                         width - scene.config.rightPadding + 0.5, height - 50.0);
    domain.stroke = QStringLiteral("#000000");
    domain.strokeWidth = 0.0;
    if (css) domain.css = css->gridDomain;
    scene.gridDomain = std::move(domain);
  }

  for (int order : layout.uniqueOrders) {
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(),
                                    [order](const GanttTask& task) {
                                      return !task.vert && task.order == order;
                                    });
    if (found == tasks.cend()) continue;
    const int category = int(std::max<qsizetype>(0, categories.indexOf(found->type)));
    GanttRectGeometry rect;
    rect.cssClass = QStringLiteral("section section%1").arg(
        category % std::max(1, scene.config.numberSectionStyles));
    rect.rect = QRectF(0.0, order * gap + scene.config.topPadding - 2.0,
                       width - scene.config.rightPadding / 2.0, gap);
    rect.fill = sectionFill(category, scene.style);
    rect.opacity = 0.2;
    rect.strokeWidth = 1.0;
    const qsizetype slot = scene.sections.size();
    if (css && slot < css->sections.size()) rect.css = css->sections.at(slot);
    scene.sections.append(std::move(rect));
  }

  // The task-label measurement runs through a classless probe text (only the
  // font-size presentation attribute exists at that point), so themeCSS
  // font overrides reach it through tag/ancestor selectors only.
  const QString measureFamily =
      css && !css->measureText.fontFamily.isEmpty() ? css->measureText.fontFamily
                                                    : scene.style.fontFamily;
  const qreal measureSize =
      css && css->measureText.fontSize >= 0.0 ? css->measureText.fontSize
                                              : scene.config.fontSize;
  for (const GanttTask& task : tasks) {
    const qsizetype slot = scene.tasks.size();
    const GanttCssOverrides::Task& taskCss =
        css && slot < css->tasks.size() ? css->tasks.at(slot)
                                        : GanttCssOverrides::Task{};
    qreal startX = scale(task.startTime);
    qreal endX = scale(task.renderEndTime.isValid() ? task.renderEndTime : task.endTime);
    qreal rectX = startX + scene.config.leftPadding;
    qreal rectY = task.vert ? scene.config.gridLineStartPadding
                            : task.order * gap + scene.config.topPadding;
    qreal rectWidth = endX - startX;
    if (task.milestone) {
      rectX = startX + scene.config.leftPadding +
              0.5 * (scale(task.endTime) - startX) -
              0.5 * scene.config.barHeight;
      rectWidth = scene.config.barHeight;
    } else if (task.vert) {
      rectWidth = 0.08 * scene.config.barHeight;
    }
    const qreal rectHeight = task.vert
                                 ? layout.rowCount * gap + scene.config.barHeight * 2.0
                                 : scene.config.barHeight;
    int secNum = 0;
    for (int i = 0; i < categories.size(); ++i) {
      if (task.type == categories.at(i))
        secNum = i % std::max(1, scene.config.numberSectionStyles);
    }
    GanttRectGeometry rect;
    rect.id = task.id;
    rect.cssClass = ganttTaskRectClass(task, secNum);
    rect.rect = QRectF(rectX, rectY, rectWidth, rectHeight);
    rect.fill = taskFill(task, scene.style);
    rect.stroke = taskStroke(task, scene.style);
    rect.strokeWidth = 2.0;
    rect.radius = 3.0;
    rect.milestone = task.milestone;
    rect.transformOrigin = QPointF(
        scale(task.startTime) + scene.config.leftPadding +
            0.5 * (scale(task.endTime) - scale(task.startTime)),
        task.order * gap + scene.config.topPadding +
            0.5 * scene.config.barHeight);
    rect.css = taskCss.rect;
    scene.tasks.append(rect);

    const GanttTaskTextPlacement placement = ganttTaskTextPlacement(
        task, layout, scene.config, measureFamily, measureSize);
    GanttTextGeometry text;
    text.id = task.id + QStringLiteral("-text");
    text.cssClass = ganttTaskTextClass(task, secNum, placement.textWidth,
                                       placement.outside, placement.outsideLeft);
    text.text = task.task;
    text.position = QPointF(
        placement.textX,
        task.vert ? scene.config.gridLineStartPadding + layout.rowCount * gap + 60.0
                  : task.order * gap + scene.config.barHeight / 2.0 +
                        (scene.config.fontSize / 2.0 - 2.0) +
                        scene.config.topPadding);
    text.fontSize = task.vert ? 15.0 : scene.config.fontSize;
    text.fill = taskTextFill(task, placement.outside, scene.style);
    text.anchor = placement.outsideLeft
                      ? GanttTextAnchor::End
                      : (placement.outside ? GanttTextAnchor::Start
                                           : GanttTextAnchor::Middle);
    text.italic = task.milestone;
    text.bold = task.classes.contains(QStringLiteral("clickable"));
    text.css = taskCss.text;
    scene.taskLabels.append(std::move(text));

    const QString href = data.links.value(task.id);
    if (!href.isEmpty()) {
      scene.interactions.append({rect.rect, href, {}, task.task});
    }
  }

  int previousRows = 0;
  for (int category = 0; category < categories.size(); ++category) {
    const QString name = categories.at(category);
    const int rows = layout.categoryHeights.value(name);
    GanttTextGeometry text;
    text.cssClass = QStringLiteral("sectionTitle sectionTitle%1").arg(
        category % std::max(1, scene.config.numberSectionStyles));
    text.text = name;
    text.lines = name.split(QRegularExpression(QStringLiteral(R"(<br\s*/?>)"),
                                               QRegularExpression::CaseInsensitiveOption),
                            Qt::KeepEmptyParts);
    text.position = QPointF(10.0, rows * gap / 2.0 + previousRows * gap +
                                      scene.config.topPadding);
    text.fontSize = scene.config.sectionFontSize;
    text.fill = scene.style.titleColor;
    text.anchor = GanttTextAnchor::Start;
    text.lineStep = scene.config.sectionFontSize;
    if (css && category < css->sectionTitles.size())
      text.css = css->sectionTitles.at(category);
    scene.sectionLabels.append(std::move(text));
    previousRows += rows;
  }

  if (data.todayMarker != QLatin1String("off") && layout.minTime.isValid() &&
      layout.maxTime.isValid()) {
    GanttLineGeometry today;
    today.cssClass = QStringLiteral("today");
    const qreal x = scene.config.leftPadding + scale(QDateTime::currentDateTime());
    today.line = QLineF(x, scene.config.titleTopMargin, x,
                        height - scene.config.titleTopMargin);
    today.stroke = scene.style.todayLineColor;
    today.strokeWidth = 2.0;
    if (!data.todayMarker.isEmpty()) {
      QString declarations = data.todayMarker;
      declarations.replace(QLatin1Char(','), QLatin1Char(';'));
      for (const QString& declaration : declarations.split(QLatin1Char(';'))) {
        const qsizetype colon = declaration.indexOf(QLatin1Char(':'));
        if (colon < 0) continue;
        const QString property = declaration.left(colon).trimmed().toLower();
        const QString value = declaration.mid(colon + 1).trimmed();
        if (property == QLatin1String("stroke")) {
          today.stroke = value;
        } else if (property == QLatin1String("stroke-width")) {
          QString numeric = value;
          if (numeric.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
            numeric.chop(2);
          bool ok = false;
          const qreal widthValue = numeric.toDouble(&ok);
          if (ok && std::isfinite(widthValue) && widthValue >= 0.0)
            today.strokeWidth = widthValue;
        } else if (property == QLatin1String("opacity")) {
          bool ok = false;
          const qreal opacityValue = value.toDouble(&ok);
          if (ok && std::isfinite(opacityValue))
            today.opacity = std::clamp(opacityValue, 0.0, 1.0);
        }
      }
    }
    if (css) today.css = css->today;
    scene.todayLines.append(std::move(today));
  }

  scene.titleGeometry.cssClass = QStringLiteral("titleText");
  scene.titleGeometry.text = scene.title;
  scene.titleGeometry.position = QPointF(width / 2.0, scene.config.titleTopMargin);
  scene.titleGeometry.fontSize = 18.0;
  scene.titleGeometry.fill = scene.style.titleColor.isEmpty()
                                 ? scene.style.textColor : scene.style.titleColor;
  scene.titleGeometry.anchor = GanttTextAnchor::Middle;
  if (css) scene.titleGeometry.css = css->title;
  return scene;
}

}  // namespace muffin::mermaid::gantt
