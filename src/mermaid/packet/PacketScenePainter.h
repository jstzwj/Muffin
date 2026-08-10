#pragma once

class QPainter;

namespace muffin::mermaid {
struct MermaidPaintOptions;
}

namespace muffin::mermaid::packet {

struct PacketScene;

void paintPacketScene(const PacketScene& scene, QPainter& painter,
                      const MermaidPaintOptions& options);

}  // namespace muffin::mermaid::packet
