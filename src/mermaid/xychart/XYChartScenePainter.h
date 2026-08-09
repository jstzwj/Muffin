#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
namespace xychart {
struct XYChartScene;

void paintXYChartScene(const XYChartScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options);

}  // namespace xychart
}  // namespace muffin::mermaid
