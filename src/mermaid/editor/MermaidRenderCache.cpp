#include "mermaid/editor/MermaidRenderCache.h"

#include "mermaid/editor/MermaidSvgExporter.h"

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidDiagram.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassLayout.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/classdiagram/ClassScenePainter.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/state/StateScenePainter.h"
#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/erdiagram/ErScenePainter.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QtConcurrent>

#include <optional>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QPair<int, int> lineColumnForOffset(const QString& source, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, source.size());
  int line = 1;
  int column = 1;
  for (qsizetype index = 0; index < offset; ++index) {
    const QChar ch = source.at(index);
    if (ch == QLatin1Char('\r')) {
      if (index + 1 < offset && source.at(index + 1) == QLatin1Char('\n')) ++index;
      ++line;
      column = 1;
    } else if (ch == QLatin1Char('\n')) {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return {line, column};
}

qsizetype offsetForLineColumn(const QString& source, int targetLine, int targetColumn) {
  if (targetLine < 1 || targetColumn < 1) return -1;
  int line = 1;
  int column = 1;
  for (qsizetype index = 0; index <= source.size(); ++index) {
    if (line == targetLine && column == targetColumn) return index;
    if (index == source.size()) break;
    const QChar ch = source.at(index);
    if (ch == QLatin1Char('\r')) {
      if (index + 1 < source.size() && source.at(index + 1) == QLatin1Char('\n')) ++index;
      ++line;
      column = 1;
    } else if (ch == QLatin1Char('\n')) {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return -1;
}

qsizetype originalOffsetForCodeOffset(
    const MermaidPreprocessResult& pre, qsizetype codeOffset,
    qsizetype sourceSize) {
  if (codeOffset < 0) return -1;
  if (codeOffset < pre.codeSourceOffsets.size()) {
    return qBound<qsizetype>(0, pre.codeSourceOffsets.at(codeOffset), sourceSize);
  }
  if (codeOffset >= pre.code.size()) {
    return qBound<qsizetype>(0, pre.codeSourceEndOffset, sourceSize);
  }
  return qBound<qsizetype>(0, codeOffset, sourceSize);
}

MermaidSourceSpan mappedSourceSpan(
    const QString& source, const MermaidPreprocessResult& pre,
    qsizetype codeOffset, qsizetype codeLength, int fallbackLine,
    int fallbackColumn) {
  MermaidSourceSpan span;
  span.offset = originalOffsetForCodeOffset(pre, codeOffset, source.size());
  if (span.offset < 0 && fallbackLine > 0 && fallbackColumn > 0) {
    span.offset = offsetForLineColumn(source, fallbackLine, fallbackColumn);
  }
  if (span.offset < 0) return span;

  if (codeLength > 0 && codeOffset >= 0 && !pre.code.isEmpty()) {
    const qsizetype lastCodeOffset = qBound<qsizetype>(
        0, codeOffset + codeLength - 1, pre.code.size() - 1);
    const qsizetype lastSourceOffset = originalOffsetForCodeOffset(
        pre, lastCodeOffset, source.size());
    if (lastSourceOffset >= span.offset) {
      span.length = qMax<qsizetype>(1, lastSourceOffset - span.offset + 1);
    }
  }
  const auto [line, column] = lineColumnForOffset(source, span.offset);
  span.line = line;
  span.column = column;
  return span;
}

MermaidDiagnostic parserDiagnostic(
    const QString& source, const MermaidPreprocessResult& pre,
    QString diagramType, QString stage, QString code, QString message,
    qsizetype offset, qsizetype length, int line, int column,
    QString production, QString actual, QStringList expected) {
  MermaidDiagnostic diagnostic;
  diagnostic.diagramType = std::move(diagramType);
  diagnostic.stage = std::move(stage);
  diagnostic.code = std::move(code);
  diagnostic.span = mappedSourceSpan(
      source, pre, offset, length, line, column);
  diagnostic.message = std::move(message);
  diagnostic.production = std::move(production);
  diagnostic.actual = std::move(actual);
  diagnostic.expected = std::move(expected);
  return diagnostic;
}

MermaidRenderEntry errorEntry(
    MermaidDiagnostic diagnostic,
    MermaidRenderStatus status = MermaidRenderStatus::Error) {
  MermaidRenderEntry entry;
  entry.status = status;
  entry.diagnostic = std::move(diagnostic);
  entry.errorMessage = formatMermaidDiagnostic(entry.diagnostic);
  entry.errorDiagnostic = mermaidDiagnosticToJson(entry.diagnostic);
  return entry;
}

MermaidDiagnostic preprocessingDiagnostic(
    const QString& source, const QString& message) {
  MermaidDiagnostic diagnostic;
  diagnostic.stage = QStringLiteral("preprocess");
  diagnostic.code = QStringLiteral("invalid-configuration");
  diagnostic.message = message;
  static const QRegularExpression yamlPosition(
      QStringLiteral(R"(line\s+(\d+)\s*,\s*column\s+(\d+))"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = yamlPosition.match(message);
  if (match.hasMatch()) {
    // yaml-cpp reports a 1-based position inside the YAML body. Mermaid front
    // matter starts after the opening --- line, hence the extra source line.
    const int line = match.captured(1).toInt() + 1;
    const int column = match.captured(2).toInt();
    const qsizetype offset = offsetForLineColumn(source, line, column);
    if (offset >= 0) {
      diagnostic.span.offset = offset;
      diagnostic.span.length = offset < source.size() ? 1 : 0;
      diagnostic.span.line = line;
      diagnostic.span.column = column;
    }
  }
  return diagnostic;
}

std::optional<MermaidRenderEntry> unsupportedLayoutConfiguration(
    const MermaidPreprocessResult& pre, const QString& type) {
  QString section;
  if (type.startsWith(QLatin1String("flowchart")))
    section = QStringLiteral("flowchart");
  else if (type == QLatin1String("class") ||
           type == QLatin1String("classDiagram"))
    section = QStringLiteral("class");
  else if (type == QLatin1String("state") ||
           type == QLatin1String("stateDiagram"))
    section = QStringLiteral("state");
  else
    return std::nullopt;

  QString path = QStringLiteral("layout");
  QString actual = pre.config.value(path).toString();
  QString expected = QStringLiteral("dagre");
  if (actual.isEmpty() || actual == expected) {
    path = section + QStringLiteral(".defaultRenderer");
    actual = pre.config.value(section).toObject()
                 .value(QStringLiteral("defaultRenderer"))
                 .toString();
    expected = QStringLiteral("dagre-wrapper");
  }
  if (actual.isEmpty() || actual == expected) return std::nullopt;

  MermaidDiagnostic diagnostic;
  diagnostic.diagramType = type;
  diagnostic.stage = QStringLiteral("configuration");
  diagnostic.code = QStringLiteral("unsupported-layout-engine");
  diagnostic.message = QStringLiteral(
      "Native Mermaid rendering does not support %1='%2'.")
                           .arg(path, actual);
  diagnostic.production = path;
  diagnostic.actual = actual;
  diagnostic.expected = {expected};
  return errorEntry(std::move(diagnostic),
                    MermaidRenderStatus::Unsupported);
}

bool isSequenceFragment(int type) {
  return type == 10 || type == 12 || type == 15 || type == 19 ||
         type == 22 || type == 27 || type == 30 || type == 32;
}

sequence::SequenceSceneStyle sequenceStyleFromConfig(
    const QJsonObject& config) {
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

}  // namespace

MermaidRenderCache::MermaidRenderCache(QObject* parent, int capacity)
    : QObject(parent), capacity_(capacity) {
  debounceTimer_.setSingleShot(true);
  connect(&debounceTimer_, &QTimer::timeout, this, [this]() {
    if (!debouncePending_) return;
    const MermaidRenderKey key = debouncedKey_;
    const QString source = debouncedSource_;
    debouncePending_ = false;
    debouncedSource_.clear();
    const auto it = entries_.constFind(key);
    if (it == entries_.cend() || it.value().status != MermaidRenderStatus::Loading)
      return;
    launchWorker(key, source);
  });
}

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

void MermaidRenderCache::launchWorker(
    const MermaidRenderKey& key, const QString& source) {
  auto* watcher = new QFutureWatcher<MermaidRenderEntry>(this);
  watchers_.insert(watcher, key);
  const QString theme = key.theme;
  connect(watcher, &QFutureWatcher<MermaidRenderEntry>::finished, this,
          [this, watcher, key]() {
    watchers_.remove(watcher);
    const MermaidRenderEntry result = watcher->result();
    watcher->deleteLater();
    commit(key, result);
    emit renderReady(key);
  });
  watcher->setFuture(QtConcurrent::run(
      [source, theme]() { return renderSource(source, theme); }));
}

void MermaidRenderCache::cancelDebouncedRequest(bool removeLoadingEntry) {
  if (!debouncePending_) return;
  debounceTimer_.stop();
  if (removeLoadingEntry) {
    const auto it = entries_.constFind(debouncedKey_);
    if (it != entries_.cend() &&
        it.value().status == MermaidRenderStatus::Loading) {
      entries_.remove(debouncedKey_);
      lru_.removeAll(debouncedKey_);
    }
  }
  debouncePending_ = false;
  debouncedSource_.clear();
}

MermaidRenderEntry MermaidRenderCache::request(
    const MermaidRenderKey& key, const QString& source) {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    if (debouncePending_ && debouncedKey_ == key) {
      cancelDebouncedRequest(false);
      launchWorker(key, source);
    }
    touch(key);
    return it.value();
  }
  // Absent — mark Loading and launch a worker.
  MermaidRenderEntry loading;
  loading.status = MermaidRenderStatus::Loading;
  entries_.insert(key, loading);
  touch(key);
  evict();
  launchWorker(key, source);
  return loading;
}

MermaidRenderEntry MermaidRenderCache::requestDebounced(
    const MermaidRenderKey& key, const QString& source, int delayMs) {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    touch(key);
    return it.value();
  }

  cancelDebouncedRequest(true);
  MermaidRenderEntry loading;
  loading.status = MermaidRenderStatus::Loading;
  entries_.insert(key, loading);
  touch(key);
  evict();

  debouncedKey_ = key;
  debouncedSource_ = source;
  debouncePending_ = true;
  debounceTimer_.start(qMax(0, delayMs));
  return loading;
}

MermaidRenderEntry MermaidRenderCache::getSync(const MermaidRenderKey& key, const QString& source) {
  auto it = entries_.find(key);
  if (it != entries_.end() && it.value().status != MermaidRenderStatus::Loading) {
    touch(key);
    return it.value();
  }
  if (debouncePending_ && debouncedKey_ == key)
    cancelDebouncedRequest(false);
  MermaidRenderEntry result = renderSource(source, key.theme);
  entries_.insert(key, result);
  touch(key);
  evict();
  return result;
}

void MermaidRenderCache::clear() {
  cancelDebouncedRequest(false);
  entries_.clear();
  lru_.clear();
}

MermaidPngRenderResult MermaidRenderCache::renderMermaidSourceToPng(
    const QString& source, qreal dpr) {
  MermaidPngRenderResult result;
  const QString theme = makeKey(source).theme;
  const MermaidRenderEntry entry = renderSource(source, theme);
  if (entry.status != MermaidRenderStatus::Ready ||
      (!entry.scene && !entry.sequenceScene && !entry.classScene &&
       !entry.stateScene && !entry.erScene)) return result;
  result.metadata = entry.metadata;
  dpr = qMax<qreal>(0.25, dpr);
  QImage image;
  if (entry.scene)
    image = flowscene::renderFlowSceneToImage(
        *entry.scene, dpr, entry.metadata.diagramPadding,
        MermaidFontRegistry::primaryFamily());
  else if (entry.sequenceScene)
    image = sequence::renderSequenceSceneToImage(
        *entry.sequenceScene, dpr, entry.sequenceViewport);
  else if (entry.classScene)
    image = classdiagram::renderClassSceneToImage(
        *entry.classScene, dpr, entry.metadata.diagramPadding);
  else if (entry.erScene)
    image = er::renderErSceneToImage(
        *entry.erScene, dpr, entry.metadata.diagramPadding);
  else image = state::renderStateSceneToImage(
      *entry.stateScene, dpr, entry.metadata.diagramPadding);
  if (entry.metadata.hasVisibleTitle()) {
    const qreal contentWidth = image.width() / dpr;
    const qreal contentHeight = image.height() / dpr;
    const qreal canvasWidth = qMax<qreal>(
        contentWidth, entry.naturalSize.width());
    const qreal canvasHeight = entry.metadata.titleHeight + contentHeight;
    QImage titled(qCeil(canvasWidth * dpr), qCeil(canvasHeight * dpr),
                  QImage::Format_ARGB32_Premultiplied);
    titled.fill(Qt::transparent);
    QPainter painter(&titled);
    const QPoint contentTopLeft(
        qRound((canvasWidth - contentWidth) * dpr / 2.0),
        qRound(entry.metadata.titleHeight * dpr));
    painter.drawImage(contentTopLeft, image);
    painter.scale(dpr, dpr);
    paintMermaidTitle(entry.metadata, painter,
                      QRectF(0.0, 0.0, canvasWidth,
                             entry.metadata.titleHeight));
    painter.end();
    image = std::move(titled);
  }
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly)) return result;
  if (!image.save(&buffer, "PNG")) return result;
  buffer.close();
  result.dataUrl = QStringLiteral("data:image/png;base64,") +
                   QString::fromLatin1(png.toBase64());
  return result;
}

QString MermaidRenderCache::renderMermaidSourceToPngDataUrl(
    const QString& source, qreal dpr) {
  return renderMermaidSourceToPng(source, dpr).dataUrl;
}

MermaidSvgRenderResult MermaidRenderCache::renderMermaidSourceToSvg(
    const QString& source, qsizetype instanceIndex) {
  MermaidSvgRenderResult result;
  const MermaidRenderKey key = makeKey(source);
  const MermaidRenderEntry entry = renderSource(source, key.theme);
  result.svg = renderMermaidEntryToSvg(entry, instanceIndex);
  if (result.svg.isEmpty()) return result;
  result.metadata = entry.metadata;
  result.dataUrl = QStringLiteral("data:image/svg+xml;base64,") +
                   QString::fromLatin1(result.svg.toBase64());
  return result;
}

QString MermaidRenderCache::renderMermaidSourceToSvgDataUrl(
    const QString& source, qsizetype instanceIndex) {
  return renderMermaidSourceToSvg(source, instanceIndex).dataUrl;
}

namespace {

// stateDiagram behind the Diagram contract (step 3). Body is the former
// renderSource() state branch, verbatim.
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

// classDiagram behind the Diagram contract (step 3). Body is the former
// renderSource() class branch, verbatim.
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

// sequenceDiagram behind the Diagram contract (step 3). Body is the former
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

// flowchart behind the Diagram contract (step 3). Body is the former
// renderSource() flowchart fallthrough, verbatim.
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

// erDiagram behind the Diagram contract (step 3 incremental migration). The
// render() body is the former renderSource() er branch, verbatim; the central
// catch in renderSource still handles its parse errors.
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
    const er::ErLayoutMeasurements measurements =
        er::measureErLayoutInput(input, fontFamily, fontSize);
    const er::ErPlacementResult placement =
        er::layoutErDiagramDagre(input, measurements);
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
    const QJsonObject erConfig = pre.config.value(QStringLiteral("er")).toObject();
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

// Registry of natively-rendered diagrams, keyed by detected type id. As each
// family migrates onto the Diagram contract it is added to kAll and its
// renderSource() branch removed, until the if/else dispatch is gone.
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

}  // namespace

MermaidRenderEntry MermaidRenderCache::renderSource(const QString& source, const QString& theme) {
  MermaidPreprocessResult pre;
  try {
    pre = preprocessDiagram(source);
  } catch (const std::exception& error) {
    return errorEntry(preprocessingDiagnostic(
        source, QString::fromUtf8(error.what())));
  }

  QString type;
  try {
    type = detectDiagramType(pre.code, pre.config);
  } catch (const UnknownDiagramError&) {
    MermaidDiagnostic diagnostic;
    diagnostic.stage = QStringLiteral("detector");
    diagnostic.code = QStringLiteral("missing-diagram-header");
    diagnostic.message = QStringLiteral(
        "No supported Mermaid diagram header was found.");
    diagnostic.expected = {
        QStringLiteral("flowchart"), QStringLiteral("sequenceDiagram"),
        QStringLiteral("classDiagram"), QStringLiteral("stateDiagram-v2")};
    diagnostic.span = mappedSourceSpan(
        source, pre, 0, pre.code.isEmpty() ? 0 : 1, 1, 1);
    return errorEntry(std::move(diagnostic));
  }
  if (const auto unsupported = unsupportedLayoutConfiguration(pre, type))
    return *unsupported;
  const Diagram* diagram = findMermaidDiagram(type);
  if (!diagram) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("unsupported");
    diagnostic.code = QStringLiteral("unsupported-diagram");
    diagnostic.message = QStringLiteral(
        "Diagram type '%1' is not natively supported.").arg(type);
    return errorEntry(std::move(diagnostic), MermaidRenderStatus::Unsupported);
  }

  try {
    return diagram->render(pre, type, theme);
  } catch (const math::MathMlPaintError& error) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("render");
    diagnostic.code = QStringLiteral("sequence-label-mathml");
    diagnostic.message = QString::fromUtf8(error.what());
    MermaidRenderEntry entry = errorEntry(std::move(diagnostic));
    entry.errorDiagnostic.insert(QStringLiteral("renderFailure"),
                                 error.failure().toJson());
    entry.errorDiagnostic.insert(QStringLiteral("component"),
                                 QStringLiteral("sequence-label-mathml"));
    return entry;
  } catch (const flowchart::FlowchartParseError& error) {
    const flowchart::FlowchartDiagnostic& value = error.diagnostic();
    return errorEntry(parserDiagnostic(
        source, pre, type,
        flowchart::flowchartErrorStageName(value.stage),
        flowchart::flowchartErrorCodeName(value.code),
        QString::fromUtf8(error.what()), value.span.offset,
        value.span.length, value.span.line, value.span.column,
        value.production, value.actual, value.expected));
  } catch (const sequence::SequenceParseError& error) {
    const sequence::SequenceDiagnostic& value = error.diagnostic();
    return errorEntry(parserDiagnostic(
        source, pre, type,
        sequence::sequenceErrorStageName(value.stage),
        sequence::sequenceErrorCodeName(value.code), value.detail,
        value.span.offset, value.span.length, value.span.line,
        value.span.column + 1, value.production, value.actual,
        value.expected));
  } catch (const classdiagram::ClassParseError& error) {
    const classdiagram::ClassDiagnostic& value = error.diagnostic();
    const qsizetype length = value.span.length > 0
        ? value.span.length
        : qMax<qsizetype>(1, value.actual.size());
    return errorEntry(parserDiagnostic(
        source, pre, type,
        classdiagram::classErrorStageName(value.stage),
        classdiagram::classErrorCodeName(value.code), value.detail,
        value.span.offset, length, value.span.line,
        value.span.column + 1, value.production, value.actual,
        value.expected));
  } catch (const state::StateParseError& error) {
    const state::StateDiagnostic& value = error.diagnostic();
    const qsizetype length = value.span.length > 0
        ? value.span.length
        : qMax<qsizetype>(1, value.actual.size());
    return errorEntry(parserDiagnostic(
        source, pre, type,
        state::stateErrorStageName(value.stage),
        state::stateErrorCodeName(value.code), value.detail,
        value.span.offset, length, value.span.line,
        value.span.column + 1, value.production, value.actual,
        value.expected));
  } catch (const std::exception& error) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("render");
    diagnostic.code = QStringLiteral("native-render-failed");
    diagnostic.message = QString::fromUtf8(error.what());
    return errorEntry(std::move(diagnostic));
  }
}

}  // namespace muffin::mermaid::editor
