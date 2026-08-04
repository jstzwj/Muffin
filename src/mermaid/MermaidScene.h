#pragma once

// Common base for every diagram scene (flowchart, sequence, class, state, er,
// requirement).
//
// The render cache and exporters hold scenes through this uniform pointer so
// they can paint and measure bounds without type-switching on the diagram
// family. Concrete scenes inherit and delegate paint() to their existing free
// painter; sceneBounds() returns the pre-computed `bounds` member.
//
// This is the foundational seam of the 1:1-parity architecture
// (docs/mermaid-architecture.md, L1): the Diagram registry, render cache, and
// exporters use it to treat every native family uniformly.

#include "mermaid/MermaidPaintOptions.h"

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid {

// A hit/interaction region in scene coordinates. Consumers (SVG export, editor
// hit-test) iterate these without knowing the diagram family. Each region is
// exactly one role:
//   - flow node: href/toolTip set (requiresOpenMenu/togglesMenu empty)
//   - sequence menu item: requiresOpenMenu = the actor whose menu must be open
//   - sequence actor toggle: togglesMenu = the actor id to toggle on click
// href is RAW — consumers apply isSafeUrl themselves so the policy is single-sourced.
struct InteractionRegion {
  QRectF bounds;
  QString href;
  QString toolTip;          // editor hover tooltip (flow node tooltip; empty for sequence items)
  QString accessibleLabel;  // SVG <title>/aria-label (flow node tooltip; sequence item label)
  QString requiresOpenMenu;
  QString togglesMenu;
};

struct MermaidScene {
  virtual ~MermaidScene() = default;

  // Diagram bounds in scene coordinates (the concrete scene's `bounds`).
  virtual QRectF sceneBounds() const = 0;

  // Paint the scene. Callers that need a non-default paint mode (e.g. the
  // category-mask / golden pipeline) still call the concrete free painter
  // directly; this entry covers the normal color render path used by the
  // image and SVG backends.
  virtual void paint(QPainter& painter, const MermaidPaintOptions& options) const = 0;

  // Canonical structural+geometric+style dump shared by (a) the deterministic
  // SVG root-id digest (MermaidSvgExporter) and (b) parity/regression oracles.
  // Each concrete scene serializes bounds + its element arrays (id/geometry/
  // style/label), with numbers rounded to 0.001 — mirrors FlowScene::toJson.
  virtual QJsonObject toJsonObject() const = 0;

  // The scene's base render extent. The generic image/SVG-canvas paths add the
  // diagram padding uniformly; they do not know the family. Default: scene
  // bounds. SequenceScene overrides to its resolved viewport rect.
  virtual QRectF renderBounds() const { return sceneBounds(); }

  // True if the scene has time-animated elements (e.g. animated flowchart edges).
  // Default false; FlowScene overrides. Drives the editor's repaint timer.
  virtual bool hasAnimation() const { return false; }

  // Hit/interaction regions in scene coordinates (empty by default; FlowScene
  // and SequenceScene precompute and return a reference). Class/state/er/
  // requirement inherit the empty default at no cost. Consumers apply their own
  // visibility/safe-URL rules. Returns a reference so the editor hot path
  // (mouse-move hit-test) does not reallocate.
  virtual const QVector<InteractionRegion>& interactionRegions() const {
    static const QVector<InteractionRegion> kEmpty;
    return kEmpty;
  }

  // Sequence forceMenus: when true, menus are rendered open and their item links
  // are always active (no actor-toggle). Default false.
  virtual bool menusAlwaysOpen() const { return false; }
};

}  // namespace muffin::mermaid
