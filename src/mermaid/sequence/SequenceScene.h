#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"

namespace muffin::mermaid::sequence {

struct SequenceSceneStyle {
  QString actorFill = QStringLiteral("#ECECFF");
  QString actorStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString actorTextColor = QStringLiteral("#333333");
  QString signalColor = QStringLiteral("#333333");
  QString signalTextColor = QStringLiteral("#333333");
  QString lifelineColor = QStringLiteral("#999999");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString noteTextColor = QStringLiteral("#333333");
  QString activationFill = QStringLiteral("#f4f4f4");
  QString activationStroke = QStringLiteral("#666666");
  QString fragmentFill = QStringLiteral("transparent");
  QString fragmentStroke = QStringLiteral("#666666");
  QString loopTextColor = QStringLiteral("#333333");
  QString labelFill = QStringLiteral("#eaeaea");
  QString labelStroke = QStringLiteral("#666666");
  QString labelTextColor = QStringLiteral("#333333");
  QString sequenceNumberColor = QStringLiteral("#333333");
  QString boxStroke = QStringLiteral("rgba(0,0,0,0.5)");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
};

struct SequencePreparedLabels {
  QMap<int, SequenceLabelDocument> boxesByIndex;
  QMap<QString, SequenceLabelDocument> participantsById;
  QMap<int, SequenceLabelDocument> messagesByIndex;
  QMap<int, SequenceLabelDocument> notesByIndex;
  QMap<int, SequenceLabelDocument> fragmentKindsByIndex;
  QMap<int, SequenceLabelDocument> fragmentsByIndex;
  QMap<QString, SequenceLabelDocument> menuItemsByKey;
};

struct SequenceSceneMenuItem {
  QString label;
  QString link;
  QRectF hitRect;
  QRectF labelRect;
  SequenceLabelDocument labelDocument;
};

struct SequenceSceneMenu {
  QString actorId;
  QRectF panelRect;
  QVector<SequenceSceneMenuItem> items;
};

// Immutable geometry consumed by the sequence painter. The scene never reads
// the parser DB and never performs placement.
struct SequenceScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  // The resolved viewport rect (logicalBounds + configured margins), computed
  // once at build time. renderBounds returns it so the generic image/canvas
  // paths treat sequence like any other family without a dispatch branch.
  QRectF renderBounds() const override {
    return viewportRect.isValid() ? viewportRect : sceneBounds();
  }
  bool menusAlwaysOpen() const override { return forceMenus; }
  const QVector<InteractionRegion>& interactionRegions() const override { return interactionRegions_; }

  QRectF bounds;
  QRectF logicalBounds;
  QRectF viewportRect;
  QVector<SequenceLayoutBox> boxes;
  QVector<SequenceLabelDocument> boxLabels;
  QVector<SequenceLayoutParticipant> participants;
  QVector<SequenceLabelDocument> participantLabels;
  QVector<SequenceLayoutMessage> messages;
  QVector<SequenceLabelDocument> messageLabels;
  QVector<SequenceLayoutActivation> activations;
  QVector<SequenceLayoutNote> notes;
  QVector<SequenceLabelDocument> noteLabels;
  QVector<SequenceLayoutFragment> fragments;
  QVector<SequenceLabelDocument> fragmentKindLabels;
  QVector<SequenceLabelDocument> fragmentLabels;
  QVector<SequenceLayoutNumber> sequenceNumbers;
  QVector<SequenceSceneMenu> menus;
  bool forceMenus = false;
  QVector<InteractionRegion> interactionRegions_;  // precomputed at build
  SequenceSceneStyle style;
  // handDrawn (rough) look — gated in the painter, only set when the diagram
  // config requests `look: handDrawn`. Default rendering is unaffected.
  bool handDrawn = false;
  quint32 handDrawnSeed = 0;
};

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style = {},
                                 const SequencePreparedLabels& prepared = {},
                                 bool requirePrepared = false);

}  // namespace muffin::mermaid::sequence
