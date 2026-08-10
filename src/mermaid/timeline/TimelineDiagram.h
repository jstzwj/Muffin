#pragma once

// Native parser/database model for Mermaid 11.16.0 timeline diagrams.

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::timeline {

enum class TimelineDirection { LeftToRight, TopDown };
enum class TimelineErrorKind { Parser, Runtime };

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

struct TimelineParseError : std::runtime_error {
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

}  // namespace muffin::mermaid::timeline
