#pragma once

// Native parser/database model for Mermaid 11.16.0 timeline diagrams.

#include <QString>
#include <QStringList>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::timeline {

enum class TimelineDirection { LeftToRight, TopDown };
enum class TimelineErrorKind { Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(TimelineErrorKind kind) {
  switch (kind) {
  case TimelineErrorKind::Parser:
    return u"parse";
  case TimelineErrorKind::Runtime:
    return u"runtime";
  }
  return u"parse";
}

struct TimelineTask {
  int id = 0;
  QString section;
  QString type;
  QString task;
  double score = 0.0;
  QStringList events;
};

struct TimelineData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<QString> sections;
  QVector<TimelineTask> tasks;
  TimelineDirection direction = TimelineDirection::LeftToRight;
};

struct TimelineParseError : DiagramError {
  DiagramErrorInfo info() const override;
  int line = 0;
  int column = 0;
  TimelineErrorKind kind = TimelineErrorKind::Parser;

  TimelineParseError(const QString& message, int line = 0, int column = 1,
                     TimelineErrorKind kind = TimelineErrorKind::Parser);
};

class TimelineDiagram {
public:
  static TimelineData parse(const QString& source);
};

} // namespace muffin::mermaid::timeline
