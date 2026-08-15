#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"

#include <QFont>

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
  // Colorless `rect` fragments fall back to the renderer's RAW themeVariables
  // chain (rectBkgColor || actorBkg || rgba(128,128,128,0.5)) — distinct from
  // every stylesheet-consumed slot, which uses the resolved theme.
  QString rectFallbackFill = QStringLiteral("rgba(128, 128, 128, 0.5)");
  QString fragmentStroke = QStringLiteral("#666666");
  QString loopTextColor = QStringLiteral("#333333");
  QString labelFill = QStringLiteral("#eaeaea");
  QString labelStroke = QStringLiteral("#666666");
  QString labelTextColor = QStringLiteral("#333333");
  QString sequenceNumberColor = QStringLiteral("#333333");
  QString boxStroke = QStringLiteral("rgba(0,0,0,0.5)");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString actorFontFamily = QStringLiteral("Noto Sans");
  QString messageFontFamily = QStringLiteral("Noto Sans");
  QString noteFontFamily = QStringLiteral("Noto Sans");
  qreal actorFontSize = 16.0;
  qreal messageFontSize = 16.0;
  qreal noteFontSize = 16.0;
  qreal actorStrokeWidth = 2.0;
  // Upstream sequence.messageAlign / sequence.noteAlign (start/middle/end).
  // Defaults are Center, matching mermaid, so default rendering is unchanged.
  // Only notes and messages read these; participants, boxes and fragments stay
  // centered (mermaid drawLoop hard-codes anchor "middle").
  flowchart::FlowLabelAlign messageAlign = flowchart::FlowLabelAlign::Center;
  flowchart::FlowLabelAlign noteAlign = flowchart::FlowLabelAlign::Center;
  // Mermaid drawText insets left/right alignment by textMargin: noteMargin for
  // notes, wrapPadding for messages. Defaults match the sequence config (10).
  qreal noteMargin = 10.0;
  qreal wrapPadding = 10.0;
  // Per-kind CSS font weights (Qt 6 100..900 scale; Normal=400, Bold=700).
  // Resolved in sequenceStyleFromConfig: each defaults to Normal, then a truthy
  // GLOBAL fontWeight overrides all three — mirroring mermaid setConf()'s mirror
  // (sequenceDiagram): if (cnf.fontWeight) mirror to all three. Attribution
  // (verified vs mermaid 11.16.0): participant/box/menu -> actor, note -> note,
  // message/fragment -> message. The weight is written into each prepared
  // label's FlowLabelDocument::baseWeight before wrap/measure/paint; labels
  // containing Math render Normal (drawKatex ignores font-weight).
  QFont::Weight actorFontWeight = QFont::Normal;
  QFont::Weight noteFontWeight = QFont::Normal;
  QFont::Weight messageFontWeight = QFont::Normal;
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
  SvgMarkerProjection svgMarkerProjection() const override;
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
  // No handDrawn surface: mermaid 11.16's sequence renderer never branches on
  // config.look (a look:handDrawn source renders the identical classic SVG —
  // probed; only render-id counters move). `look`'s config-matrix scope
  // deliberately excludes this family.
};

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style = {},
                                 const SequencePreparedLabels& prepared = {},
                                 bool requirePrepared = false);

}  // namespace muffin::mermaid::sequence
