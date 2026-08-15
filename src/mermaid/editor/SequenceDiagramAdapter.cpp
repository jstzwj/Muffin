#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

bool isSequenceFragment(int type) {
  return type == 10 || type == 12 || type == 15 || type == 19 ||
         type == 22 || type == 27 || type == 30 || type == 32;
}

sequence::SequenceSceneStyle sequenceStyleFromConfig(const QJsonObject& config) {
  sequence::SequenceSceneStyle style;
  style.fontFamily = MermaidFontRegistry::cssFamilyStack();
  // The sequence stylesheet (styles.js getStyles) consumes the RESOLVED theme
  // (updateCurrentConfig replaces config.themeVariables with
  // getThemeVariables(userOverrides) before render), so every palette slot
  // comes from flowtheme — including user themeVariables overrides, which
  // resolveFlowTheme replays after the per-theme derivation.
  const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
      themeIdFromName(themeFromConfig(config)), themeOverrides(config));
  style.actorFill = themeVars.actorBkg;
  style.actorStroke = themeVars.actorBorder;
  style.textColor = themeVars.textColor;
  style.actorTextColor = themeVars.actorTextColor;
  style.signalColor = themeVars.signalColor;
  style.signalTextColor = themeVars.signalTextColor;
  style.lifelineColor = themeVars.actorLineColor;
  style.noteFill = themeVars.noteBkgColor;
  style.noteStroke = themeVars.noteBorderColor;
  style.noteTextColor = themeVars.noteTextColor;
  style.activationFill = themeVars.activationBkgColor;
  style.activationStroke = themeVars.activationBorderColor;
  // `.loopLine` (the fragment/loop border lines) takes labelBoxBorderColor
  // from the resolved theme — probed: default renders #9370DB dashed 2,2.
  style.fragmentStroke = themeVars.labelBoxBorderColor;
  style.loopTextColor = themeVars.loopTextColor;
  style.labelFill = themeVars.labelBoxBkgColor;
  style.labelStroke = themeVars.labelBoxBorderColor;
  style.labelTextColor = themeVars.labelTextColor;
  style.sequenceNumberColor = themeVars.sequenceNumberColor;
  // `box` statement rects carry only class "rect"; `g rect.rect { stroke:
  // nodeBorder }` paints the border from the resolved theme.
  style.boxStroke = themeVars.nodeBorder;
  // Loops/alt/opt boxes have NO background upstream (drawLoop emits only the
  // four loopLine borders + label polygon), so fragmentFill stays transparent.
  // The colorless `rect` fragment reads config.themeVariables (the MERGED
  // object: user override ?: resolved theme value) with JS-|| fallbacks:
  // `rectBkgColor || actorBkg || "rgba(128,128,128,0.5)"` — the resolved
  // rectBkgColor (tertiaryColor-derived) is always present for built-ins, so
  // the rgba tier only engages for empty-string overrides.
  style.fragmentFill = QStringLiteral("transparent");
  style.rectFallbackFill = !themeVars.rectBkgColor.isEmpty()
                               ? themeVars.rectBkgColor
                               : (!themeVars.actorBkg.isEmpty()
                                      ? themeVars.actorBkg
                                      : QStringLiteral("rgba(128, 128, 128, 0.5)"));
  // `.actor { stroke-width: ${options.strokeWidth ?? 1} }` (sequence styles.js):
  // the themeVariables strokeWidth — 2 for the neo/redux light family, 1 for
  // every other built-in theme. Probed vs 11.16.0 (the previous hardcoded 2px
  // mismatched the computed 1px default).
  style.actorStrokeWidth = themeVars.strokeWidth;
  if (style.actorStrokeWidth <= 0.0) style.actorStrokeWidth = 1.0;
  const QHash<QString, QString> theme = themeOverrides(config);
  if (theme.contains(QStringLiteral("fontFamily")))
    style.fontFamily = theme.value(QStringLiteral("fontFamily"));
  if (theme.contains(QStringLiteral("fontSize")))
    style.fontSize = pixelValue(
        theme.value(QStringLiteral("fontSize")), style.fontSize);
  style.actorFontFamily = style.messageFontFamily = style.noteFontFamily =
      style.fontFamily;
  style.actorFontSize = style.messageFontSize = style.noteFontSize =
      style.fontSize;

  csscascade::ElementStyle rootFallback;
  rootFallback.fill = style.textColor;
  rootFallback.stroke = QStringLiteral("none");
  rootFallback.strokeWidth = QStringLiteral("1px");
  rootFallback.color = QStringLiteral("black");
  rootFallback.fontFamily = style.fontFamily;
  rootFallback.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
  rootFallback.fontWeight = QStringLiteral("400");
  csscascade::ElementStyle actorFallback = rootFallback;
  actorFallback.fill = style.actorFill;
  actorFallback.stroke = style.actorStroke;
  actorFallback.strokeWidth =
      QString::number(style.actorStrokeWidth) + QStringLiteral("px");
  csscascade::ElementStyle messageFallback = rootFallback;
  messageFallback.fill = style.signalTextColor;
  const QVector<csscascade::ElementInput> cssElements = {
      {QStringLiteral("svg"), {}, QStringLiteral("svg"),
       QStringLiteral("diagram-root"), {QStringLiteral("sequenceDiagram")},
       {}, rootFallback, {}},
      {QStringLiteral("root"), QStringLiteral("svg"), QStringLiteral("g"),
       {}, {QStringLiteral("root")}, {}, rootFallback, {}},
      {QStringLiteral("actor"), QStringLiteral("root"), QStringLiteral("rect"),
       {}, {QStringLiteral("actor")}, {}, actorFallback, {}},
      {QStringLiteral("message"), QStringLiteral("root"), QStringLiteral("text"),
       {}, {QStringLiteral("messageText")}, {}, messageFallback, {}}};
  const QString themeCss = config.value(QStringLiteral("themeCSS")).toString();
  if (!themeCss.trimmed().isEmpty()) {
    const auto projected = csscascade::resolveElements(themeCss, cssElements);
    const csscascade::ElementStyle actor = projected.value(
        QStringLiteral("actor"), actorFallback);
    const csscascade::ElementStyle message = projected.value(
        QStringLiteral("message"), messageFallback);
    style.actorFill = actor.fill;
    style.actorStroke = actor.stroke;
    style.actorStrokeWidth = cssStrokeWidthPx(actor.strokeWidth, {}, 0.0);
    style.actorFontFamily = actor.fontFamily;
    style.actorFontSize = cssFontSizePx(actor.fontSize, {});
    style.messageFontFamily = message.fontFamily;
    style.messageFontSize = cssFontSizePx(message.fontSize, {});
    style.signalTextColor = message.fill;
  }
  // sequence.messageAlign / sequence.noteAlign (start/middle/end). Defaults are
  // Center, so an absent or unrecognized value leaves rendering unchanged.
  const QJsonObject sequenceSection =
      config.value(QStringLiteral("sequence")).toObject();
  const auto readAlign = [&sequenceSection](const QString& key) {
    const QString value = sequenceSection.value(key).toString();
    if (value == QLatin1String("left")) return flowchart::FlowLabelAlign::Left;
    if (value == QLatin1String("right")) return flowchart::FlowLabelAlign::Right;
    return flowchart::FlowLabelAlign::Center;
  };
  style.messageAlign = readAlign(QStringLiteral("messageAlign"));
  style.noteAlign = readAlign(QStringLiteral("noteAlign"));
  // Margins that drawText insets left/right alignment by (noteMargin for notes,
  // wrapPadding for messages). Absent keys keep the 10 px defaults.
  style.noteMargin = configNumber(sequenceSection, QStringLiteral("noteMargin"), 10.0);
  style.wrapPadding = configNumber(sequenceSection, QStringLiteral("wrapPadding"), 10.0);
  // Per-kind CSS font weights, each defaulting to Normal. A truthy GLOBAL
  // fontWeight overrides all three — mirroring mermaid setConf()'s mirror
  // (sequenceDiagram): if (cnf.fontWeight) the global is copied into all three
  // per-label weights, so a set global wins over any per-kind value.
  const QJsonValue globalWeight = config.value(QStringLiteral("fontWeight"));
  style.actorFontWeight =
      cssFontWeightToQt(sequenceSection.value(QStringLiteral("actorFontWeight")), QFont::Normal);
  style.noteFontWeight =
      cssFontWeightToQt(sequenceSection.value(QStringLiteral("noteFontWeight")), QFont::Normal);
  style.messageFontWeight =
      cssFontWeightToQt(sequenceSection.value(QStringLiteral("messageFontWeight")), QFont::Normal);
  if (truthyConfigValue(globalWeight)) {
    const QFont::Weight resolved = cssFontWeightToQt(globalWeight, QFont::Normal);
    style.actorFontWeight = style.noteFontWeight = style.messageFontWeight = resolved;
  }
  return style;
}

// sequenceDiagram behind the Diagram contract. Body is the former
// renderSource() sequence branch, verbatim.
struct SequenceDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("sequence")}; }
  QString cssClass() const override { return QStringLiteral("sequenceDiagram"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      (void)theme;  // sequence theme is resolved from config, not the requested theme
      const sequence::SequenceDiagram diagram = sequence::SequenceDiagram::parse(pre.code);
      sequence::SequenceLayoutMeasurements measurements;
      sequence::SequencePreparedLabels preparedLabels;
      const QJsonObject sequenceConfig = pre.config.value(QStringLiteral("sequence")).toObject();
      sequence::SequenceSceneStyle style = sequenceStyleFromConfig(pre.config);
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, diagram.data().title, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          style.fontSize);
      const qreal labelLineHeight = style.fontSize * (22.0 / 16.0);
      const qreal actorMargin = configNumber(sequenceConfig, QStringLiteral("actorMargin"), 50.0);
      const qreal actorWidth = configNumber(sequenceConfig, QStringLiteral("width"), 150.0);
      const qreal wrapPadding = configNumber(sequenceConfig, QStringLiteral("wrapPadding"), 10.0);
      const bool globalWrap = sequenceConfig.contains(QStringLiteral("wrap"))
          ? sequenceConfig.value(QStringLiteral("wrap")).toBool(false)
          : pre.config.value(QStringLiteral("wrap")).toBool(false);
      const auto labelDocument = [&](const QString& text, sequence::SequenceLabelKind kind) {
        auto document = sequence::parseSequenceLabel(text, kind);
        QFont::Weight weight = QFont::Normal;
        switch (kind) {
          case sequence::SequenceLabelKind::Participant:
          case sequence::SequenceLabelKind::Box: weight = style.actorFontWeight; break;
          case sequence::SequenceLabelKind::Note: weight = style.noteFontWeight; break;
          case sequence::SequenceLabelKind::Message:
          case sequence::SequenceLabelKind::Fragment: weight = style.messageFontWeight; break;
        }
        // Math labels render Normal: mermaid drawKatex() ignores font-weight, so
        // a note/message containing $$ keeps Normal regardless of the weight.
        document.richText.baseWeight =
            document.richText.math.isEmpty() ? weight : QFont::Normal;
        return document;
      };
      const auto prepare = [&](sequence::SequenceLabelDocument label) {
        return sequence::prepareSequenceLabel(std::move(label), style.fontSize);
      };
      const auto measure = [&](const sequence::SequenceLabelDocument& label) {
        return sequence::layoutSequenceLabel(
            label, style.fontFamily, style.fontSize, labelLineHeight).size;
      };
      for (const auto& actor : diagram.data().actors) {
        auto document = labelDocument(actor.description, sequence::SequenceLabelKind::Participant);
        if (actor.wrap || globalWrap) {
          document = sequence::wrapSequenceLabel(std::move(document),
              style.fontFamily, style.fontSize,
              std::max(1.0, actorWidth - 2.0 * wrapPadding));
          measurements.participantDisplayById.insert(actor.id, document.richText.text);
        }
        document = prepare(std::move(document));
        measurements.participants.insert(actor.id, measure(document));
        preparedLabels.participantsById.insert(actor.id, std::move(document));
        for (auto it = actor.links.begin(); it != actor.links.end(); ++it) {
          auto menuDocument = prepare(labelDocument(
              it.key(), sequence::SequenceLabelKind::Participant));
          const QString key = sequence::sequenceMenuLabelKey(actor.id, it.key());
          measurements.menuItems.insert(key, measure(menuDocument));
          preparedLabels.menuItemsByKey.insert(key, std::move(menuDocument));
        }
      }
      for (qsizetype index = 0; index < diagram.data().boxes.size(); ++index) {
        auto document = prepare(labelDocument(
            diagram.data().boxes.at(index).name, sequence::SequenceLabelKind::Box));
        measurements.boxes.append(measure(document));
        preparedLabels.boxesByIndex.insert(
            static_cast<int>(index), std::move(document));
      }
      for (qsizetype index = 0; index < diagram.data().messages.size(); ++index) {
        const auto& message = diagram.data().messages[index];
        const bool note = message.type == 2;
        const bool fragment = isSequenceFragment(message.type);
        const auto kind = note ? sequence::SequenceLabelKind::Note
            : fragment ? sequence::SequenceLabelKind::Fragment
                       : sequence::SequenceLabelKind::Message;
        auto document = labelDocument(message.message.toString(), kind);
        const bool wrapped = !fragment && (message.wrap || globalWrap);
        if (wrapped) {
          auto marginDocument = sequence::wrapSequenceLabel(
              document, style.fontFamily, style.fontSize,
              std::max(1.0, actorWidth - 2.0 * wrapPadding));
          marginDocument = prepare(std::move(marginDocument));
          if (note)
            measurements.marginNotesByIndex.insert(
                static_cast<int>(index), measure(marginDocument));
          else
            measurements.marginMessagesByIndex.insert(
                static_cast<int>(index), measure(marginDocument));
          // buildNoteModel() performs its second wrap against conf.width.
          // Signal widths depend on activation endpoints and are resolved
          // after the provisional horizontal layout below.
          if (note)
            document = sequence::wrapSequenceLabel(
                std::move(document), style.fontFamily, style.fontSize, actorWidth);
        }
        document = prepare(std::move(document));
        const QSizeF size = measure(document);
        if (!note && !fragment && document.richText.math.isEmpty())
          measurements.messageDisplayByIndex.insert(static_cast<int>(index), document.richText.text);
        if (note) {
          measurements.notesByIndex.insert(static_cast<int>(index), size);
          if (document.richText.math.isEmpty())
            measurements.noteDisplayByIndex.insert(static_cast<int>(index), document.richText.text);
        }
        else if (fragment)
          measurements.fragmentsByIndex.insert(static_cast<int>(index), size);
        else measurements.messagesByIndex.insert(static_cast<int>(index), size);
        if (note)
          preparedLabels.notesByIndex.insert(
              static_cast<int>(index), std::move(document));
        else if (fragment)
          preparedLabels.fragmentsByIndex.insert(
              static_cast<int>(index), std::move(document));
        else
          preparedLabels.messagesByIndex.insert(
              static_cast<int>(index), std::move(document));
      }
      sequence::SequenceLayoutOptions layoutOptions;
      layoutOptions.actorMargin = actorMargin;
      layoutOptions.width = actorWidth;
      layoutOptions.height = configNumber(sequenceConfig, QStringLiteral("height"), 65.0);
      layoutOptions.boxMargin = configNumber(sequenceConfig, QStringLiteral("boxMargin"), 10.0);
      layoutOptions.boxTextMargin = configNumber(sequenceConfig, QStringLiteral("boxTextMargin"), 5.0);
      layoutOptions.noteMargin = configNumber(sequenceConfig, QStringLiteral("noteMargin"), 10.0);
      layoutOptions.activationWidth = configNumber(sequenceConfig, QStringLiteral("activationWidth"), 10.0);
      layoutOptions.wrapPadding = wrapPadding;
      layoutOptions.labelBoxWidth = configNumber(sequenceConfig, QStringLiteral("labelBoxWidth"), 50.0);
      layoutOptions.labelBoxHeight = configNumber(sequenceConfig, QStringLiteral("labelBoxHeight"), 20.0);
      layoutOptions.rightAngles = sequenceConfig.value(QStringLiteral("rightAngles")).toBool(false);
      layoutOptions.wrap = globalWrap;
      layoutOptions.mirrorActors = sequenceConfig.value(QStringLiteral("mirrorActors")).toBool(true);
      layoutOptions.hideUnusedParticipants =
          sequenceConfig.value(QStringLiteral("hideUnusedParticipants")).toBool(false);
      layoutOptions.showSequenceNumbers =
          sequenceConfig.value(QStringLiteral("showSequenceNumbers")).toBool(false);
      layoutOptions.forceMenus =
          sequenceConfig.value(QStringLiteral("forceMenus")).toBool(false);
      const sequence::SequenceLayoutResult provisionalLayout =
          sequence::layoutSequence(diagram.data(), measurements, layoutOptions);
      for (qsizetype index = 0; index < diagram.data().messages.size(); ++index) {
        const auto& message = diagram.data().messages.at(index);
        const bool fragment = isSequenceFragment(message.type);
        if (message.type == 2 || fragment || !(message.wrap || globalWrap)) continue;
        const qreal maximumWidth = provisionalLayout.messageWrapWidthsByIndex.value(
            static_cast<int>(index), actorWidth);
        auto document = sequence::wrapSequenceLabel(
            labelDocument(message.message.toString(), sequence::SequenceLabelKind::Message),
            style.fontFamily, style.fontSize, maximumWidth);
        document = prepare(std::move(document));
        measurements.messagesByIndex.insert(static_cast<int>(index), measure(document));
        if (document.richText.math.isEmpty())
          measurements.messageDisplayByIndex.insert(
              static_cast<int>(index), document.richText.text);
        preparedLabels.messagesByIndex.insert(
            static_cast<int>(index), std::move(document));
      }
      const sequence::SequenceLayoutResult layout =
          sequence::layoutSequence(diagram.data(), measurements, layoutOptions);
      for (const auto& fragment : layout.fragments) {
        // The fragment kind tag ("loop"/"alt"/...) uses Box layout (centered
        // baseline) but mermaid draws it with the MESSAGE font weight, so build
        // it directly instead of through the kind-based labelDocument mapping.
        auto kindDocument = sequence::parseSequenceLabel(
            fragment.kind, sequence::SequenceLabelKind::Box);
        kindDocument.richText.baseWeight = style.messageFontWeight;
        preparedLabels.fragmentKindsByIndex.insert(
            fragment.messageIndex, prepare(std::move(kindDocument)));
      }
      sequence::SequenceScene scene = sequence::buildSequenceScene(
          layout, std::move(style), preparedLabels, true);
      sequence::SequenceViewportOptions viewportOptions;
      viewportOptions.diagramMarginX = configNumber(
          sequenceConfig, QStringLiteral("diagramMarginX"), 50.0);
      viewportOptions.diagramMarginY = configNumber(
          sequenceConfig, QStringLiteral("diagramMarginY"), 10.0);
      viewportOptions.boxMargin = layoutOptions.boxMargin;
      viewportOptions.bottomMarginAdj = configNumber(
          sequenceConfig, QStringLiteral("bottomMarginAdj"), 1.0);
      viewportOptions.mirrorActors = layoutOptions.mirrorActors;
      const QRectF viewport = sequence::sequenceViewportRect(scene, viewportOptions);
      scene.viewportRect = viewport;
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(viewport.width()), qCeil(viewport.height()));
      entry.scene = std::make_shared<const sequence::SequenceScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

}  // namespace

const Diagram& sequenceDiagramAdapter() {
  static const SequenceDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
