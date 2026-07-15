#include "mermaid/editor/MermaidRenderCache.h"

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/scene/FlowScenePainter.h"
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
  if (entry.status != MermaidRenderStatus::Ready || !entry.scene) return {};
  const QImage image = flowscene::renderFlowSceneToImage(*entry.scene, dpr, 8.0, QStringLiteral("Arial"));
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
  if (type != QLatin1String("flowchart") && type != QLatin1String("flowchart-v2")) {
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Unsupported;
    entry.errorMessage = QStringLiteral("Diagram type '%1' is not natively supported").arg(type);
    return entry;
  }

  try {
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme), themeOverrides(pre.config));
    const QJsonObject flowConfig = pre.config.value(QStringLiteral("flowchart")).toObject();
    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
    textOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    const qreal padding = configNumber(flowConfig, QStringLiteral("padding"), 15.0);
    textOptions.horizontalPadding = padding * 2.0;
    textOptions.verticalPadding = padding;
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data(), textOptions);
    flowchart::FlowLayoutOptions layoutOptions;
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
    flowscene::FlowScene scene = flowscene::buildFlowScene(chart.data(), layout, themeVars);
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
