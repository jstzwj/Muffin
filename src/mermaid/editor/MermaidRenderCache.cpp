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
