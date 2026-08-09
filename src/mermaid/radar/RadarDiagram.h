#pragma once

// Native parser/data model for Mermaid 11.16.0 `radar-beta` diagrams.

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::radar {

struct RadarAxis {
  QString name;
  QString label;
};

struct RadarCurve {
  QString name;
  QString label;
  QVector<double> entries;
};

struct RadarOptions {
  bool showLegend = true;
  double ticks = 5.0;
  double max = 0.0;
  bool hasMax = false;
  double min = 0.0;
  QString graticule = QStringLiteral("circle");
};

struct RadarData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<RadarAxis> axes;
  QVector<RadarCurve> curves;
  RadarOptions options;
};

struct RadarParseError : std::runtime_error {
  int line = 0;
  int column = 0;
  RadarParseError(const QString& message, int line = 0, int column = 1);
};

class RadarDiagram {
public:
  static RadarData parse(const QString& source);
};

}  // namespace muffin::mermaid::radar
