#pragma once

// Native parser/data model for Mermaid 11.16.0 user journey diagrams.

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::journey {

struct JourneyTask {
  QString section;
  QString type;
  QString task;
  double score = 0.0;
  QStringList people;
};

struct JourneyData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<QString> sections;
  QVector<JourneyTask> tasks;
};

struct JourneyParseError : std::runtime_error {
  int line = 0;
  JourneyParseError(const QString& message, int line = 0);
};

class JourneyDiagram {
public:
  static JourneyData parse(const QString& source);
};

}  // namespace muffin::mermaid::journey
