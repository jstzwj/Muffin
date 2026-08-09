#pragma once

// Native parser/database model for Mermaid 11.16.0 `xychart[-beta]` diagrams.

#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>
#include <stdexcept>

namespace muffin::mermaid::xychart {

enum class XYChartOrientation { Vertical, Horizontal };
enum class XYChartAxisType { Band, Linear };
enum class XYChartPlotType { Line, Bar };

struct XYChartAxisData {
  XYChartAxisType type = XYChartAxisType::Band;
  QString title;
  QStringList categories;
  double min = std::numeric_limits<double>::infinity();
  double max = -std::numeric_limits<double>::infinity();
};

struct XYChartPoint {
  QString category;
  double value = 0.0;
  bool valueDefined = true;
};

struct XYChartPlotData {
  XYChartPlotType type = XYChartPlotType::Line;
  QVector<XYChartPoint> points;
  QStringList pointLabels;
  bool hasPointLabels = false;
  int paletteIndex = 0;
};

struct XYChartData {
  XYChartAxisData xAxis;
  XYChartAxisData yAxis{XYChartAxisType::Linear};
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<XYChartPlotData> plots;
  XYChartOrientation orientation = XYChartOrientation::Vertical;
  bool hasOrientationDirective = false;
  bool hasTitleDirective = false;
};

struct XYChartParseError : std::runtime_error {
  int line = 0;
  int column = 0;
  XYChartParseError(const QString& message, int line = 0, int column = 1);
};

class XYChartDiagram {
public:
  static XYChartData parse(const QString& source);
};

}  // namespace muffin::mermaid::xychart
