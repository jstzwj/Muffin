#include "mermaid/editor/MermaidSvgExporter.h"

#include "blocks/html/HtmlUrlSafety.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/FlowScenePainter.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSvgGenerator>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace muffin::mermaid::editor {
namespace {

constexpr auto kSvgNamespace = "http://www.w3.org/2000/svg";
constexpr auto kXLinkNamespace = "http://www.w3.org/1999/xlink";

struct SvgCanvas {
  QSize size;
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
    // Every family exposes one base extent (sceneBounds, or sequence's resolved
    // viewport); apply the diagram padding uniformly, then center it below the
    // title strip.
    const qreal padding = entry.metadata.diagramPadding;
    const QRectF extent =
        entry.scene->renderBounds().adjusted(-padding, -padding, padding, padding);
    canvas.sceneOffset = QPointF(
        (canvas.size.width() - extent.width()) / 2.0 - extent.left(),
        titleHeight - extent.top());
  }
  return canvas;
}

void paintEntry(const MermaidRenderEntry& entry, const SvgCanvas& canvas,
                QPainter& painter) {
  painter.save();
  painter.translate(canvas.sceneOffset);
  if (entry.scene)
    entry.scene->paint(painter, MermaidPaintOptions{});
  painter.restore();
  paintMermaidTitle(entry.metadata, painter,
                    QRectF(0.0, 0.0, canvas.size.width(),
                           entry.metadata.titleHeight));
}

QVector<SvgInteraction> interactions(const MermaidRenderEntry& entry,
                                     const SvgCanvas& canvas) {
  QVector<SvgInteraction> result;
  if (const auto* flow = dynamic_cast<const flowscene::FlowScene*>(entry.scene.get())) {
    for (const flowscene::FlowSceneNode& node : flow->nodes) {
      const QString href = isSafeUrl(node.link, false) ? node.link : QString();
      if (href.isEmpty() && node.tooltip.isEmpty()) continue;
      result.append({QRectF(node.cx - node.width / 2.0,
                            node.cy - node.height / 2.0,
                            node.width, node.height)
                         .translated(canvas.sceneOffset),
                     href, node.tooltip});
    }
  } else if (const auto* sequence =
                 dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
             sequence && sequence->forceMenus) {
    for (const sequence::SequenceSceneMenu& menu : sequence->menus) {
      for (const sequence::SequenceSceneMenuItem& item : menu.items) {
        if (!isSafeUrl(item.link, false)) continue;
        result.append({item.hitRect.translated(canvas.sceneOffset), item.link,
                       item.label});
      }
    }
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
                        qsizetype instanceIndex) {
  QXmlStreamReader reader(generated);
  QByteArray output;
  QXmlStreamWriter writer(&output);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);

  const QString rootId = svgRootId(entry, instanceIndex);
  const QString titleId = rootId + QStringLiteral("-title");
  const QString descriptionId = rootId + QStringLiteral("-desc");
  const QString accessibleTitle = entry.metadata.accessibleName();
  const QString accessibleDescription =
      entry.metadata.accessibleDescription.trimmed();
  const QVector<SvgInteraction> svgInteractions = interactions(entry, canvas);
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
          writer.writeAttribute(QStringLiteral("class"),
                                QStringLiteral("mfn-mermaid ") +
                                    entry.metadata.cssClass);
          writer.writeAttribute(QStringLiteral("viewBox"),
                                QStringLiteral("0 0 %1 %2")
                                    .arg(canvas.size.width())
                                    .arg(canvas.size.height()));
          if (entry.metadata.svgUseMaxWidth) {
            writer.writeAttribute(QStringLiteral("width"), QStringLiteral("100%"));
            writer.writeAttribute(
                QStringLiteral("style"),
                QStringLiteral("max-width: %1px;").arg(canvas.size.width()));
          } else {
            writer.writeAttribute(QStringLiteral("width"),
                                  QString::number(canvas.size.width()));
            writer.writeAttribute(QStringLiteral("height"),
                                  QString::number(canvas.size.height()));
          }
          writer.writeAttribute(QStringLiteral("role"),
                                QStringLiteral("graphics-document document"));
          if (!entry.metadata.roleDescription.isEmpty())
            writer.writeAttribute(QStringLiteral("aria-roledescription"),
                                  entry.metadata.roleDescription);
          writer.writeAttribute(QStringLiteral("aria-labelledby"), titleId);
          if (!accessibleDescription.isEmpty())
            writer.writeAttribute(QStringLiteral("aria-describedby"),
                                  descriptionId);
          writer.writeAttribute(QStringLiteral("data-diagram-type"),
                                entry.metadata.diagramType);
          writer.writeStartElement(QStringLiteral("title"));
          writer.writeAttribute(QStringLiteral("id"), titleId);
          writer.writeCharacters(accessibleTitle);
          writer.writeEndElement();
          if (!accessibleDescription.isEmpty()) {
            writer.writeStartElement(QStringLiteral("desc"));
            writer.writeAttribute(QStringLiteral("id"), descriptionId);
            writer.writeCharacters(accessibleDescription);
            writer.writeEndElement();
          }
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
  if (entry.status != MermaidRenderStatus::Ready || !entry.scene)
    return {};

  const SvgCanvas canvas = svgCanvas(entry);
  QByteArray generated;
  QBuffer buffer(&generated);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  QSvgGenerator generator;
  generator.setOutputDevice(&buffer);
  generator.setSize(canvas.size);
  generator.setViewBox(QRectF(QPointF(0.0, 0.0), QSizeF(canvas.size)));
  generator.setResolution(96);
  QPainter painter;
  if (!painter.begin(&generator)) return {};
  paintEntry(entry, canvas, painter);
  painter.end();
  buffer.close();
  return normalizeSvg(generated, entry, canvas, instanceIndex);
}

}  // namespace muffin::mermaid::editor
