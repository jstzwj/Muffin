#pragma once

#include "mermaid/sequence/SequenceDiagram.h"

#include <QMap>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>

namespace muffin::mermaid::sequence {

struct SequenceLayoutMeasurements {
  QMap<QString, QSizeF> participants;
  QVector<QSizeF> messages;
  QVector<QSizeF> notes;
  QVector<QSizeF> fragments;
  QMap<int, QSizeF> messagesByIndex;
  QMap<int, QSizeF> notesByIndex;
  QMap<int, QSizeF> fragmentsByIndex;
};

struct SequenceLayoutParticipantInput {
  int actorIndex = -1;
  QString id;
  QString label;
  QString type;
  QSizeF measuredLabel;
};

struct SequenceLayoutMessageInput {
  int messageIndex = -1;
  QString from;
  QString to;
  QString label;
  int type = 0;
  QSizeF measuredLabel;
};

struct SequenceLayoutActivationInput {
  int messageIndex = -1;
  QString actor;
  int depth = 0;
  bool begin = false;
};

struct SequenceLayoutNoteInput {
  int messageIndex = -1;
  QString from;
  QString to;
  QString label;
  int placement = -1;
  QSizeF measuredLabel;
};

struct SequenceLayoutFragmentInput {
  int messageIndex = -1;
  QString kind;
  QString label;
  int depth = 0;
  QSizeF measuredLabel;
};

struct SequenceLayoutInput {
  QVector<SequenceLayoutParticipantInput> participants;
  QVector<SequenceLayoutMessageInput> messages;
  QVector<SequenceLayoutActivationInput> activations;
  QVector<SequenceLayoutNoteInput> notes;
  QVector<SequenceLayoutFragmentInput> fragments;
  int maximumActivationDepth = 0;
  int maximumFragmentDepth = 0;
};

struct SequenceLayoutOptions {
  qreal actorMargin = 50.0;
  qreal width = 150.0;
  qreal height = 65.0;
  qreal boxMargin = 10.0;
  qreal boxTextMargin = 5.0;
  qreal noteMargin = 10.0;
  qreal activationWidth = 10.0;
  qreal wrapPadding = 10.0;
  qreal labelBoxWidth = 50.0;
  qreal labelBoxHeight = 20.0;
  bool rightAngles = false;
};

struct SequenceLayoutParticipant {
  QString id;
  QString type;
  QString label;
  QRectF logicalRect;
  qreal margin = 0.0;
  qreal anchorX = 0.0;
  qreal lifelineStartY = 0.0;
  qreal lifelineStopY = 0.0;
  qreal topY = 0.0;
  qreal bottomY = 0.0;
  bool created = false;
  bool destroyed = false;
  QVector<QPainterPath> topShapePaths;
  QVector<QPainterPath> bottomShapePaths;
  QRectF topLabelRect;
  QRectF bottomLabelRect;
  QRectF topPaintedBounds;
  QRectF bottomPaintedBounds;
};

struct SequenceLayoutMessage {
  int messageIndex = -1;
  QString id;
  QString from;
  QString to;
  QString label;
  int type = 0;
  qreal startX = 0.0;
  qreal stopX = 0.0;
  qreal lineY = 0.0;
  QRectF labelRect;
  QString path;
  QString markerStart;
  QString markerEnd;
  bool dashed = false;
};

struct SequenceLayoutActivation {
  int messageIndex = -1;
  QString actor;
  int depth = 0;
  QRectF rect;
};

struct SequenceLayoutNote {
  int messageIndex = -1;
  QString from;
  QString to;
  int placement = -1;
  QString label;
  QRectF rect;
};

struct SequenceLayoutFragment {
  int messageIndex = -1;
  QString kind;
  QString label;
  int depth = 0;
  QRectF rect;
  QVector<qreal> sectionY;
};

struct SequenceLayoutResult {
  QVector<SequenceLayoutParticipant> participants;
  QVector<SequenceLayoutMessage> messages;
  QVector<SequenceLayoutActivation> activations;
  QVector<SequenceLayoutNote> notes;
  QVector<SequenceLayoutFragment> fragments;
  QRectF bounds;
};

// Converts the parser DB into the same semantic streams consumed by Mermaid's
// sequence renderer. Measurements are supplied by the label oracle, keeping
// text shaping errors separate from placement errors.
SequenceLayoutInput buildSequenceLayoutInput(
    const SequenceData& data, const SequenceLayoutMeasurements& measurements = {});

SequenceLayoutResult layoutSequence(const SequenceData& data,
                                    const SequenceLayoutMeasurements& measurements,
                                    SequenceLayoutOptions options = {});

}  // namespace muffin::mermaid::sequence
