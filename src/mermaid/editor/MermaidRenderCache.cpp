#include "mermaid/editor/MermaidRenderCache.h"

#include "mermaid/editor/MermaidSvgExporter.h"

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidDiagram.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/editor/MermaidDiagrams.h"
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
#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/journey/JourneyDiagram.h"
#include "mermaid/radar/RadarDiagram.h"
#include "mermaid/xychart/XYChartDiagram.h"
#include "mermaid/timeline/TimelineDiagram.h"
#include "mermaid/packet/PacketDiagram.h"
#include "mermaid/kanban/KanbanDiagram.h"
#include "mermaid/mindmap/MindmapDiagram.h"
#include "mermaid/gantt/GanttDiagram.h"
#include "mermaid/info/InfoDiagram.h"
#include "mermaid/treeview/TreeViewDiagram.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/sankey/SankeyDiagram.h"
#include "mermaid/treemap/TreemapDiagram.h"
#include "mermaid/cynefin/CynefinDiagram.h"
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

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

void enforceUpstreamThemeColorLimit(const QJsonObject& config) {
  const QJsonValue raw = config.value(QStringLiteral("themeVariables"))
                             .toObject()
                             .value(QStringLiteral("THEME_COLOR_LIMIT"));
  if (raw.isUndefined() || raw.isNull()) return;
  const double limit = jsNumberValue(raw);
  if (!(limit > 12.0)) return;

  const flowtheme::FlowThemeId theme = themeIdFromName(themeFromConfig(config));
  // Redux does not index the missing derived cScale channels, so finite values
  // above 12 are accepted. Infinity would make upstream's CSS-generation loop
  // non-terminating; reject it as the native resource-safe equivalent.
  if (theme == flowtheme::FlowThemeId::Redux && std::isfinite(limit)) return;
  if (theme == flowtheme::FlowThemeId::Dark && limit <= 13.0) return;

  const bool failsInInvert =
      theme == flowtheme::FlowThemeId::Neutral ||
      theme == flowtheme::FlowThemeId::ReduxColor ||
      theme == flowtheme::FlowThemeId::ReduxDarkColor ||
      theme == flowtheme::FlowThemeId::Dark;
  throw std::runtime_error(
      failsInInvert
          ? "Cannot read properties of undefined (reading 'r')"
          : "Cannot read properties of undefined (reading 'l')");
}

// Rasterizes any family's scene to a DPR-aware QImage through the uniform
// MermaidScene pointer — no family dispatch. The scene supplies its render
// extent via renderBounds(); painting goes through the virtual paint().
QImage renderMermaidSceneToImage(const std::shared_ptr<const MermaidScene>& scene,
                                 qreal dpr, qreal padding) {
  const QRectF extent =
      scene->renderBounds().adjusted(-padding, -padding, padding, padding);
  const qreal w = std::max<qreal>(1.0, extent.width());
  const qreal h = std::max<qreal>(1.0, extent.height());
  QImage image(qCeil(w * dpr), qCeil(h * dpr), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.scale(dpr, dpr);
  painter.translate(-extent.left(), -extent.top());
  scene->paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}

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
  if (entry.status != MermaidRenderStatus::Ready || !entry.scene)
    return result;
  result.metadata = entry.metadata;
  dpr = qMax<qreal>(0.25, dpr);
  QImage image = renderMermaidSceneToImage(
      entry.scene, dpr, entry.metadata.diagramPadding);
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
        QStringLiteral("classDiagram"), QStringLiteral("stateDiagram-v2"),
        QStringLiteral("erDiagram"), QStringLiteral("requirementDiagram"),
        QStringLiteral("pie"), QStringLiteral("quadrantChart"),
        QStringLiteral("journey"), QStringLiteral("radar-beta")};
    diagnostic.expected.append(QStringLiteral("xychart-beta"));
    diagnostic.expected.append(QStringLiteral("timeline"));
    diagnostic.expected.append(QStringLiteral("packet-beta"));
    diagnostic.expected.append(QStringLiteral("kanban"));
    diagnostic.expected.append(QStringLiteral("mindmap"));
    diagnostic.expected.append(QStringLiteral("gantt"));
    diagnostic.expected.append(QStringLiteral("info"));
    diagnostic.expected.append(QStringLiteral("treeView-beta"));
    diagnostic.expected.append(QStringLiteral("eventmodeling"));
    diagnostic.expected.append(QStringLiteral("ishikawa-beta"));
    diagnostic.expected.append(QStringLiteral("venn-beta"));
    diagnostic.expected.append(QStringLiteral("sankey-beta"));
    diagnostic.expected.append(QStringLiteral("treemap-beta"));
    diagnostic.expected.append(QStringLiteral("cynefin-beta"));
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
    // Mermaid calculates the selected theme before invoking the diagram
    // parser/renderer. Several 11.16 themes index a finite cScale array while
    // doing so and throw for source THEME_COLOR_LIMIT values beyond their
    // actual palette boundary. Preserve that observable failure instead of
    // letting the native model's defensive fixed-array clamp render a diagram
    // that upstream rejects.
    enforceUpstreamThemeColorLimit(pre.config);
    MermaidRenderEntry entry = diagram->render(pre, type, theme);
    // cssClass is a Diagram-contract value (not renderMetadata's job), so write
    // it at the single dispatch site — a new family cannot forget it.
    entry.metadata.cssClass = diagram->cssClass();
    return entry;
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
  } catch (const requirement::RequirementParseError& error) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("parse");
    diagnostic.code = QStringLiteral("requirement-parse-error");
    diagnostic.message = QString::fromUtf8(error.what());
    return errorEntry(std::move(diagnostic));
  } catch (const journey::JourneyParseError& error) {
    const qsizetype offset = error.line > 0
                                 ? offsetForLineColumn(pre.code, error.line, 1)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        QStringLiteral("journey-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.line > 0 ? 1 : 0, QString(), QString(), {}));
  } catch (const radar::RadarParseError& error) {
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        QStringLiteral("radar-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const xychart::XYChartParseError& error) {
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        QStringLiteral("xychart-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const timeline::TimelineParseError& error) {
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        error.kind == timeline::TimelineErrorKind::Runtime
            ? QStringLiteral("timeline-runtime-error")
            : QStringLiteral("timeline-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const packet::PacketParseError& error) {
    QString code;
    switch (error.kind) {
      case packet::PacketErrorKind::Lexer:
        code = QStringLiteral("packet-lexer-error");
        break;
      case packet::PacketErrorKind::Parser:
        code = QStringLiteral("packet-parse-error");
        break;
      case packet::PacketErrorKind::Runtime:
        code = QStringLiteral("packet-runtime-error");
        break;
    }
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const kanban::KanbanParseError& error) {
    QString code;
    switch (error.kind) {
      case kanban::KanbanErrorKind::Lexer:
        code = QStringLiteral("kanban-lexer-error");
        break;
      case kanban::KanbanErrorKind::Parser:
        code = QStringLiteral("kanban-parse-error");
        break;
      case kanban::KanbanErrorKind::Yaml:
        code = QStringLiteral("kanban-yaml-error");
        break;
      case kanban::KanbanErrorKind::Runtime:
        code = QStringLiteral("kanban-runtime-error");
        break;
    }
    // Jison's Kanban lexer reports an unrecognized token at column 0. The
    // shared diagnostic model is 1-based, so retain the known line and map it
    // to that line's first source column instead of dropping the location.
    const int diagnosticColumn =
        error.line > 0 ? std::max(1, error.column) : error.column;
    const qsizetype offset = error.line > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       diagnosticColumn)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, diagnosticColumn, QString(), error.token, {}));
  } catch (const mindmap::MindmapParseError& error) {
    QString code;
    switch (error.kind) {
      case mindmap::MindmapErrorKind::Lexer:
        code = QStringLiteral("mindmap-lexer-error");
        break;
      case mindmap::MindmapErrorKind::Parser:
        code = QStringLiteral("mindmap-parse-error");
        break;
      case mindmap::MindmapErrorKind::Runtime:
        code = QStringLiteral("mindmap-runtime-error");
        break;
    }
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
  } catch (const gantt::GanttParseError& error) {
    QString code;
    switch (error.kind) {
      case gantt::GanttErrorKind::Lexer:
        code = QStringLiteral("gantt-lexer-error");
        break;
      case gantt::GanttErrorKind::Parser:
        code = QStringLiteral("gantt-parse-error");
        break;
      case gantt::GanttErrorKind::Runtime:
        code = QStringLiteral("gantt-runtime-error");
        break;
    }
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const info::InfoParseError& error) {
    const qsizetype offset = error.line > 0 && error.column > 0
                                 ? offsetForLineColumn(pre.code, error.line,
                                                       error.column)
                                 : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        error.kind == info::InfoErrorKind::Lexer
            ? QStringLiteral("info-lexer-error")
            : QStringLiteral("info-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const treeview::TreeViewParseError& error) {
    QString code;
    switch (error.kind) {
      case treeview::TreeViewErrorKind::Lexer:
        code = QStringLiteral("treeview-lexer-error");
        break;
      case treeview::TreeViewErrorKind::Parser:
        code = QStringLiteral("treeview-parse-error");
        break;
      case treeview::TreeViewErrorKind::Preprocess:
        code = QStringLiteral("treeview-preprocess-error");
        break;
    }
    const int diagnosticColumn = error.column > 0 ? error.column : 1;
    const qsizetype offset =
        error.line > 0
            ? offsetForLineColumn(pre.code, error.line, diagnosticColumn)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, diagnosticColumn, QString(), QString(), {}));
  } catch (const eventmodeling::EventModelingParseError& error) {
    const qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        error.kind == eventmodeling::EventModelingErrorKind::Lexer
            ? QStringLiteral("eventmodeling-lexer-error")
            : QStringLiteral("eventmodeling-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), QString(), {}));
  } catch (const ishikawa::IshikawaParseError& error) {
    qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    // Jison's Ishikawa grammar reports its caret after the accumulated
    // SPACELINE token, which can exceed the physical token line. Preserve the
    // parser's exact line/column in IshikawaParseError while anchoring the UI
    // diagnostic to the actual offending token line in decorated source.
    if (offset < 0 && error.line > 0)
      offset = offsetForLineColumn(pre.code, error.line, 1);
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"),
        error.kind == ishikawa::IshikawaErrorKind::Lexer
            ? QStringLiteral("ishikawa-lexer-error")
            : QStringLiteral("ishikawa-parse-error"),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
  } catch (const venn::VennParseError& error) {
    QString code;
    switch (error.kind) {
      case venn::VennErrorKind::Lexer:
        code = QStringLiteral("venn-lexer-error");
        break;
      case venn::VennErrorKind::Parser:
        code = QStringLiteral("venn-parse-error");
        break;
      case venn::VennErrorKind::Runtime:
        code = QStringLiteral("venn-runtime-error");
        break;
    }
    const qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
  } catch (const sankey::SankeyParseError& error) {
    QString code;
    switch (error.kind) {
      case sankey::SankeyErrorKind::Lexer:
        code = QStringLiteral("sankey-lexer-error");
        break;
      case sankey::SankeyErrorKind::Parser:
        code = QStringLiteral("sankey-parse-error");
        break;
      case sankey::SankeyErrorKind::Runtime:
        code = QStringLiteral("sankey-runtime-error");
        break;
    }
    const qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
  } catch (const treemap::TreemapParseError& error) {
    QString code;
    switch (error.kind) {
      case treemap::TreemapErrorKind::Lexer:
        code = QStringLiteral("treemap-lexer-error");
        break;
      case treemap::TreemapErrorKind::Parser:
        code = QStringLiteral("treemap-parse-error");
        break;
      case treemap::TreemapErrorKind::Runtime:
        code = QStringLiteral("treemap-runtime-error");
        break;
    }
    const qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
  } catch (const cynefin::CynefinParseError& error) {
    QString code;
    switch (error.kind) {
      case cynefin::CynefinErrorKind::Lexer:
        code = QStringLiteral("cynefin-lexer-error");
        break;
      case cynefin::CynefinErrorKind::Parser:
        code = QStringLiteral("cynefin-parse-error");
        break;
      case cynefin::CynefinErrorKind::Runtime:
        code = QStringLiteral("cynefin-runtime-error");
        break;
    }
    const qsizetype offset =
        error.line > 0 && error.column > 0
            ? offsetForLineColumn(pre.code, error.line, error.column)
            : -1;
    return errorEntry(parserDiagnostic(
        source, pre, type, QStringLiteral("parse"), std::move(code),
        QString::fromUtf8(error.what()), offset, offset >= 0 ? 1 : 0,
        error.line, error.column, QString(), error.token, {}));
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
