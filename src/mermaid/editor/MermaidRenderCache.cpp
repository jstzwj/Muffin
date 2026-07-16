#include "mermaid/editor/MermaidRenderCache.h"

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/theme/FlowTheme.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtConcurrent>

#include <utility>

namespace muffin::mermaid::editor {
namespace {

flowtheme::FlowThemeId themeIdFromName(const QString& name) {
  static const QHash<QString, flowtheme::FlowThemeId> map = {
      {QStringLiteral("default"), flowtheme::FlowThemeId::Default},
      {QStringLiteral("base"), flowtheme::FlowThemeId::Base},
      {QStringLiteral("dark"), flowtheme::FlowThemeId::Dark},
      {QStringLiteral("forest"), flowtheme::FlowThemeId::Forest},
      {QStringLiteral("neutral"), flowtheme::FlowThemeId::Neutral},
      {QStringLiteral("neo"), flowtheme::FlowThemeId::Neo},
      {QStringLiteral("neo-dark"), flowtheme::FlowThemeId::NeoDark},
      {QStringLiteral("redux"), flowtheme::FlowThemeId::Redux},
      {QStringLiteral("redux-dark"), flowtheme::FlowThemeId::ReduxDark},
      {QStringLiteral("redux-color"), flowtheme::FlowThemeId::ReduxColor},
      {QStringLiteral("redux-dark-color"), flowtheme::FlowThemeId::ReduxDarkColor},
  };
  return map.value(name.isEmpty() ? QStringLiteral("default") : name, flowtheme::FlowThemeId::Default);
}

// Extract the mermaid theme the source declares (%%{init:{theme}}%%), else default.
QString themeFromConfig(const QJsonObject& config) {
  const QJsonValue top = config.value(QStringLiteral("theme"));
  if (top.isString()) return top.toString();
  return QStringLiteral("default");
}

QString configString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
  if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  return {};
}

QHash<QString, QString> themeOverrides(const QJsonObject& config) {
  QHash<QString, QString> result;
  const QJsonObject values = config.value(QStringLiteral("themeVariables")).toObject();
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    const QString value = configString(it.value());
    if (!value.isEmpty()) result.insert(it.key(), value);
  }
  if (config.value(QStringLiteral("fontFamily")).isString())
    result.insert(QStringLiteral("fontFamily"), config.value(QStringLiteral("fontFamily")).toString());
  if (!result.contains(QStringLiteral("fontFamily")))
    result.insert(QStringLiteral("fontFamily"), MermaidFontRegistry::cssFamilyStack());
  return result;
}

qreal pixelValue(const QString& value, qreal fallback) {
  static const QRegularExpression number(QStringLiteral(R"(^\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))px\s*$)"),
                                          QRegularExpression::CaseInsensitiveOption);
  const auto match = number.match(value);
  if (!match.hasMatch()) return fallback;
  bool ok = false;
  const qreal parsed = match.captured(1).toDouble(&ok);
  return ok && parsed > 0.0 ? parsed : fallback;
}

QString firstFontFamily(QString cssFamily) {
  cssFamily = cssFamily.section(QLatin1Char(','), 0, 0).trimmed();
  if (cssFamily.size() >= 2 &&
      ((cssFamily.front() == QLatin1Char('"') && cssFamily.back() == QLatin1Char('"')) ||
       (cssFamily.front() == QLatin1Char('\'') && cssFamily.back() == QLatin1Char('\''))))
    cssFamily = cssFamily.mid(1, cssFamily.size() - 2);
  return cssFamily.isEmpty() ? QStringLiteral("Arial") : cssFamily;
}

qreal configNumber(const QJsonObject& object, const QString& key, qreal fallback) {
  const QJsonValue value = object.value(key);
  return value.isDouble() && value.toDouble() >= 0.0 ? value.toDouble() : fallback;
}

}  // namespace

MermaidRenderCache::MermaidRenderCache(QObject* parent, int capacity) : QObject(parent), capacity_(capacity) {}

MermaidRenderKey MermaidRenderCache::makeKey(const QString& source) {
  QString theme = QStringLiteral("default");
  try {
    theme = themeFromConfig(preprocessDiagram(source).config);
  } catch (...) {
    // Preprocessing failed (e.g. malformed frontmatter) — render the raw source
    // with the default theme; the worker will report the real error.
  }
  MermaidRenderKey key;
  key.sourceHash = QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256);
  key.theme = theme;
  return key;
}

void MermaidRenderCache::touch(const MermaidRenderKey& key) {
  lru_.removeAll(key);
  lru_.append(key);
}

void MermaidRenderCache::evict() {
  while (static_cast<int>(entries_.size()) > capacity_ && !lru_.isEmpty()) {
    const MermaidRenderKey oldest = lru_.takeFirst();
    entries_.remove(oldest);
  }
}

void MermaidRenderCache::commit(const MermaidRenderKey& key, const MermaidRenderEntry& entry) {
  // Only commit if the key is still pending (not evicted while the worker ran).
  auto it = entries_.find(key);
  if (it == entries_.end()) return;
  if (it.value().status != MermaidRenderStatus::Loading) return;  // superseded
  it.value() = entry;
  touch(key);
}

MermaidRenderEntry MermaidRenderCache::request(const MermaidRenderKey& key, const QString& source) {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    touch(key);
    return it.value();
  }
  // Absent — mark Loading and launch a worker.
  MermaidRenderEntry loading;
  loading.status = MermaidRenderStatus::Loading;
  entries_.insert(key, loading);
  touch(key);
  evict();

  auto* watcher = new QFutureWatcher<MermaidRenderEntry>(this);
  watchers_.insert(watcher, key);
  const QString theme = key.theme;
  connect(watcher, &QFutureWatcher<MermaidRenderEntry>::finished, this, [this, watcher, key]() {
    watchers_.remove(watcher);
    const MermaidRenderEntry result = watcher->result();
    watcher->deleteLater();
    commit(key, result);
    emit renderReady(key);
  });
  watcher->setFuture(QtConcurrent::run([source, theme]() { return renderSource(source, theme); }));
  return loading;
}

MermaidRenderEntry MermaidRenderCache::getSync(const MermaidRenderKey& key, const QString& source) {
  auto it = entries_.find(key);
  if (it != entries_.end() && it.value().status != MermaidRenderStatus::Loading) {
    touch(key);
    return it.value();
  }
  MermaidRenderEntry result = renderSource(source, key.theme);
  entries_.insert(key, result);
  touch(key);
  evict();
  return result;
}

void MermaidRenderCache::clear() {
  entries_.clear();
  lru_.clear();
}

QString MermaidRenderCache::renderMermaidSourceToPngDataUrl(const QString& source, qreal dpr) {
  const QString theme = makeKey(source).theme;
  const MermaidRenderEntry entry = renderSource(source, theme);
  if (entry.status != MermaidRenderStatus::Ready || (!entry.scene && !entry.sequenceScene)) return {};
  const QImage image = entry.scene
      ? flowscene::renderFlowSceneToImage(*entry.scene, dpr, 8.0, MermaidFontRegistry::primaryFamily())
      : sequence::renderSequenceSceneToImage(*entry.sequenceScene, dpr, 8.0);
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  image.save(&buffer, "PNG");
  buffer.close();
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

MermaidRenderEntry MermaidRenderCache::renderSource(const QString& source, const QString& theme) {
  MermaidPreprocessResult pre;
  try {
    pre = preprocessDiagram(source);
  } catch (const std::exception& error) {
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Error;
    entry.errorMessage = QString::fromUtf8(error.what());
    return entry;
  }

  const QString type = detectDiagramType(pre.code, pre.config);
  if (type != QLatin1String("flowchart") && type != QLatin1String("flowchart-v2") &&
      type != QLatin1String("sequence")) {
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Unsupported;
    entry.errorMessage = QStringLiteral("Diagram type '%1' is not natively supported").arg(type);
    return entry;
  }

  try {
    if (type == QLatin1String("sequence")) {
      const sequence::SequenceDiagram diagram = sequence::SequenceDiagram::parse(pre.code);
      sequence::SequenceLayoutMeasurements measurements;
      const QJsonObject sequenceConfig = pre.config.value(QStringLiteral("sequence")).toObject();
      const qreal actorMargin = configNumber(sequenceConfig, QStringLiteral("actorMargin"), 50.0);
      const qreal actorWidth = configNumber(sequenceConfig, QStringLiteral("width"), 150.0);
      const qreal wrapPadding = configNumber(sequenceConfig, QStringLiteral("wrapPadding"), 10.0);
      const bool globalWrap = sequenceConfig.value(QStringLiteral("wrap")).toBool(false);
      const auto labelDocument = [&](const QString& text, sequence::SequenceLabelKind kind) {
        return sequence::parseSequenceLabel(text, kind);
      };
      const auto measure = [&](const sequence::SequenceLabelDocument& label) {
        return sequence::layoutSequenceLabel(label, MermaidFontRegistry::cssFamilyStack(),
                                             16.0, 22.0).size;
      };
      for (const auto& actor : diagram.data().actors) {
        auto document = labelDocument(actor.description, sequence::SequenceLabelKind::Participant);
        if (actor.wrap || globalWrap) {
          document = sequence::wrapSequenceLabel(std::move(document),
              MermaidFontRegistry::cssFamilyStack(), 16.0,
              std::max(1.0, actorWidth - 2.0 * wrapPadding));
          measurements.participantDisplayById.insert(actor.id, document.richText.text);
        }
        measurements.participants.insert(actor.id, measure(document));
      }
      for (const auto& box : diagram.data().boxes)
        measurements.boxes.append(measure(labelDocument(box.name, sequence::SequenceLabelKind::Box)));
      for (qsizetype index = 0; index < diagram.data().messages.size(); ++index) {
        const auto& message = diagram.data().messages[index];
        const bool note = message.type == 2;
        const bool fragment = message.type == 10 || message.type == 12 || message.type == 15 ||
                              message.type == 19 || message.type == 22 || message.type == 27 ||
                              message.type == 30 || message.type == 32;
        const auto kind = note ? sequence::SequenceLabelKind::Note
            : fragment ? sequence::SequenceLabelKind::Fragment
                       : sequence::SequenceLabelKind::Message;
        auto document = labelDocument(message.message.toString(), kind);
        if (!fragment && (message.wrap || globalWrap)) {
          qreal maximumWidth = actorWidth + actorMargin;
          if (note) {
            maximumWidth = message.placement == 2 && message.from != message.to
                ? 2.0 * actorWidth - 2.0 * wrapPadding
                : actorWidth - 2.0 * wrapPadding;
          }
          document = sequence::wrapSequenceLabel(std::move(document),
              MermaidFontRegistry::cssFamilyStack(), 16.0, maximumWidth);
        }
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
      layoutOptions.wrap = sequenceConfig.value(QStringLiteral("wrap")).toBool(false);
      layoutOptions.mirrorActors = sequenceConfig.value(QStringLiteral("mirrorActors")).toBool(true);
      layoutOptions.hideUnusedParticipants =
          sequenceConfig.value(QStringLiteral("hideUnusedParticipants")).toBool(false);
      const sequence::SequenceLayoutResult layout =
          sequence::layoutSequence(diagram.data(), measurements, layoutOptions);
      sequence::SequenceSceneStyle style;
      style.fontFamily = MermaidFontRegistry::cssFamilyStack();
      if (themeFromConfig(pre.config).compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
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
      const QHash<QString, QString> sequenceTheme = themeOverrides(pre.config);
      const auto applyTheme = [&](QString& target, const QString& key) {
        if (sequenceTheme.contains(key)) target = sequenceTheme.value(key);
      };
      applyTheme(style.actorFill, QStringLiteral("actorBkg"));
      applyTheme(style.actorStroke, QStringLiteral("actorBorder"));
      applyTheme(style.actorTextColor, QStringLiteral("actorTextColor"));
      applyTheme(style.lifelineColor, QStringLiteral("actorLineColor"));
      applyTheme(style.signalColor, QStringLiteral("signalColor"));
      applyTheme(style.signalTextColor, QStringLiteral("signalTextColor"));
      applyTheme(style.noteFill, QStringLiteral("noteBkgColor"));
      applyTheme(style.noteStroke, QStringLiteral("noteBorderColor"));
      applyTheme(style.noteTextColor, QStringLiteral("noteTextColor"));
      applyTheme(style.activationFill, QStringLiteral("activationBkgColor"));
      applyTheme(style.activationStroke, QStringLiteral("activationBorderColor"));
      applyTheme(style.fragmentFill, QStringLiteral("rectBkgColor"));
      applyTheme(style.fragmentStroke, QStringLiteral("labelBoxBorderColor"));
      applyTheme(style.loopTextColor, QStringLiteral("loopTextColor"));
      applyTheme(style.labelFill, QStringLiteral("labelBoxBkgColor"));
      applyTheme(style.labelStroke, QStringLiteral("labelBoxBorderColor"));
      applyTheme(style.labelTextColor, QStringLiteral("labelTextColor"));
      applyTheme(style.sequenceNumberColor, QStringLiteral("sequenceNumberColor"));
      applyTheme(style.fontFamily, QStringLiteral("fontFamily"));
      if (sequenceTheme.contains(QStringLiteral("fontSize")))
        style.fontSize = pixelValue(sequenceTheme.value(QStringLiteral("fontSize")), style.fontSize);
      sequence::SequenceScene scene = sequence::buildSequenceScene(layout, style);
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
      entry.sequenceScene = std::make_shared<const sequence::SequenceScene>(std::move(scene));
      return entry;
    }
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
      if (!edge.text.isEmpty())
        layoutOptions.measuredEdgeLabels.insert(edge.id, flowchart::measureLabel(edge.text, edge.labelType, textOptions));
    }
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, layoutOptions);
    const quint32 handDrawnSeed = static_cast<quint32>(
        std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(scene.bounds.width(), scene.bounds.height());
    entry.scene = std::make_shared<const flowscene::FlowScene>(std::move(scene));
    return entry;
  } catch (const flowchart::FlowchartParseError& error) {
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Error;
    entry.errorMessage = QString::fromUtf8(error.what());
    return entry;
  } catch (const std::exception& error) {
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Error;
    entry.errorMessage = QString::fromUtf8(error.what());
    return entry;
  }
}

}  // namespace muffin::mermaid::editor
