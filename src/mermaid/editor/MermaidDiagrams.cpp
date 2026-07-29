#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassLayout.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"

#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <algorithm>

namespace muffin::mermaid::editor {
namespace {

bool isSequenceFragment(int type) {
  return type == 10 || type == 12 || type == 15 || type == 19 ||
         type == 22 || type == 27 || type == 30 || type == 32;
}

sequence::SequenceSceneStyle sequenceStyleFromConfig(const QJsonObject& config) {
  sequence::SequenceSceneStyle style;
  style.fontFamily = MermaidFontRegistry::cssFamilyStack();
  if (themeFromConfig(config).compare(
          QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
    style.actorFill = QStringLiteral("#1f2020");
    style.actorStroke = QStringLiteral("#cccccc");
    style.textColor = QStringLiteral("#d3d3d3");
    style.actorTextColor = QStringLiteral("#d3d3d3");
    style.signalColor = QStringLiteral("#d3d3d3");
    style.signalTextColor = QStringLiteral("#d3d3d3");
    style.lifelineColor = QStringLiteral("#cccccc");
    style.noteFill = QStringLiteral("#474949");
    style.noteStroke = QStringLiteral("#2f2f2f");
    style.noteTextColor = QStringLiteral("#ffffff");
    style.activationFill = QStringLiteral("#2f3030");
    style.activationStroke = QStringLiteral("#cccccc");
    style.fragmentStroke = QStringLiteral("#d3d3d3");
    style.loopTextColor = QStringLiteral("#d3d3d3");
    style.labelFill = QStringLiteral("#1f2020");
    style.labelStroke = QStringLiteral("#bdbccc");
    style.labelTextColor = QStringLiteral("#d3d3d3");
    style.sequenceNumberColor = QStringLiteral("#ffffff");
    style.boxStroke = QStringLiteral("rgba(204,204,204,0.5)");
  }
  const QHash<QString, QString> theme = themeOverrides(config);
  const auto apply = [&](QString& target, const QString& key) {
    if (theme.contains(key)) target = theme.value(key);
  };
  apply(style.actorFill, QStringLiteral("actorBkg"));
  apply(style.actorStroke, QStringLiteral("actorBorder"));
  apply(style.actorTextColor, QStringLiteral("actorTextColor"));
  apply(style.lifelineColor, QStringLiteral("actorLineColor"));
  apply(style.signalColor, QStringLiteral("signalColor"));
  apply(style.signalTextColor, QStringLiteral("signalTextColor"));
  apply(style.noteFill, QStringLiteral("noteBkgColor"));
  apply(style.noteStroke, QStringLiteral("noteBorderColor"));
  apply(style.noteTextColor, QStringLiteral("noteTextColor"));
  apply(style.activationFill, QStringLiteral("activationBkgColor"));
  apply(style.activationStroke, QStringLiteral("activationBorderColor"));
  apply(style.fragmentFill, QStringLiteral("rectBkgColor"));
  apply(style.fragmentStroke, QStringLiteral("labelBoxBorderColor"));
  apply(style.loopTextColor, QStringLiteral("loopTextColor"));
  apply(style.labelFill, QStringLiteral("labelBoxBkgColor"));
  apply(style.labelStroke, QStringLiteral("labelBoxBorderColor"));
  apply(style.labelTextColor, QStringLiteral("labelTextColor"));
  apply(style.sequenceNumberColor, QStringLiteral("sequenceNumberColor"));
  apply(style.fontFamily, QStringLiteral("fontFamily"));
  if (theme.contains(QStringLiteral("fontSize")))
    style.fontSize = pixelValue(
        theme.value(QStringLiteral("fontSize")), style.fontSize);
  return style;
}

// stateDiagram behind the Diagram contract. Body is the former renderSource()
// state branch, verbatim.
struct StateDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("state"), QStringLiteral("stateDiagram")};
  }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      const state::StateDiagram diagram = state::StateDiagram::parse(pre.code);
      const QString configuredTheme = themeFromConfig(pre.config);
      const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
          themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
          themeOverrides(pre.config));
      const QString look = pre.config.value(QStringLiteral("look"))
          .toString(QStringLiteral("classic"));
      const QJsonObject stateConfig =
          pre.config.value(QStringLiteral("state")).toObject();
      const state::StateLayoutInput input =
          state::buildStateLayoutInput(diagram.data(), look);
      state::StateSceneStyle style;
      style.stateFill = themeVars.mainBkg;
      style.stateStroke = themeVars.border1;
      style.textColor = themeVars.primaryTextColor;
      style.transitionColor = themeVars.lineColor;
      style.edgeLabelFill = themeVars.mainBkg;
      style.compositeFill = themeVars.clusterBkg;
      style.compositeStroke = themeVars.clusterBorder;
      style.fontFamily = MermaidFontRegistry::cssFamilyStack();
      style.fontSize = pixelValue(themeVars.fontSize, 16.0);
      style.lineHeight = style.fontSize * 1.5;
      style.strokeWidth = themeVars.strokeWidth;
      if (configuredTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        style.noteFill = QStringLiteral("#474949");
        style.noteStroke = QStringLiteral("#2f2f2f");
        style.noteTextColor = QStringLiteral("#ffffff");
      }
      const state::StateLayoutMeasurements measurements = state::measureStateLayoutInput(
          input, style.fontFamily, style.fontSize);
      const state::StatePlacementResult placement =
          state::layoutStateDiagramDagre(
              input, measurements,
              configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0),
              configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0));
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, {}, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(stateConfig, QStringLiteral("titleTopMargin"),
                             25.0), 8.0);
      state::StateScene scene = state::buildStateScene(
          input, placement, std::move(style));
      scene.handDrawn = look.compare(
          QStringLiteral("handDrawn"), Qt::CaseInsensitive) == 0;
      scene.handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
      entry.stateScene = std::make_shared<const state::StateScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

// classDiagram behind the Diagram contract. Body is the former renderSource()
// class branch, verbatim.
struct ClassDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("class"), QStringLiteral("classDiagram")};
  }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      const classdiagram::ClassDiagram diagram =
          classdiagram::ClassDiagram::parse(pre.code);
      const QString configuredTheme = themeFromConfig(pre.config);
      const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
          themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
          themeOverrides(pre.config));
      const QJsonObject classConfig = pre.config.value(QStringLiteral("class")).toObject();
      classdiagram::ClassLayoutOptions options;
      options.padding = configNumber(classConfig, QStringLiteral("padding"), 12.0);
      options.hierarchicalNamespaces =
          classConfig.value(QStringLiteral("hierarchicalNamespaces")).toBool(true);
      options.hideEmptyMembersBox =
          classConfig.value(QStringLiteral("hideEmptyMembersBox")).toBool(false);
      options.htmlLabels = pre.config.value(QStringLiteral("htmlLabels")).toBool(true);
      options.look = pre.config.value(QStringLiteral("look")).toString(QStringLiteral("classic"));
      const classdiagram::ClassLayoutInput input =
          classdiagram::buildClassLayoutInput(diagram.data(), options);
      classdiagram::ClassLabelMeasureOptions measureOptions;
      measureOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
      measureOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
      measureOptions.lineHeight = measureOptions.fontPixelSize * 1.5;
      measureOptions.htmlLabels = options.htmlLabels;
      const classdiagram::ClassLayoutMeasurements labelMeasurements =
          classdiagram::measureClassLayoutLabels(input, measureOptions);
      const QVector<classdiagram::ClassBoxGeometry> boxes =
          classdiagram::layoutClassBoxes(input, labelMeasurements, options);
      const classdiagram::ClassDagreMeasurements dagreMeasurements =
          classdiagram::measureClassDagreInput(input, boxes, measureOptions);
      const classdiagram::ClassPlacementResult placement =
          classdiagram::layoutClassDiagramDagre(input, dagreMeasurements);
      classdiagram::ClassSceneStyle style;
      style.classFill = themeVars.mainBkg;
      style.classStroke = themeVars.border1;
      style.textColor = themeVars.primaryTextColor;
      style.lineColor = themeVars.lineColor;
      style.edgeLabelFill = themeVars.mainBkg;
      style.clusterFill = themeVars.secondaryColor;
      style.clusterStroke = themeVars.border2;
      style.titleColor = themeVars.titleColor;
      style.strokeWidth = themeVars.strokeWidth;
      if (configuredTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        style.noteFill = QStringLiteral("#474949");
        style.noteStroke = QStringLiteral("#2f2f2f");
        style.noteTextColor = color::invert(themeVars.secondaryColor);
      }
      style.fontFamily = measureOptions.fontFamily;
      style.fontSize = measureOptions.fontPixelSize;
      style.lineHeight = measureOptions.lineHeight;
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, diagram.data().title, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(classConfig, QStringLiteral("titleTopMargin"),
                             25.0), 8.0);
      classdiagram::ClassScene scene = classdiagram::buildClassScene(
          input, boxes, labelMeasurements, placement, std::move(style));
      scene.handDrawn = options.look.compare(
          QStringLiteral("handDrawn"), Qt::CaseInsensitive) == 0;
      scene.handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
      entry.classScene = std::make_shared<const classdiagram::ClassScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

// sequenceDiagram behind the Diagram contract. Body is the former
// renderSource() sequence branch, verbatim.
struct SequenceDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("sequence")}; }
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
        return sequence::parseSequenceLabel(text, kind);
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
        preparedLabels.fragmentKindsByIndex.insert(
            fragment.messageIndex,
            prepare(labelDocument(fragment.kind, sequence::SequenceLabelKind::Box)));
      }
      sequence::SequenceScene scene = sequence::buildSequenceScene(
          layout, std::move(style), preparedLabels, true);
      scene.handDrawn = pre.config.value(QStringLiteral("look"))
          .toString().compare(QStringLiteral("handDrawn"), Qt::CaseInsensitive) == 0;
      scene.handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
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
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(viewport.width()), qCeil(viewport.height()));
      entry.sequenceViewport = viewportOptions;
      entry.sequenceScene = std::make_shared<const sequence::SequenceScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

// flowchart behind the Diagram contract. Body is the former renderSource()
// flowchart fallthrough, verbatim.
struct FlowchartDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("flowchart"), QStringLiteral("flowchart-v2")};
  }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme), themeOverrides(pre.config));
    const QJsonObject flowConfig = pre.config.value(QStringLiteral("flowchart")).toObject();
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        pre.config.value(QStringLiteral("look")).toString());
    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
    textOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    const qreal padding = configNumber(flowConfig, QStringLiteral("padding"), 15.0);
    const qreal diagramPadding = configNumber(
        flowConfig, QStringLiteral("diagramPadding"), 8.0);
    textOptions.horizontalPadding = padding * 2.0;
    textOptions.verticalPadding = padding;
    textOptions.look = look;
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data(), textOptions);
    flowchart::FlowLayoutOptions layoutOptions;
    layoutOptions.look = look;
    layoutOptions.nodePadding = padding;
    layoutOptions.nodeSpacing = configNumber(flowConfig, QStringLiteral("nodeSpacing"), 50.0);
    layoutOptions.rankSpacing = configNumber(flowConfig, QStringLiteral("rankSpacing"), 50.0);
    const QString curve = flowConfig.value(QStringLiteral("curve")).toString();
    if (!curve.isEmpty()) layoutOptions.curve = curve;
    for (const flowchart::FlowEdge& edge : chart.data().edges) {
      if (!edge.text.isEmpty()) {
        const flowchart::FlowEdgeLabelLayout prepared =
            flowchart::layoutFlowchartEdgeLabel(edge, textOptions);
        layoutOptions.measuredEdgeLabels.insert(edge.id, prepared.size);
        layoutOptions.preparedEdgeLabels.insert(edge.id, prepared);
      }
    }
    for (const flowchart::FlowSubgraph& subgraph : chart.data().subgraphs)
      if (!subgraph.title.isEmpty())
        layoutOptions.measuredClusterLabels.insert(
            subgraph.id,
            flowchart::measureFlowchartClusterLabel(subgraph, textOptions));
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, layoutOptions);
    const quint32 handDrawnSeed = static_cast<quint32>(
        std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, chart.data().title, chart.data().accTitle,
        chart.data().accDescription, themeVars.textColor,
        textOptions.fontFamily, 18.0,
        configNumber(flowConfig, QStringLiteral("titleTopMargin"), 25.0),
        diagramPadding);
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const flowscene::FlowScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

// erDiagram behind the Diagram contract. Body is the former renderSource()
// er branch, verbatim.
struct ErDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("er")}; }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const er::ErDiagram diagram = er::ErDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    const er::ErLayoutInput input = er::buildErLayoutInput(diagram.data());
    const QString fontFamily = firstFontFamily(themeVars.fontFamily);
    const qreal fontSize = pixelValue(themeVars.fontSize, 16.0);
    const QJsonObject erConfig = pre.config.value(QStringLiteral("er")).toObject();
    const er::ErLayoutMeasurements measurements = er::measureErLayoutInput(
        input, fontFamily, fontSize,
        configNumber(erConfig, QStringLiteral("minEntityWidth"), 100.0),
        configNumber(erConfig, QStringLiteral("minEntityHeight"), 75.0));
    const er::ErPlacementResult placement = er::layoutErDiagramDagre(
        input, measurements,
        configNumber(erConfig, QStringLiteral("nodeSpacing"), 140.0),
        configNumber(erConfig, QStringLiteral("rankSpacing"), 80.0));
    er::ErSceneStyle style;
    style.entityFill = themeVars.mainBkg;
    style.entityStroke = themeVars.border1;
    style.entityTitle1 = themeVars.primaryTextColor;
    style.attributeColor = themeVars.primaryTextColor;
    style.relationshipColor = themeVars.lineColor;
    style.relationshipLabelColor = themeVars.textColor;
    style.labelBackground = themeVars.mainBkg;
    style.strokeWidth = themeVars.strokeWidth;
    style.fontFamily = fontFamily;
    style.fontSize = fontSize;
    style.lineHeight = fontSize * 1.5;
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, diagram.data().title, diagram.data().accTitle,
        diagram.data().accDescription, style.entityTitle1, style.fontFamily,
        18.0, configNumber(erConfig, QStringLiteral("titleTopMargin"), 25.0), 8.0);
    er::ErScene scene = er::buildErScene(input, placement, std::move(style));
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.erScene = std::make_shared<const er::ErScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram* findMermaidDiagram(const QString& type) {
  static const StateDiagramImpl stateImpl;
  static const ClassDiagramImpl classImpl;
  static const SequenceDiagramImpl sequenceImpl;
  static const FlowchartDiagramImpl flowchartImpl;
  static const ErDiagramImpl erImpl;
  static const QVector<const Diagram*> kAll = {
      &stateImpl, &classImpl, &sequenceImpl, &flowchartImpl, &erImpl};
  static const QHash<QString, const Diagram*> kByType = [] {
    QHash<QString, const Diagram*> registry;
    for (const Diagram* diagram : kAll)
      for (const QString& id : diagram->ids()) registry.insert(id, diagram);
    return registry;
  }();
  return kByType.value(type, nullptr);
}

}  // namespace muffin::mermaid::editor
