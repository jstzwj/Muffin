#pragma once

// Native parser/database model for Mermaid 11.16.0 Gantt diagrams.

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::gantt {

enum class GanttErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(GanttErrorKind kind) {
  switch (kind) {
  case GanttErrorKind::Lexer: return u"lexer";
  case GanttErrorKind::Parser: return u"parse";
  case GanttErrorKind::Runtime: return u"runtime";
  }
  return u"parse";
}

struct GanttTask {
  QString section;
  QString type;
  bool processed = false;
  bool manualEndTime = false;
  QDateTime renderEndTime;
  QString rawData;
  QString rawStartType;
  QString rawStartData;
  QString rawEndData;
  QString task;
  QStringList classes;
  QString id;
  QString prevTaskId;
  bool active = false;
  bool done = false;
  bool crit = false;
  bool milestone = false;
  bool vert = false;
  int order = 0;
  QDateTime startTime;
  QDateTime endTime;
};

struct GanttData {
  QString title;
  QString accTitle;
  QString accDescr;
  QString dateFormat;
  QString axisFormat;
  QString tickInterval;
  QString todayMarker;
  QStringList includes;
  QStringList excludes;
  QStringList sections;
  bool inclusiveEndDates = false;
  bool topAxis = false;
  QString weekday = QStringLiteral("sunday");
  QString weekend = QStringLiteral("saturday");
  QString displayMode;
  QHash<QString, QString> links;
  QVector<GanttTask> tasks;
};

struct GanttParseError : DiagramError {
  DiagramErrorInfo info() const override;
  int line = 0;
  int column = 1;
  GanttErrorKind kind = GanttErrorKind::Parser;

  GanttParseError(const QString& message, int line = 0, int column = 1, GanttErrorKind kind = GanttErrorKind::Parser);
};

class GanttDiagram {
public:
  static GanttData parse(const QString& source);
};

} // namespace muffin::mermaid::gantt
