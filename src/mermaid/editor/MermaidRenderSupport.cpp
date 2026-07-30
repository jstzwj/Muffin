#include "mermaid/editor/MermaidRenderSupport.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace muffin::mermaid::editor {
namespace {

QString configString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
  if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  return {};
}

}  // namespace

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

MermaidRenderMetadata renderMetadata(
    const MermaidPreprocessResult& pre, const QString& diagramType,
    const QString& diagramTitle, const QString& accessibleTitle,
    const QString& accessibleDescription, const QString& titleColor,
    const QString& fontFamily, qreal titleFontSize,
    qreal titleTopMargin, qreal diagramPadding) {
  MermaidRenderMetadata metadata;
  metadata.diagramType = diagramType;
  metadata.roleDescription = diagramType;
  if (diagramType.startsWith(QLatin1String("flowchart")))
    metadata.cssClass = QStringLiteral("flowchart");
  else if (diagramType == QLatin1String("sequence"))
    metadata.cssClass = QStringLiteral("sequenceDiagram");
  else if (diagramType.startsWith(QLatin1String("class")))
    metadata.cssClass = QStringLiteral("classDiagram");
  else if (diagramType == QLatin1String("er"))
    metadata.cssClass = QStringLiteral("erDiagram");
  else
    metadata.cssClass = QStringLiteral("stateDiagram");
  metadata.title = diagramTitle.trimmed().isEmpty() ? pre.title : diagramTitle;
  metadata.accessibleTitle = accessibleTitle;
  metadata.accessibleDescription = accessibleDescription;
  metadata.titleColor = titleColor;
  metadata.fontFamily = firstFontFamily(fontFamily);
  metadata.titleFontSize = titleFontSize;
  metadata.titleTopMargin = titleTopMargin;
  metadata.diagramPadding = qMax<qreal>(0.0, diagramPadding);
  const bool classDiagram = diagramType.startsWith(QLatin1String("class"));
  QString configSection = QStringLiteral("state");
  if (diagramType.startsWith(QLatin1String("flowchart")))
    configSection = QStringLiteral("flowchart");
  else if (diagramType == QLatin1String("sequence"))
    configSection = QStringLiteral("sequence");
  // Mermaid 11.16's unified class renderer reads state.useMaxWidth; the class
  // field itself is upstream-inert and the generated effect matrix records it.
  const QJsonObject svgConfig = pre.config.value(configSection).toObject();
  metadata.svgUseMaxWidth = svgConfig.contains(QStringLiteral("useMaxWidth"))
      ? svgConfig.value(QStringLiteral("useMaxWidth")).toBool(true) : true;
  const QJsonObject familyConfig = pre.config.value(
      classDiagram ? QStringLiteral("class") : configSection).toObject();
  metadata.svgArrowMarkerAbsolute = familyConfig.contains(
      QStringLiteral("arrowMarkerAbsolute"))
      ? familyConfig.value(QStringLiteral("arrowMarkerAbsolute")).toBool(false)
      : pre.config.value(QStringLiteral("arrowMarkerAbsolute")).toBool(false);
  metadata.svgDeterministicIds =
      pre.config.value(QStringLiteral("deterministicIds")).toBool(false);
  metadata.svgDeterministicIdSeed =
      pre.config.value(QStringLiteral("deterministicIDSeed")).toString();
  return metadata;
}

void finalizeReadyEntry(MermaidRenderEntry& entry,
                        MermaidRenderMetadata metadata) {
  metadata.contentSize = QSizeF(entry.naturalSize);
  entry.naturalSize = QSize(
      qCeil(metadata.contentSize.width() + 2.0 * metadata.diagramPadding),
      qCeil(metadata.contentSize.height() + 2.0 * metadata.diagramPadding));
  if (metadata.hasVisibleTitle()) {
    // Mermaid places its title 25 px above the diagram and reserves about a
    // 40 px strip in the resulting SVG viewBox. Grow for larger configured
    // margins while retaining that 11.16 default geometry.
    metadata.titleHeight = qCeil(qMax(
        40.0, metadata.titleTopMargin +
                  qMax(15.0, metadata.titleFontSize * 0.75)));
    const qreal titleWidth = measureMermaidTitleWidth(metadata) + 16.0;
    entry.naturalSize.setWidth(qCeil(qMax(
        static_cast<qreal>(entry.naturalSize.width()), titleWidth)));
    entry.naturalSize.setHeight(qCeil(
        entry.naturalSize.height() + metadata.titleHeight));
  }
  entry.metadata = std::move(metadata);
}

}  // namespace muffin::mermaid::editor
