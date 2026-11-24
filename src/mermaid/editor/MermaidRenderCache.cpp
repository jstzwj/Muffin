#include "mermaid/editor/MermaidRenderCache.h"
#include "render/LayoutResources.h"
#include <QUuid>
#include <QDataStream>

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
#include "mermaid/gitgraph/GitGraphDiagram.h"
#include "mermaid/gantt/GanttDiagram.h"
#include "mermaid/info/InfoDiagram.h"
#include "mermaid/treeview/TreeViewDiagram.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/sankey/SankeyDiagram.h"
#include "mermaid/treemap/TreemapDiagram.h"
#include "mermaid/cynefin/CynefinDiagram.h"
#include "mermaid/wardley/WardleyDiagram.h"
#include "mermaid/architecture/ArchitectureDiagram.h"
#include "mermaid/block/BlockDiagram.h"
#include "mermaid/c4/C4Diagram.h"
#include "mermaid/railroad/RailroadDiagram.h"
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
  const QJsonValue raw =
      config.value(QStringLiteral("themeVariables")).toObject().value(QStringLiteral("THEME_COLOR_LIMIT"));
  if (raw.isUndefined() || raw.isNull())
    return;
  const double limit = jsNumberValue(raw);
  if (!(limit > 12.0))
    return;

  const flowtheme::FlowThemeId theme = themeIdFromName(themeFromConfig(config));
  // Redux does not index the missing derived cScale channels, so finite values
  // above 12 are accepted. Infinity would make upstream's CSS-generation loop
  // non-terminating; reject it as the native resource-safe equivalent.
  if (theme == flowtheme::FlowThemeId::Redux && std::isfinite(limit))
    return;
  if (theme == flowtheme::FlowThemeId::Dark && limit <= 13.0)
    return;

  const bool failsInInvert = theme == flowtheme::FlowThemeId::Neutral || theme == flowtheme::FlowThemeId::ReduxColor ||
                             theme == flowtheme::FlowThemeId::ReduxDarkColor || theme == flowtheme::FlowThemeId::Dark;
  throw std::runtime_error(failsInInvert ? "Cannot read properties of undefined (reading 'r')"
                                         : "Cannot read properties of undefined (reading 'l')");
}

// Rasterizes any family's scene to a DPR-aware QImage through the uniform
// MermaidScene pointer — no family dispatch. The scene supplies its render
// extent via renderBounds(); painting goes through the virtual paint().
QImage renderMermaidSceneToImage(const std::shared_ptr<const MermaidScene>& scene, qreal dpr, qreal padding) {
  const QRectF extent = scene->renderBounds().adjusted(-padding, -padding, padding, padding);
  const qreal w = std::max<qreal>(1.0, extent.width());
  const qreal h = std::max<qreal>(1.0, extent.height());
  const int pixelWidth = scene->roundRasterExtentToNearestPixel() ? qMax(1, qRound(w * dpr)) : qCeil(w * dpr);
  const int pixelHeight = scene->roundRasterExtentToNearestPixel() ? qMax(1, qRound(h * dpr)) : qCeil(h * dpr);
  QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
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
      if (index + 1 < offset && source.at(index + 1) == QLatin1Char('\n'))
        ++index;
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
  if (targetLine < 1 || targetColumn < 1)
    return -1;
  int line = 1;
  int column = 1;
  for (qsizetype index = 0; index <= source.size(); ++index) {
    if (line == targetLine && column == targetColumn)
      return index;
    if (index == source.size())
      break;
    const QChar ch = source.at(index);
    if (ch == QLatin1Char('\r')) {
      if (index + 1 < source.size() && source.at(index + 1) == QLatin1Char('\n'))
        ++index;
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

qsizetype originalOffsetForCodeOffset(const MermaidPreprocessResult& pre, qsizetype codeOffset, qsizetype sourceSize) {
  if (codeOffset < 0)
    return -1;
  if (codeOffset < pre.codeSourceOffsets.size()) {
    return qBound<qsizetype>(0, pre.codeSourceOffsets.at(codeOffset), sourceSize);
  }
  if (codeOffset >= pre.code.size()) {
    return qBound<qsizetype>(0, pre.codeSourceEndOffset, sourceSize);
  }
  return qBound<qsizetype>(0, codeOffset, sourceSize);
}

MermaidSourceSpan mappedSourceSpan(const QString& source, const MermaidPreprocessResult& pre, qsizetype codeOffset,
                                   qsizetype codeLength, int fallbackLine, int fallbackColumn) {
  MermaidSourceSpan span;
  span.offset = originalOffsetForCodeOffset(pre, codeOffset, source.size());
  if (span.offset < 0 && fallbackLine > 0 && fallbackColumn > 0) {
    span.offset = offsetForLineColumn(source, fallbackLine, fallbackColumn);
  }
  if (span.offset < 0)
    return span;

  if (codeLength > 0 && codeOffset >= 0 && !pre.code.isEmpty()) {
    const qsizetype lastCodeOffset = qBound<qsizetype>(0, codeOffset + codeLength - 1, pre.code.size() - 1);
    const qsizetype lastSourceOffset = originalOffsetForCodeOffset(pre, lastCodeOffset, source.size());
    if (lastSourceOffset >= span.offset) {
      span.length = qMax<qsizetype>(1, lastSourceOffset - span.offset + 1);
    }
  }
  const auto [line, column] = lineColumnForOffset(source, span.offset);
  span.line = line;
  span.column = column;
  return span;
}

MermaidDiagnostic parserDiagnostic(const QString& source, const MermaidPreprocessResult& pre, QString diagramType,
                                   QString stage, QString code, QString message, qsizetype offset, qsizetype length,
                                   int line, int column, QString production, QString actual, QStringList expected) {
  MermaidDiagnostic diagnostic;
  diagnostic.diagramType = std::move(diagramType);
  diagnostic.stage = std::move(stage);
  diagnostic.code = std::move(code);
  diagnostic.span = mappedSourceSpan(source, pre, offset, length, line, column);
  diagnostic.message = std::move(message);
  diagnostic.production = std::move(production);
  diagnostic.actual = std::move(actual);
  diagnostic.expected = std::move(expected);
  return diagnostic;
}

// A diagram's own error report as a diagnostic. The position is resolved in the code the
// diagram's parser saw and then mapped back into the source the user sees.
MermaidDiagnostic parserDiagnostic(const QString& source, const MermaidPreprocessResult& pre, QString diagramType,
                                   const DiagramErrorInfo& report) {
  using Place = DiagramErrorInfo::Place;
  const bool located = report.line > 0 && report.column > 0;
  qsizetype offset = report.offset;
  switch (report.place) {
  case Place::Offset:
    break;
  case Place::None:
    offset = -1;
    break;
  case Place::LineColumn:
    offset = located ? offsetForLineColumn(pre.code, report.line, report.column) : -1;
    break;
  case Place::LineStart:
    offset = report.line > 0 ? offsetForLineColumn(pre.code, report.line, 1) : -1;
    break;
  case Place::LineColumnElseLineStart:
    offset = located ? offsetForLineColumn(pre.code, report.line, report.column) : -1;
    if (offset < 0 && report.line > 0) {
      offset = offsetForLineColumn(pre.code, report.line, 1);
    }
    break;
  }
  // A position that could not be resolved covers nothing.
  const qsizetype length = report.place == Place::Offset || offset >= 0 ? report.length : 0;
  return parserDiagnostic(source, pre, std::move(diagramType), report.stage, report.code, report.message, offset, length,
                          report.line, report.column, report.production, report.actual, report.expected);
}

MermaidRenderEntry errorEntry(MermaidDiagnostic diagnostic, MermaidRenderStatus status = MermaidRenderStatus::Error) {
  MermaidRenderEntry entry;
  entry.status = status;
  entry.diagnostic = std::move(diagnostic);
  entry.errorMessage = formatMermaidDiagnostic(entry.diagnostic);
  entry.errorDiagnostic = mermaidDiagnosticToJson(entry.diagnostic);
  return entry;
}

MermaidDiagnostic preprocessingDiagnostic(const QString& source, const QString& message) {
  MermaidDiagnostic diagnostic;
  diagnostic.stage = QStringLiteral("preprocess");
  diagnostic.code = QStringLiteral("invalid-configuration");
  diagnostic.message = message;
  static const QRegularExpression yamlPosition(QStringLiteral(R"(line\s+(\d+)\s*,\s*column\s+(\d+))"),
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

std::optional<MermaidRenderEntry> upstreamLayoutConfigurationError(const MermaidPreprocessResult& pre,
                                                                   const QString& type) {
  // Unified class/flow renderers call getRegisteredLayoutAlgorithm(), which
  // falls back to Dagre for an unknown name. State passes the configured name
  // directly to render(), so an unregistered top-level layout throws. Nested
  // state.defaultRenderer only selects the legacy detector and is otherwise
  // ignored by Mermaid 11.16.
  if (type == QLatin1String("state") || type == QLatin1String("stateDiagram")) {
    const QString requested = pre.config.value(QStringLiteral("layout")).toString();
    if (requested.isEmpty() || requested == QLatin1String("dagre"))
      return std::nullopt;
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("render");
    diagnostic.code = QStringLiteral("native-render-failed");
    diagnostic.message = QStringLiteral("Unknown layout algorithm: %1").arg(requested);
    return errorEntry(std::move(diagnostic));
  }

  return std::nullopt;
}

// Upstream 11.16 (mermaid.core render): a failure inside Diagram.fromText()
// or renderer.draw() falls back to `Diagram.fromText("error")` — the
// lightbulb error diagram stays in the DOM — while the original exception
// still propagates (render() rethrows after the SVG is produced). Mirror
// that: the diagnostic entry keeps its contract AND carries the error scene
// for the export paths (PNG/SVG). The editor canvas intentionally does NOT
// consume it — BlockLayoutBuilder keeps source + diagnostic panel for Error
// entries (a deliberate Muffin editing surface, locked by
// RenderMermaidBlockTest), unlike the browser where the failed svg is
// replaced in place.
//
// suppressErrorRendering cannot be enabled through the Markdown source API
// (the frontmatter sanitizer's secure set strips it, matching upstream), so
// the fallback is unconditional here. Preprocess-stage failures (frontmatter
// YAML / directive errors) throw before mermaid's try/catch upstream and
// produce no error svg; "unsupported" has no upstream equivalent (every
// registered detector id has a native adapter).
void attachErrorFallbackScene(MermaidRenderEntry& entry, const QString& source, const QString& theme) {
  if (entry.status != MermaidRenderStatus::Error || entry.scene)
    return;
  if (entry.diagnostic.stage == QLatin1String("preprocess") || entry.diagnostic.stage == QLatin1String("unsupported"))
    return;
  try {
    MermaidPreprocessResult pre = preprocessDiagram(source);
    pre.config = mermaidRenderConfig(pre.config);
    const Diagram* errorDiagram = findMermaidDiagram(QStringLiteral("error"));
    if (!errorDiagram)
      return;
    MermaidRenderEntry fallback = errorDiagram->render(pre, QStringLiteral("error"), theme);
    if (fallback.status == MermaidRenderStatus::Ready && fallback.scene) {
      entry.scene = std::move(fallback.scene);
      entry.metadata = std::move(fallback.metadata);
      entry.naturalSize = fallback.naturalSize;
    }
  } catch (const std::exception&) {
    // The diagnostic contract stays primary; the fallback visual is
    // best-effort.
  }
}

} // namespace

MermaidRenderCache::MermaidRenderCache(QObject* parent, int capacity)
    : QObject(parent), capacity_(qMax(1, capacity)), resourceIdentity_(QUuid::createUuid().toString()) {
  debounceTimer_.setSingleShot(true);
  connect(&debounceTimer_, &QTimer::timeout, this, [this]() {
    if (!debouncePending_)
      return;
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

MermaidRenderCache::~MermaidRenderCache() {
  for (const auto& key : entries_.keys())
    LayoutResources::instance().release(resourceKey(key), false);
}

MermaidRenderKey MermaidRenderCache::makeKey(const QString& source) {
  QString theme = QStringLiteral("default");
  try {
    theme = themeFromConfig(mermaidRenderConfig(preprocessDiagram(source).config));
  } catch (...) {
    // Preprocessing failed (e.g. malformed frontmatter) — render the raw source
    // with the default theme; the worker will report the real error.
  }
  MermaidRenderKey key;
  key.sourceHash = QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256);
  key.theme = theme;
  key.fontGeneration = LayoutResources::instance().read(LayoutResources::fontKey()).geometry;
  return key;
}

QString MermaidRenderCache::resourceKey(const MermaidRenderKey& key) const {
  return QStringLiteral("mermaid:") + resourceIdentity_ + ':' + QString::fromLatin1(key.sourceHash.toHex()) + ':' +
         key.theme + ':' + QString::number(key.fontGeneration);
}

namespace {
QByteArray resourceMetrics(const MermaidRenderEntry& entry) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << int(entry.status) << entry.naturalSize << entry.errorMessage << entry.errorDiagnostic;
  return bytes;
}
} // namespace

void MermaidRenderCache::touch(const MermaidRenderKey& key) {
  lru_.removeAll(key);
  lru_.append(key);
}

void MermaidRenderCache::evict() {
  while (static_cast<int>(entries_.size()) > capacity_ && !lru_.isEmpty()) {
    const MermaidRenderKey oldest = lru_.takeFirst();
    entries_.remove(oldest);
    LayoutResources::instance().release(resourceKey(oldest), false);
  }
}

bool MermaidRenderCache::commit(const MermaidRenderKey& key, const MermaidRenderEntry& entry) {
  // Only commit if the key is still pending (not evicted while the worker ran).
  auto it = entries_.find(key);
  if (it == entries_.end())
    return false;
  if (it.value().status != MermaidRenderStatus::Loading)
    return false; // superseded
  it.value() = entry;
  touch(key);
  LayoutResources::instance().publish(resourceKey(key), resourceMetrics(entry));
  return true;
}

void MermaidRenderCache::launchWorker(const MermaidRenderKey& key, const QString& source) {
  auto* watcher = new QFutureWatcher<MermaidRenderEntry>(this);
  watchers_.insert(watcher, key);
  const QString theme = key.theme;
  const auto epoch = epoch_;
  connect(watcher, &QFutureWatcher<MermaidRenderEntry>::finished, this, [this, watcher, key, epoch]() {
    watchers_.remove(watcher);
    const MermaidRenderEntry result = watcher->result();
    watcher->deleteLater();
    if (epoch == epoch_ && commit(key, result))
      emit renderReady(key);
  });
  watcher->setFuture(QtConcurrent::run([source, theme]() { return renderSource(source, theme); }));
}

void MermaidRenderCache::cancelDebouncedRequest(bool removeLoadingEntry) {
  if (!debouncePending_)
    return;
  debounceTimer_.stop();
  if (removeLoadingEntry) {
    const auto it = entries_.constFind(debouncedKey_);
    if (it != entries_.cend() && it.value().status == MermaidRenderStatus::Loading) {
      entries_.remove(debouncedKey_);
      lru_.removeAll(debouncedKey_);
    }
  }
  debouncePending_ = false;
  debouncedSource_.clear();
}

MermaidRenderEntry MermaidRenderCache::request(const MermaidRenderKey& key, const QString& source) {
  LayoutResources::instance().read(resourceKey(key));
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

MermaidRenderEntry MermaidRenderCache::requestDebounced(const MermaidRenderKey& key, const QString& source,
                                                        int delayMs) {
  LayoutResources::instance().read(resourceKey(key));
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
  LayoutResources::instance().read(resourceKey(key));
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
  LayoutResources::instance().publish(resourceKey(key), resourceMetrics(result));
  LayoutResources::instance().read(resourceKey(key));
  return result;
}

void MermaidRenderCache::clear() {
  ++epoch_;
  cancelDebouncedRequest(false);
  const auto keys = entries_.keys();
  entries_.clear();
  lru_.clear();
  for (const auto& key : keys)
    LayoutResources::instance().release(resourceKey(key));
}

MermaidPngRenderResult MermaidRenderCache::renderMermaidSourceToPng(const QString& source, qreal dpr) {
  MermaidPngRenderResult result;
  const QString theme = makeKey(source).theme;
  const MermaidRenderEntry entry = renderSource(source, theme);
  result.error = entry.status == MermaidRenderStatus::Error;
  result.errorDiagnostic = entry.errorDiagnostic;
  // Any entry carrying a scene rasterizes — including Error entries with the
  // upstream error-diagram fallback attached (invalid sources export the
  // lightbulb exactly like a browser page or mmdc would).
  if (!entry.scene)
    return result;
  result.metadata = entry.metadata;
  dpr = qMax<qreal>(0.25, dpr);
  const QRectF clientBox = mermaidClientBox(entry);
  if (clientBox.isValid()) {
    // Client-box families raster in ONE pass through the exact SVG mapping:
    // image = round(fractional client box × dpr), content painted through
    // translate(-clientBox.topLeft()), title at its scene-absolute anchor
    // (baseline -titleTopMargin, centered on the content viewBox). No
    // per-piece rounding — a titled diagramPadding-8.25 flowchart renders
    // round(207.84375) × round(119.5) = 208 × 120 with the baseline exact,
    // where the piecewise composition produced 208 × 121 with the baseline
    // ~0.75px low.
    QImage image(qMax(1, qRound(clientBox.width() * dpr)), qMax(1, qRound(clientBox.height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(dpr, dpr);
    painter.translate(-clientBox.topLeft());
    entry.scene->paint(painter, MermaidPaintOptions{});
    if (entry.metadata.hasVisibleTitle()) {
      const qreal titleWidth = measureMermaidTitleWidth(entry.metadata);
      const qreal centerX = entry.scene->svgClientViewBox().center().x();
      paintMermaidTitle(
          entry.metadata, painter,
          QRectF(centerX - titleWidth / 2.0, -entry.metadata.titleHeight, titleWidth, entry.metadata.titleHeight));
    }
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
      return result;
    if (!image.save(&buffer, "PNG"))
      return result;
    buffer.close();
    result.dataUrl = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    return result;
  }
  QImage image = renderMermaidSceneToImage(entry.scene, dpr, entry.metadata.diagramPadding);
  if (entry.metadata.hasVisibleTitle()) {
    const qreal contentWidth = image.width() / dpr;
    const qreal contentHeight = image.height() / dpr;
    const qreal canvasWidth = qMax<qreal>(contentWidth, entry.naturalSize.width());
    const qreal canvasHeight = entry.metadata.titleHeight + contentHeight;
    QImage titled(qCeil(canvasWidth * dpr), qCeil(canvasHeight * dpr), QImage::Format_ARGB32_Premultiplied);
    titled.fill(Qt::transparent);
    QPainter painter(&titled);
    // Piecewise families center the content under the title strip when the
    // title widened the canvas.
    painter.drawImage(QPoint(qRound((canvasWidth > contentWidth ? (canvasWidth - contentWidth) / 2.0 : 0.0) * dpr),
                             qRound(entry.metadata.titleHeight * dpr)),
                      image);
    painter.scale(dpr, dpr);
    paintMermaidTitle(entry.metadata, painter, QRectF(0.0, 0.0, canvasWidth, entry.metadata.titleHeight));
    painter.end();
    image = std::move(titled);
  }
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly))
    return result;
  if (!image.save(&buffer, "PNG"))
    return result;
  buffer.close();
  result.dataUrl = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
  return result;
}

QString MermaidRenderCache::renderMermaidSourceToPngDataUrl(const QString& source, qreal dpr) {
  return renderMermaidSourceToPng(source, dpr).dataUrl;
}

MermaidSvgRenderResult MermaidRenderCache::renderMermaidSourceToSvg(const QString& source, qsizetype instanceIndex) {
  return renderMermaidSourceToSvg(source, instanceIndex, QUrl{});
}

MermaidSvgRenderResult MermaidRenderCache::renderMermaidSourceToSvg(const QString& source, qsizetype instanceIndex,
                                                                    const QUrl& documentUrl, const QString& diagramId) {
  MermaidSvgRenderResult result;
  const MermaidRenderKey key = makeKey(source);
  const MermaidRenderEntry entry = renderSource(source, key.theme);
  result.error = entry.status == MermaidRenderStatus::Error;
  result.errorDiagnostic = entry.errorDiagnostic;
  MermaidSvgExportOptions options;
  options.instanceIndex = instanceIndex;
  options.documentUrl = documentUrl;
  options.diagramId = diagramId;
  result.svg = renderMermaidEntryToSvg(entry, options);
  if (result.svg.isEmpty())
    return result;
  result.metadata = entry.metadata;
  result.dataUrl = QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(result.svg.toBase64());
  return result;
}

QString MermaidRenderCache::renderMermaidSourceToSvgDataUrl(const QString& source, qsizetype instanceIndex) {
  return renderMermaidSourceToSvg(source, instanceIndex).dataUrl;
}

MermaidRenderEntry MermaidRenderCache::renderSourceDispatch(const QString& source, const QString& theme) {
  MermaidPreprocessResult pre;
  try {
    pre = preprocessDiagram(source);
    pre.config = mermaidRenderConfig(pre.config);
  } catch (const std::exception& error) {
    return errorEntry(preprocessingDiagnostic(source, QString::fromUtf8(error.what())));
  }

  QString type;
  try {
    type = detectDiagramType(pre.code, pre.config);
  } catch (const UnknownDiagramError& error) {
    MermaidDiagnostic diagnostic;
    diagnostic.stage = QStringLiteral("detector");
    diagnostic.code = QStringLiteral("missing-diagram-header");
    // Preserve the detector's upstream-compatible exception message. The
    // structured code/span still gives Muffin a friendlier editor diagnostic
    // without changing Mermaid's observable failure contract.
    diagnostic.message = QString::fromUtf8(error.what());
    diagnostic.expected = {QStringLiteral("flowchart"),    QStringLiteral("sequenceDiagram"),
                           QStringLiteral("classDiagram"), QStringLiteral("stateDiagram-v2"),
                           QStringLiteral("erDiagram"),    QStringLiteral("requirementDiagram"),
                           QStringLiteral("pie"),          QStringLiteral("quadrantChart"),
                           QStringLiteral("journey"),      QStringLiteral("radar-beta")};
    diagnostic.expected.append(QStringLiteral("xychart-beta"));
    diagnostic.expected.append(QStringLiteral("timeline"));
    diagnostic.expected.append(QStringLiteral("packet-beta"));
    diagnostic.expected.append(QStringLiteral("kanban"));
    diagnostic.expected.append(QStringLiteral("mindmap"));
    diagnostic.expected.append(QStringLiteral("gitGraph"));
    diagnostic.expected.append(QStringLiteral("gantt"));
    diagnostic.expected.append(QStringLiteral("info"));
    diagnostic.expected.append(QStringLiteral("treeView-beta"));
    diagnostic.expected.append(QStringLiteral("eventmodeling"));
    diagnostic.expected.append(QStringLiteral("ishikawa-beta"));
    diagnostic.expected.append(QStringLiteral("venn-beta"));
    diagnostic.expected.append(QStringLiteral("sankey-beta"));
    diagnostic.expected.append(QStringLiteral("treemap-beta"));
    diagnostic.expected.append(QStringLiteral("cynefin-beta"));
    diagnostic.expected.append(QStringLiteral("wardley-beta"));
    diagnostic.expected.append(QStringLiteral("architecture-beta"));
    diagnostic.expected.append(QStringLiteral("C4Context"));
    diagnostic.expected.append(QStringLiteral("C4Container"));
    diagnostic.expected.append(QStringLiteral("C4Component"));
    diagnostic.expected.append(QStringLiteral("C4Dynamic"));
    diagnostic.expected.append(QStringLiteral("C4Deployment"));
    diagnostic.expected.append(QStringLiteral("block-beta"));
    diagnostic.expected.append(QStringLiteral("swimlane-beta"));
    diagnostic.expected.append(QStringLiteral("railroad-beta"));
    diagnostic.expected.append(QStringLiteral("railroad-ebnf-beta"));
    diagnostic.expected.append(QStringLiteral("railroad-abnf-beta"));
    diagnostic.expected.append(QStringLiteral("railroad-peg-beta"));
    diagnostic.span = mappedSourceSpan(source, pre, 0, pre.code.isEmpty() ? 0 : 1, 1, 1);
    return errorEntry(std::move(diagnostic));
  }
  if (const auto layoutError = upstreamLayoutConfigurationError(pre, type))
    return *layoutError;
  const Diagram* diagram = findMermaidDiagram(type);
  if (!diagram) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("unsupported");
    diagnostic.code = QStringLiteral("unsupported-diagram");
    diagnostic.message = QStringLiteral("Diagram type '%1' is not natively supported.").arg(type);
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
    entry.errorDiagnostic.insert(QStringLiteral("renderFailure"), error.failure().toJson());
    entry.errorDiagnostic.insert(QStringLiteral("component"), QStringLiteral("sequence-label-mathml"));
    return entry;
  } catch (const DiagramError& error) {
    // Every diagram's parse error; each says how its own failure reads as a diagnostic.
    return errorEntry(parserDiagnostic(source, pre, type, error.info()));
  } catch (const std::exception& error) {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("render");
    diagnostic.code = QStringLiteral("native-render-failed");
    diagnostic.message = QString::fromUtf8(error.what());
    return errorEntry(std::move(diagnostic));
  }
}

MermaidRenderEntry MermaidRenderCache::renderSource(const QString& source, const QString& theme) {
  MermaidRenderEntry entry = renderSourceDispatch(source, theme);
  attachErrorFallbackScene(entry, source, theme);
  return entry;
}

} // namespace muffin::mermaid::editor
