#include "mermaid/gantt/GanttScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttScenePainter.h"

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

QString cssVisibleText(QString text) {
  text.replace(QRegularExpression(QStringLiteral("[\\x09-\\x0d ]+")),
               QStringLiteral(" "));
  return text.trimmed();
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

QDateTime floorLocal(QDateTime value, const QString& unit, int weekday) {
  value.setTimeZone(QTimeZone::LocalTime);
  if (unit == QLatin1String("millisecond")) return value;
  if (unit == QLatin1String("second"))
    return QDateTime(value.date(), QTime(value.time().hour(), value.time().minute(),
                                         value.time().second()), QTimeZone::LocalTime);
  if (unit == QLatin1String("minute"))
    return QDateTime(value.date(), QTime(value.time().hour(), value.time().minute()),
                     QTimeZone::LocalTime);
  if (unit == QLatin1String("hour"))
    return QDateTime(value.date(), QTime(value.time().hour(), 0), QTimeZone::LocalTime);
  QDateTime result(value.date(), QTime(0, 0), QTimeZone::LocalTime);
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

GanttScene buildGanttScene(const GanttData& data, GanttConfig config,
                           GanttSceneStyle style) {
  GanttScene scene;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  scene.config = std::move(config);
  scene.style = std::move(style);

  QVector<GanttTask> tasks = data.tasks;
  QVector<QString> categories;
  for (const GanttTask& task : tasks)
    if (!task.vert && !categories.contains(task.type)) categories.append(task.type);

  const qreal gap = scene.config.barHeight + scene.config.barGap;
  QHash<QString, int> categoryHeights;
  int rowCount = 0;
  if (data.displayMode == QLatin1String("compact") ||
      scene.config.displayMode == QLatin1String("compact")) {
    for (const QString& category : categories) {
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
            task->order = rowCount + row;
            maxRow = std::max(maxRow, row);
            break;
          }
        }
      }
      categoryHeights.insert(category, maxRow + 1);
      rowCount += maxRow + 1;
    }
  } else {
    for (const GanttTask& task : tasks)
      if (!task.vert) ++categoryHeights[task.type];
    rowCount = std::count_if(tasks.cbegin(), tasks.cend(),
                             [](const GanttTask& task) { return !task.vert; });
  }

  const qreal width = scene.config.useWidth;
  const qreal height = 2.0 * scene.config.topPadding + rowCount * gap;
  scene.bounds = QRectF(0.0, 0.0, width, height);
  const qreal scaleWidth = width - scene.config.leftPadding - scene.config.rightPadding;

  QDateTime minTime, maxTime;
  for (const GanttTask& task : tasks) {
    if (!minTime.isValid() || task.startTime < minTime) minTime = task.startTime;
    if (!maxTime.isValid() || task.endTime > maxTime) maxTime = task.endTime;
  }
  const auto scale = [&](const QDateTime& value) {
    return roundedScale(value, minTime, maxTime, scaleWidth);
  };

  std::stable_sort(tasks.begin(), tasks.end(), [](const GanttTask& a, const GanttTask& b) {
    return a.startTime < b.startTime;
  });
  std::stable_sort(tasks.begin(), tasks.end(), [](const GanttTask& a, const GanttTask& b) {
    return a.vert == b.vert ? false : !a.vert;
  });

  // Excluded local calendar-day runs are painted before the grid.
  if (minTime.isValid() && maxTime.isValid() &&
      (!data.excludes.isEmpty() || !data.includes.isEmpty()) &&
      minTime.daysTo(maxTime) <= 366 * 5 + 2) {
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
      return data.excludes.contains(iso) || data.excludes.contains(names.at(day - 1));
    };
    QDate runStart, runEnd;
    for (QDate date = minTime.date(); date <= maxTime.date(); date = date.addDays(1)) {
      if (isExcluded(date)) {
        if (!runStart.isValid()) runStart = date;
        runEnd = date;
      } else if (runStart.isValid()) {
        const QDateTime start(runStart, QTime(0, 0), QTimeZone::LocalTime);
        const QDateTime end(runEnd, QTime(23, 59, 59, 999), QTimeZone::LocalTime);
        scene.excludes.append({QStringLiteral("exclude-") + runStart.toString(Qt::ISODate),
                               QStringLiteral("exclude-range"),
                               QRectF(scale(start) + scene.config.leftPadding,
                                      scene.config.gridLineStartPadding,
                                      scale(end) - scale(start),
                                      height - scene.config.topPadding -
                                          scene.config.gridLineStartPadding),
                               scene.style.excludeBkgColor});
        runStart = {}; runEnd = {};
      }
    }
    if (runStart.isValid()) {
      const QDateTime start(runStart, QTime(0, 0), QTimeZone::LocalTime);
      const QDateTime end(runEnd, QTime(23, 59, 59, 999), QTimeZone::LocalTime);
      scene.excludes.append({QStringLiteral("exclude-") + runStart.toString(Qt::ISODate),
                             QStringLiteral("exclude-range"),
                             QRectF(scale(start) + scene.config.leftPadding,
                                    scene.config.gridLineStartPadding,
                                    scale(end) - scale(start),
                                    height - scene.config.topPadding -
                                        scene.config.gridLineStartPadding),
                             scene.style.excludeBkgColor});
    }
  }

  const QString axisFormat = !data.axisFormat.isEmpty()
                                 ? data.axisFormat
                                 : (!scene.config.axisFormat.isEmpty()
                                        ? scene.config.axisFormat
                                        : QStringLiteral("%Y-%m-%d"));
  const QString tickSource = !data.tickInterval.isEmpty()
                                 ? data.tickInterval : scene.config.tickInterval;
  QVector<QDateTime> ticks = explicitTicks(minTime, maxTime, tickSource,
                                            !data.weekday.isEmpty()
                                                ? data.weekday : scene.config.weekday);
  if (ticks.isEmpty() && minTime.isValid() && maxTime.isValid())
    ticks = automaticTicks(minTime, maxTime);
  const auto addAxis = [&](qreal y, bool top) {
    const qreal tickEnd = top
                              ? y + height - scene.config.topPadding -
                                        scene.config.gridLineStartPadding
                              : y - height + scene.config.topPadding +
                                        scene.config.gridLineStartPadding;
    for (const QDateTime& tick : ticks) {
      // d3-axis offsets one-pixel strokes by 0.5 CSS px for a 1x display.
      const qreal x = scene.config.leftPadding + scale(tick) + 0.5;
      scene.gridLines.append({{}, QStringLiteral("tick"), QLineF(x, y, x, tickEnd),
                              scene.style.gridColor, 1.0, 0.8});
      GanttTextGeometry label;
      label.cssClass = QStringLiteral("tick-label");
      label.text = formatTick(tick, axisFormat);
      label.position = QPointF(x, top ? y - 6.0 : y + 19.0);
      label.fontSize = 10.0;
      label.fill = scene.style.textColor;
      label.anchor = GanttTextAnchor::Middle;
      scene.gridLabels.append(std::move(label));
    }
  };
  addAxis(height - 50.0, false);
  if (data.topAxis || scene.config.topAxis) addAxis(scene.config.topPadding, true);

  QVector<int> uniqueOrders;
  for (const GanttTask& task : tasks)
    if (!task.vert && !uniqueOrders.contains(task.order)) uniqueOrders.append(task.order);
  for (int order : uniqueOrders) {
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(),
                                    [order](const GanttTask& task) {
                                      return !task.vert && task.order == order;
                                    });
    if (found == tasks.cend()) continue;
    const int category = int(std::max<qsizetype>(0, categories.indexOf(found->type)));
    scene.sections.append({{}, QStringLiteral("section section%1").arg(
                                   category % std::max(1, scene.config.numberSectionStyles)),
                           QRectF(0.0, order * gap + scene.config.topPadding - 2.0,
                                  width - scene.config.rightPadding / 2.0, gap),
                           sectionFill(category, scene.style), {}, 0.0, 0.2});
  }

  const editor::CssPixelFont taskFont = editor::makeUnhintedCssPixelFont(
      scene.style.fontFamily, scene.config.fontSize);
  QFontMetricsF taskMetrics(taskFont.font);
  for (const GanttTask& task : tasks) {
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
                                 ? rowCount * gap + scene.config.barHeight * 2.0
                                 : scene.config.barHeight;
    GanttRectGeometry rect;
    rect.id = task.id;
    rect.cssClass = QStringLiteral("task");
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
    scene.tasks.append(rect);

    const QString visible = cssVisibleText(task.task);
    const qreal textWidth = taskMetrics.horizontalAdvance(visible) * taskFont.scale;
    qreal textStartX = startX;
    qreal positionEndX = endX;
    qreal classEndX = scale(task.endTime);
    if (task.milestone) {
      textStartX = rectX - scene.config.leftPadding;
      positionEndX = textStartX + scene.config.barHeight;
      classEndX = textStartX + scene.config.barHeight;
    }
    bool outside = !task.vert && textWidth > classEndX - textStartX;
    bool outsideLeft = outside &&
                       classEndX + textWidth + 1.5 * scene.config.leftPadding > width;
    qreal textX;
    if (task.vert) textX = scale(task.startTime) + scene.config.leftPadding;
    else if (outsideLeft) textX = textStartX + scene.config.leftPadding - 5.0;
    else if (outside) textX = positionEndX + scene.config.leftPadding + 5.0;
    else textX = (positionEndX - textStartX) / 2.0 + textStartX + scene.config.leftPadding;
    GanttTextGeometry text;
    text.id = task.id + QStringLiteral("-text");
    text.cssClass = outsideLeft ? QStringLiteral("taskTextOutsideLeft")
                                : (outside ? QStringLiteral("taskTextOutsideRight")
                                           : QStringLiteral("taskText"));
    text.text = task.task;
    text.position = QPointF(
        textX,
        task.vert ? scene.config.gridLineStartPadding + rowCount * gap + 60.0
                  : task.order * gap + scene.config.barHeight / 2.0 +
                        (scene.config.fontSize / 2.0 - 2.0) +
                        scene.config.topPadding);
    text.fontSize = task.vert ? 15.0 : scene.config.fontSize;
    text.fill = taskTextFill(task, outside, scene.style);
    text.anchor = outsideLeft ? GanttTextAnchor::End
                              : (outside ? GanttTextAnchor::Start
                                         : GanttTextAnchor::Middle);
    text.italic = task.milestone;
    text.bold = task.classes.contains(QStringLiteral("clickable"));
    scene.taskLabels.append(std::move(text));

    const QString href = data.links.value(task.id);
    if (!href.isEmpty()) {
      scene.interactions.append({rect.rect, href, {}, task.task});
    }
  }

  int previousRows = 0;
  for (int category = 0; category < categories.size(); ++category) {
    const QString name = categories.at(category);
    const int rows = categoryHeights.value(name);
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
    scene.sectionLabels.append(std::move(text));
    previousRows += rows;
  }

  if (data.todayMarker != QLatin1String("off") && minTime.isValid() &&
      maxTime.isValid()) {
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
    scene.todayLines.append(std::move(today));
  }

  scene.titleGeometry.cssClass = QStringLiteral("titleText");
  scene.titleGeometry.text = scene.title;
  scene.titleGeometry.position = QPointF(width / 2.0, scene.config.titleTopMargin);
  scene.titleGeometry.fontSize = 18.0;
  scene.titleGeometry.fill = scene.style.titleColor.isEmpty()
                                 ? scene.style.textColor : scene.style.titleColor;
  scene.titleGeometry.anchor = GanttTextAnchor::Middle;
  return scene;
}

}  // namespace muffin::mermaid::gantt
