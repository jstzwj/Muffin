#include "mermaid/editor/MermaidSvgExporter.h"

#include "blocks/html/HtmlUrlSafety.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/FlowScenePainter.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QPainter>
#include <QSvgGenerator>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace muffin::mermaid::editor {
namespace {

constexpr auto kSvgNamespace = "http://www.w3.org/2000/svg";
constexpr auto kXLinkNamespace = "http://www.w3.org/1999/xlink";

struct SvgCanvas {
  QSize size;
  // The exact fractional client box the serialized root carries (viewBox
  // origin + dimensions + max-width): the scene's svgClientViewBox united
  // with the title text box (upstream: no translate — the raw scene origin
  // survives into the viewBox). `size` stays the integer raster canvas
  // (naturalSize). Invalid for families without a client-box contract.
  QRectF clientBox;
  // The title strip in PAINTER coordinates (bottom() == baseline +
  // titleTopMargin): scene-absolute for clientBox families, canvas-relative
  // otherwise.
  QRectF titleRect;
  QPointF sceneOffset;
};

struct SvgInteraction {
  QRectF bounds;
  QString href;
  QString toolTip;
};

QString compactNumber(qreal value) {
  QString result = QString::number(value, 'f', 3);
  while (result.contains(QLatin1Char('.')) && result.endsWith(QLatin1Char('0')))
    result.chop(1);
  if (result.endsWith(QLatin1Char('.'))) result.chop(1);
  return result;
}

// Fractional SVG root lengths (LayoutUnit-quantized client boxes are exact
// in six decimals — 1/64 = 0.015625); trailing zeros are trimmed so integer
// lengths stay "512".
QString formatSvgLength(qreal value) {
  QString result = QString::number(value, 'f', 6);
  while (result.contains(QLatin1Char('.')) && result.endsWith(QLatin1Char('0')))
    result.chop(1);
  if (result.endsWith(QLatin1Char('.'))) result.chop(1);
  return result;
}

QString svgRootId(const MermaidRenderEntry& entry, qsizetype instanceIndex) {
  const auto& metadata = entry.metadata;
  instanceIndex = std::max<qsizetype>(0, instanceIndex);
  if (metadata.svgDeterministicIds) {
    return QStringLiteral("mermaid-%1")
        .arg(metadata.svgDeterministicIdSeed.size() + instanceIndex);
  }

  QByteArray identity = metadata.diagramType.toUtf8();
  identity += '|';
  identity += QByteArray::number(entry.naturalSize.width());
  identity += 'x';
  identity += QByteArray::number(entry.naturalSize.height());
  // Contribute every family's canonical scene dump so the digest is
  // content-unique (previously only flowchart contributed; same-sized
  // class/sequence/state/er diagrams collided on the SVG root id).
  QJsonObject sceneJson;
  if (entry.scene) sceneJson = entry.scene->toJsonObject();
  if (!sceneJson.isEmpty())
    identity += QJsonDocument(sceneJson).toJson(QJsonDocument::Compact);
  const QByteArray digest = QCryptographicHash::hash(
      identity, QCryptographicHash::Sha256).toHex().left(16);
  QString id = QStringLiteral("mfn-mermaid-") + QString::fromLatin1(digest);
  if (instanceIndex > 0) id += QStringLiteral("-%1").arg(instanceIndex);
  return id;
}

SvgCanvas svgCanvas(const MermaidRenderEntry& entry) {
  SvgCanvas canvas;
  canvas.size = entry.naturalSize.expandedTo(QSize(1, 1));
  const qreal titleHeight = entry.metadata.titleHeight;
  if (entry.scene) {
    // Client-box families (state/error/architecture): paint in the SCENE's
    // own coordinates and let the root viewBox carry the raw fractional
    // origin — the browser model (setupViewPortForSVG writes
    // svgBBox(content ∪ title) ± padding with no translate). The title
    // anchors at its scene-absolute position (baseline -titleTopMargin,
    // centered on the content bbox center).
    canvas.clientBox = mermaidClientBox(entry);
    if (canvas.clientBox.isValid()) {
      if (entry.metadata.hasVisibleTitle()) {
        const qreal titleWidth = measureMermaidTitleWidth(entry.metadata);
        const qreal centerX =
            entry.scene->svgClientViewBox().center().x();
        canvas.titleRect = QRectF(centerX - titleWidth / 2.0, -titleHeight,
                                  titleWidth, titleHeight);
      }
      return canvas;
    }
    // Every other family exposes one base extent (sceneBounds, or sequence's
    // resolved viewport); apply the diagram padding uniformly, then center it
    // below the title strip.
    const qreal padding = entry.metadata.diagramPadding;
    const QRectF extent =
        entry.scene->renderBounds().adjusted(-padding, -padding, padding, padding);
    qreal clientWidth = extent.width();
    if (entry.metadata.hasVisibleTitle())
      clientWidth = qMax(clientWidth,
                         measureMermaidTitleWidth(entry.metadata) + 16.0);
    const QSizeF clientSize(clientWidth, extent.height() + titleHeight);
    canvas.clientBox = QRectF(QPointF(0.0, 0.0), clientSize);
    canvas.titleRect = QRectF(0.0, 0.0, clientSize.width(), titleHeight);
    canvas.sceneOffset = QPointF(
        (clientWidth - extent.width()) / 2.0 - extent.left(),
        titleHeight - extent.top());
  }
  return canvas;
}

void paintEntry(const MermaidRenderEntry& entry, const SvgCanvas& canvas,
                QPainter& painter) {
  painter.save();
  painter.translate(canvas.sceneOffset);
  if (entry.scene) {
    MermaidPaintOptions options;
    options.paintEdgeMarkers = false;
    entry.scene->paint(painter, options);
  }
  painter.restore();
  paintMermaidTitle(entry.metadata, painter, canvas.titleRect);
}

QString cssEscapeUrl(QString value) {
  // CSS.escape(window.location...) as used by Mermaid Sequence. ASCII URL
  // punctuation is escaped with a backslash; alphanumerics, '-' and '_' stay.
  QString result;
  result.reserve(value.size() * 2);
  for (const QChar ch : value) {
    if (ch.isLetterOrNumber() || ch == QLatin1Char('-') ||
        ch == QLatin1Char('_')) {
      result.append(ch);
    } else {
      result.append(QLatin1Char('\\'));
      result.append(ch);
    }
  }
  return result;
}

QString absoluteMarkerBase(const MermaidRenderEntry& entry,
                           const MermaidSvgExportOptions& options) {
  // Family capability is precomputed in the metadata (svgMarkerAbsoluteEligible /
  // svgMarkerUrlCssEscape) — this serializer stays family-agnostic.
  if (!entry.metadata.svgArrowMarkerAbsolute || !entry.metadata.svgMarkerAbsoluteEligible ||
      options.documentUrl.isEmpty())
    return {};
  QUrl url = options.documentUrl;
  url.setFragment({});
  const QString serialized = url.toString(QUrl::FullyEncoded);
  return entry.metadata.svgMarkerUrlCssEscape ? cssEscapeUrl(serialized) : serialized;
}

QString markerId(const QString& rootId, const SvgMarkerDefinition& definition) {
  return rootId + definition.idSuffix;
}

void writeMarkerChild(QXmlStreamWriter& writer, const SvgMarkerChild& child) {
  writer.writeStartElement(child.tag);
  if (!child.cssClass.isEmpty())
    writer.writeAttribute(QStringLiteral("class"), child.cssClass);
  if (!child.path.isEmpty()) writer.writeAttribute(QStringLiteral("d"), child.path);
  if (!child.points.isEmpty()) writer.writeAttribute(QStringLiteral("points"), child.points);
  if (!child.viewBox.isEmpty()) writer.writeAttribute(QStringLiteral("viewBox"), child.viewBox);
  if (child.tag == QLatin1String("circle")) {
    writer.writeAttribute(QStringLiteral("cx"), compactNumber(child.cx));
    writer.writeAttribute(QStringLiteral("cy"), compactNumber(child.cy));
    writer.writeAttribute(QStringLiteral("r"), compactNumber(child.radius));
  } else if (child.tag == QLatin1String("line")) {
    writer.writeAttribute(QStringLiteral("x1"), compactNumber(child.x1));
    writer.writeAttribute(QStringLiteral("y1"), compactNumber(child.y1));
    writer.writeAttribute(QStringLiteral("x2"), compactNumber(child.x2));
    writer.writeAttribute(QStringLiteral("y2"), compactNumber(child.y2));
  }
  if (!child.fill.isEmpty()) writer.writeAttribute(QStringLiteral("fill"), child.fill);
  if (!child.stroke.isEmpty()) writer.writeAttribute(QStringLiteral("stroke"), child.stroke);
  if (!child.strokeWidth.isEmpty())
    writer.writeAttribute(QStringLiteral("stroke-width"), child.strokeWidth);
  if (!child.style.isEmpty()) writer.writeAttribute(QStringLiteral("style"), child.style);
  writer.writeEndElement();
}

void writeMarkers(QXmlStreamWriter& writer, const SvgMarkerProjection& projection,
                  const QString& rootId, const QString& absoluteBase,
                  const QPointF& sceneOffset) {
  if (projection.empty()) return;
  QHash<QString, QString> ids;
  writer.writeStartElement(QStringLiteral("defs"));
  writer.writeAttribute(QStringLiteral("id"), rootId + QStringLiteral("-marker-defs"));
  for (const SvgMarkerDefinition& definition : projection.definitions) {
    const QString id = markerId(rootId, definition);
    ids.insert(definition.key, id);
    writer.writeStartElement(QStringLiteral("marker"));
    writer.writeAttribute(QStringLiteral("id"), id);
    if (!definition.viewBox.isEmpty())
      writer.writeAttribute(QStringLiteral("viewBox"), definition.viewBox);
    writer.writeAttribute(QStringLiteral("refX"), compactNumber(definition.refX));
    writer.writeAttribute(QStringLiteral("refY"), compactNumber(definition.refY));
    writer.writeAttribute(QStringLiteral("markerWidth"), compactNumber(definition.markerWidth));
    writer.writeAttribute(QStringLiteral("markerHeight"), compactNumber(definition.markerHeight));
    if (!definition.markerUnits.isEmpty())
      writer.writeAttribute(QStringLiteral("markerUnits"), definition.markerUnits);
    writer.writeAttribute(QStringLiteral("orient"), definition.orient);
    if (definition.groupChildren) writer.writeStartElement(QStringLiteral("g"));
    for (const SvgMarkerChild& child : definition.children)
      writeMarkerChild(writer, child);
    if (definition.groupChildren) writer.writeEndElement();
    writer.writeEndElement();
  }
  writer.writeEndElement();

  writer.writeStartElement(QStringLiteral("g"));
  writer.writeAttribute(QStringLiteral("id"), rootId + QStringLiteral("-marker-edges"));
  writer.writeAttribute(QStringLiteral("class"), QStringLiteral("mfn-mermaid-marker-edges"));
  writer.writeAttribute(QStringLiteral("transform"),
                        QStringLiteral("translate(%1 %2)")
                            .arg(compactNumber(sceneOffset.x()),
                                 compactNumber(sceneOffset.y())));
  const auto reference = [&](const QString& key) {
    const QString id = ids.value(key);
    return id.isEmpty() ? QString() :
        QStringLiteral("url(%1#%2)").arg(absoluteBase, id);
  };
  for (const SvgMarkerEdge& edge : projection.edges) {
    writer.writeStartElement(edge.tag);
    if (!edge.id.isEmpty()) writer.writeAttribute(QStringLiteral("data-edge-id"), edge.id);
    if (!edge.cssClass.isEmpty()) writer.writeAttribute(QStringLiteral("class"), edge.cssClass);
    if (edge.tag == QLatin1String("line")) {
      writer.writeAttribute(QStringLiteral("x1"), compactNumber(edge.start.x()));
      writer.writeAttribute(QStringLiteral("y1"), compactNumber(edge.start.y()));
      writer.writeAttribute(QStringLiteral("x2"), compactNumber(edge.end.x()));
      writer.writeAttribute(QStringLiteral("y2"), compactNumber(edge.end.y()));
    } else {
      writer.writeAttribute(QStringLiteral("d"), edge.path);
    }
    writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
    writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("none"));
    if (!edge.markerStart.isEmpty())
      writer.writeAttribute(QStringLiteral("marker-start"), reference(edge.markerStart));
    if (!edge.markerEnd.isEmpty())
      writer.writeAttribute(QStringLiteral("marker-end"), reference(edge.markerEnd));
    writer.writeEndElement();
  }
  writer.writeEndElement();
}

QVector<SvgInteraction> interactions(const MermaidRenderEntry& entry,
                                     const SvgCanvas& canvas) {
  QVector<SvgInteraction> result;
  if (!entry.scene) return result;
  const bool force = entry.scene->menusAlwaysOpen();
  for (const auto& r : entry.scene->interactionRegions()) {
    if (!r.togglesMenu.isEmpty()) continue;                       // actor toggles are editor-only
    if (!r.requiresOpenMenu.isEmpty() && !force) continue;         // seq item only under forceMenus
    const QString href = isSafeUrl(r.href, false) ? r.href : QString();
    if (!r.requiresOpenMenu.isEmpty() && href.isEmpty()) continue; // seq item w/o safe link -> no region
    if (href.isEmpty() && r.accessibleLabel.isEmpty()) continue;   // flow node w/o link+label -> skip
    result.append({r.bounds.translated(canvas.sceneOffset), href, r.accessibleLabel});
  }
  return result;
}

bool rootAttributeIsReplaced(QStringView name) {
  static const QSet<QString> replaced = {
      QStringLiteral("id"), QStringLiteral("class"),
      QStringLiteral("width"), QStringLiteral("height"),
      QStringLiteral("style"), QStringLiteral("viewBox"),
      QStringLiteral("role"), QStringLiteral("aria-roledescription"),
      QStringLiteral("aria-labelledby"), QStringLiteral("aria-describedby")};
  return replaced.contains(name.toString());
}

void writeNamespaces(QXmlStreamWriter& writer,
                     const QXmlStreamNamespaceDeclarations& declarations,
                     bool root) {
  bool hasDefaultSvg = false;
  for (const auto& declaration : declarations) {
    const QString prefix = declaration.prefix().toString();
    const QString uri = declaration.namespaceUri().toString();
    if (prefix == QLatin1String("xlink")) continue;
    if (prefix.isEmpty() && uri == QLatin1String(kSvgNamespace))
      hasDefaultSvg = true;
    writer.writeNamespace(uri, prefix);
  }
  if (root && !hasDefaultSvg)
    writer.writeDefaultNamespace(QString::fromLatin1(kSvgNamespace));
  if (root)
    writer.writeNamespace(QString::fromLatin1(kXLinkNamespace),
                          QStringLiteral("xlink"));
}

void writeInteractions(QXmlStreamWriter& writer,
                       const QVector<SvgInteraction>& items,
                       const QString& rootId) {
  if (items.isEmpty()) return;
  writer.writeStartElement(QStringLiteral("g"));
  writer.writeAttribute(QStringLiteral("id"), rootId + QStringLiteral("-links"));
  writer.writeAttribute(QStringLiteral("class"), QStringLiteral("mfn-mermaid-links"));
  for (qsizetype index = 0; index < items.size(); ++index) {
    const SvgInteraction& item = items.at(index);
    if (!item.href.isEmpty()) {
      writer.writeStartElement(QStringLiteral("a"));
      writer.writeAttribute(QStringLiteral("id"),
                            rootId + QStringLiteral("-link-%1").arg(index));
      writer.writeAttribute(QStringLiteral("href"), item.href);
      writer.writeAttribute(QStringLiteral("xlink:href"), item.href);
      writer.writeAttribute(QStringLiteral("target"), QStringLiteral("_blank"));
      writer.writeAttribute(QStringLiteral("rel"),
                            QStringLiteral("noopener noreferrer"));
      writer.writeAttribute(QStringLiteral("tabindex"), QStringLiteral("0"));
      writer.writeAttribute(QStringLiteral("aria-label"),
                            item.toolTip.isEmpty() ? item.href : item.toolTip);
    } else {
      writer.writeStartElement(QStringLiteral("g"));
    }
    if (!item.toolTip.isEmpty())
      writer.writeTextElement(QStringLiteral("title"), item.toolTip);
    writer.writeStartElement(QStringLiteral("rect"));
    writer.writeAttribute(QStringLiteral("x"), compactNumber(item.bounds.x()));
    writer.writeAttribute(QStringLiteral("y"), compactNumber(item.bounds.y()));
    writer.writeAttribute(QStringLiteral("width"),
                          compactNumber(item.bounds.width()));
    writer.writeAttribute(QStringLiteral("height"),
                          compactNumber(item.bounds.height()));
    writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("#ffffff"));
    writer.writeAttribute(QStringLiteral("fill-opacity"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("none"));
    writer.writeAttribute(QStringLiteral("pointer-events"), QStringLiteral("all"));
    writer.writeEndElement();
    writer.writeEndElement();
  }
  writer.writeEndElement();
}

QByteArray normalizeSvg(const QByteArray& generated,
                        const MermaidRenderEntry& entry,
                        const SvgCanvas& canvas,
                        const MermaidSvgExportOptions& options) {
  QXmlStreamReader reader(generated);
  QByteArray output;
  QXmlStreamWriter writer(&output);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);

  const QString rootId = svgRootId(entry, options.instanceIndex);
  const QString diagramId = options.diagramId.isEmpty()
      ? rootId : options.diagramId;
  const QString titleId = rootId + QStringLiteral("-title");
  const QString descriptionId = rootId + QStringLiteral("-desc");
  const QString accessibleTitle = entry.metadata.accessibleName();
  const QString accessibleDescription =
      entry.metadata.accessibleDescription.trimmed();
  const QVector<SvgInteraction> svgInteractions = interactions(entry, canvas);
  const SvgMarkerProjection markerProjection =
      entry.scene ? entry.scene->svgMarkerProjection() : SvgMarkerProjection{};
  const QString markerBase = absoluteMarkerBase(entry, options);
  int depth = 0;
  bool rootSeen = false;

  while (!reader.atEnd()) {
    reader.readNext();
    switch (reader.tokenType()) {
      case QXmlStreamReader::StartDocument:
      case QXmlStreamReader::EndDocument:
      case QXmlStreamReader::DTD:
        break;
      case QXmlStreamReader::StartElement: {
        const bool root = !rootSeen;
        // QSvgGenerator injects root-level placeholder metadata
        // ("Qt SVG Document" / "Generated with Qt") even though Mermaid did
        // not request it. Drop only those direct root children; accessible
        // Mermaid metadata is emitted below and interaction-region <title>
        // elements are written later by writeInteractions().
        if (!root && depth == 1 &&
            (reader.name() == QLatin1String("title") ||
             reader.name() == QLatin1String("desc"))) {
          reader.skipCurrentElement();
          break;
        }
        if (root) rootSeen = true;
        writer.writeStartElement(reader.qualifiedName().toString());
        writeNamespaces(writer, reader.namespaceDeclarations(), root);
        for (const auto& attribute : reader.attributes()) {
          if (!root || !rootAttributeIsReplaced(attribute.qualifiedName()))
            writer.writeAttribute(attribute.qualifiedName().toString(),
                                  attribute.value().toString());
        }
        ++depth;
        if (root) {
          writer.writeAttribute(QStringLiteral("id"), rootId);
          // Families whose upstream svg carries no class (the error diagram)
          // keep only the Muffin embedding marker.
          writer.writeAttribute(
              QStringLiteral("class"),
              entry.metadata.cssClass.isEmpty()
                  ? QStringLiteral("mfn-mermaid")
                  : QStringLiteral("mfn-mermaid ") + entry.metadata.cssClass);
          if (entry.metadata.svgEmitViewBox) {
            // Scenes with a client-box contract (state getBBox extents, the
            // error diagram's LayoutUnit 108.671875, architecture's fcose
            // union) carry the exact fractional box — origin included (the
            // browser writes svgBBox ± padding with no translate), with the
            // title union so a titled export never clips its own title.
            const bool clientMode =
                entry.scene && entry.scene->svgClientViewBox().isValid();
            const QString viewBoxValue = clientMode
                ? QStringLiteral("%1 %2 %3 %4")
                      .arg(formatSvgLength(canvas.clientBox.x()),
                           formatSvgLength(canvas.clientBox.y()),
                           formatSvgLength(canvas.clientBox.width()),
                           formatSvgLength(canvas.clientBox.height()))
                : QStringLiteral("0 0 %1 %2")
                      .arg(canvas.size.width())
                      .arg(canvas.size.height());
            writer.writeAttribute(QStringLiteral("viewBox"), viewBoxValue);
          }
          if (entry.metadata.svgUseMaxWidth) {
            writer.writeAttribute(QStringLiteral("width"), QStringLiteral("100%"));
            // The browser pins max-width to the fractional viewBox width
            // (e.g. 104.390625px), not the raster-rounded canvas int.
            const bool clientMode =
                entry.scene && entry.scene->svgClientViewBox().isValid();
            const QString maxWidth = clientMode
                ? formatSvgLength(canvas.clientBox.width())
                : QString::number(canvas.size.width());
            writer.writeAttribute(
                QStringLiteral("style"),
                QStringLiteral("max-width: %1px;").arg(maxWidth));
          } else {
            // Fixed sizing writes the FRACTIONAL client box exactly like the
            // browser (upstream svg.attr('width', viewBoxWidth) — e.g.
            // width="207.84375" height="70.5", NOT the raster-rounded ints).
            const bool clientMode =
                entry.scene && entry.scene->svgClientViewBox().isValid();
            writer.writeAttribute(
                QStringLiteral("width"),
                clientMode ? formatSvgLength(canvas.clientBox.width())
                           : QString::number(canvas.size.width()));
            writer.writeAttribute(
                QStringLiteral("height"),
                clientMode ? formatSvgLength(canvas.clientBox.height())
                           : QString::number(canvas.size.height()));
          }
          writer.writeAttribute(QStringLiteral("role"),
                                QStringLiteral("graphics-document document"));
          if (!entry.metadata.roleDescription.isEmpty())
            writer.writeAttribute(QStringLiteral("aria-roledescription"),
                                  entry.metadata.roleDescription);
          if (entry.metadata.svgEmitAccessibleTitle)
            writer.writeAttribute(QStringLiteral("aria-labelledby"), titleId);
          if (!accessibleDescription.isEmpty())
            writer.writeAttribute(QStringLiteral("aria-describedby"),
                                  descriptionId);
          writer.writeAttribute(QStringLiteral("data-diagram-type"),
                                entry.metadata.diagramType);
          if (entry.metadata.svgEmitAccessibleTitle) {
            writer.writeStartElement(QStringLiteral("title"));
            writer.writeAttribute(QStringLiteral("id"), titleId);
            writer.writeCharacters(accessibleTitle);
            writer.writeEndElement();
          }
          if (!accessibleDescription.isEmpty()) {
            writer.writeStartElement(QStringLiteral("desc"));
            writer.writeAttribute(QStringLiteral("id"), descriptionId);
            writer.writeCharacters(accessibleDescription);
            writer.writeEndElement();
          }
          writeMarkers(writer, markerProjection, diagramId, markerBase,
                       canvas.sceneOffset);
        }
        break;
      }
      case QXmlStreamReader::EndElement:
        if (depth == 1)
          writeInteractions(writer, svgInteractions, rootId);
        writer.writeEndElement();
        --depth;
        break;
      case QXmlStreamReader::Characters:
        if (reader.isCDATA())
          writer.writeCDATA(reader.text().toString());
        else
          writer.writeCharacters(reader.text().toString());
        break;
      case QXmlStreamReader::Comment:
        writer.writeComment(reader.text().toString());
        break;
      case QXmlStreamReader::ProcessingInstruction:
        writer.writeProcessingInstruction(
            reader.processingInstructionTarget().toString(),
            reader.processingInstructionData().toString());
        break;
      case QXmlStreamReader::EntityReference:
        writer.writeEntityReference(reader.name().toString());
        break;
      default:
        break;
    }
  }
  if (reader.hasError() || !rootSeen) return {};
  return output;
}

}  // namespace

QByteArray renderMermaidEntryToSvg(const MermaidRenderEntry& entry,
                                   qsizetype instanceIndex) {
  MermaidSvgExportOptions options;
  options.instanceIndex = instanceIndex;
  return renderMermaidEntryToSvg(entry, options);
}

QByteArray renderMermaidEntryToSvg(const MermaidRenderEntry& entry,
                                   const MermaidSvgExportOptions& options) {
  // Any entry carrying a scene serializes — including Error entries with the
  // upstream error-diagram fallback attached (invalid sources export the
  // lightbulb exactly like a browser page or mmdc would).
  if (!entry.scene)
    return {};

  const SvgCanvas canvas = svgCanvas(entry);
  QByteArray generated;
  QBuffer buffer(&generated);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  QSvgGenerator generator;
  generator.setOutputDevice(&buffer);
  generator.setSize(canvas.size);
  // Client-box families paint in the scene's own coordinates: the generator
  // viewBox must MATCH the serialized root viewBox (raw fractional origin),
  // or the child coordinates would not line up with the replaced attribute.
  generator.setViewBox(entry.scene && entry.scene->svgClientViewBox().isValid()
                           ? canvas.clientBox
                           : QRectF(QPointF(0.0, 0.0), QSizeF(canvas.size)));
  generator.setResolution(96);
  QPainter painter;
  if (!painter.begin(&generator)) return {};
  paintEntry(entry, canvas, painter);
  painter.end();
  buffer.close();
  return normalizeSvg(generated, entry, canvas, options);
}

}  // namespace muffin::mermaid::editor
