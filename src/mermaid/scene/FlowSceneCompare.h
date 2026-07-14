#pragma once

// Level-3 category-driven pixel comparison (milestone G3, the centerpiece).
//
// The scene is fully trusted at this point: Level 1 (exact colours/strings) +
// Level 2 (≤0.002px geometry) have certified it. So Level 3 only has to verify
// RASTERIZATION fidelity — and it does so by classifying every pixel into a
// category derived SOLELY from the trusted scene, then applying that category's
// fixed rule. The classifier is a category MASK rendered by the painter itself
// (PaintMode::CategoryMask), so it can never drift from what is actually drawn.
//
// Categories + rules (thresholds fixed per category in CompareThresholds, never
// per-fixture — docs/mermaid-flowchart-remaining-plan.md §10):
//   INTERIOR  mask∈{NODE,CLUSTER,EDGE_LABEL_BG} & native alpha==255  → exact RGB (tol 0)
//   BOUNDARY  non-EMPTY & 0<native alpha<255                          → linear RGBA diff ≤ T, deviant ratio ≤ R
//   TEXT      inside a label bbox                                      → per-label alpha-mask IoU ≥ T_glyph
//   EMPTY     mask transparent                                        → golden transparent
//
// Coordinate frame: the manifest viewBox is the single frame. Native colour,
// native mask, and the Chrome golden are all viewBox-sized and registered to the
// same origin, so pixel (x,y) is the same scene coordinate in all three. (The
// ≤1px Chrome-vs-Qt stroke-placement residual lands in the BOUNDARY category.)

#include "mermaid/scene/FlowScene.h"

#include <QImage>
#include <QString>

namespace muffin::mermaid::flowscene {

struct ViewBox {
  qreal x = 0.0, y = 0.0, width = 0.0, height = 0.0;
};

struct CompareThresholds {
  // INTERIOR: at most this fraction of deep-interior pixels may mismatch.
  // Accommodates residual classification artifacts at thick strokes / text over
  // fill (sub-pixel Chrome-vs-Qt placement); a real colour-derivation bug moves
  // a whole region → a far higher ratio.
  qreal interiorMaxMismatchRatio = 0.04;
  // BOUNDARY: a pixel is "deviant" if (both sides have ink and) a colour channel
  // differs by more than this.
  int boundaryMaxRgbaDiff = 80;
  // BOUNDARY: at most this fraction of boundary pixels may be deviant.
  qreal boundaryMaxDeviantRatio = 0.20;
  // TEXT: minimum alpha-mask IoU per label (1x; tighten at higher DPR).
  qreal textGlyphIou = 0.78;
  // TEXT: alpha threshold for a pixel counting as ink.
  int textInkAlpha = 96;
  // EMPTY: a mask-transparent pixel may have at most this golden alpha.
  int emptyMaxGoldenAlpha = 24;
  // EMPTY: at most this fraction of content pixels (interior+boundary) may be
  // golden-opaque where the native mask is empty (silhouette-fringe bleed).
  qreal emptyMaxMismatchRatio = 0.06;
};

struct CompareReport {
  bool passed = true;
  QString summary;
  qint64 interior = 0, interiorMismatch = 0;
  qint64 boundary = 0, boundaryDeviant = 0;
  qint64 emptyPixels = 0, emptyMismatch = 0;
  int labels = 0, labelFailures = 0;
  QString worstLabel;
  qreal worstLabelIou = 1.0;
  // Written on failure (expected/actual/diff/mask PNGs) when failureDir is set.
  QString expectedPath, actualPath, diffPath, maskPath;
};

// Compare the native render of `scene` against the Chrome `golden` PNG. Both are
// normalized to their actual painted-content tight bbox (origin/margin/mermaid
// padding stripped) and center-aligned, so pixel (x,y) is the same content
// location in both regardless of how each side computed its bounds.
// `failureDir` (optional): where to dump diagnostic PNGs on failure.
CompareReport compareLevel3(const FlowScene& scene, const QImage& golden, const QString& fontFamily,
                            const CompareThresholds& thresholds = {},
                            const QString& failureDir = QString(), bool enforceInterior = true,
                            qreal dpr = 1.0);

}  // namespace muffin::mermaid::flowscene
