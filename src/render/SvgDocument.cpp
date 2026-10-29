#include "render/SvgDocument.h"

#include <QPainter>
#include <QSet>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>
#include <algorithm>

namespace muffin::svg {
QByteArray qtCompatible(const QByteArray& source) {
  if (!source.contains("feDropShadow"))
    return source;
  // Results share one namespace per filter. Reserve authored names before
  // introducing intermediates, including forward references.
  QSet<QString> names;
  QXmlStreamReader scan(source);
  while (!scan.atEnd()) {
    scan.readNext();
    if (scan.isStartElement() && scan.attributes().hasAttribute("result"))
      names.insert(scan.attributes().value("result").toString());
  }
  if (scan.hasError())
    return source;
  int sequence = 0;
  const auto unique = [&] {
    QString result;
    do {
      result = QStringLiteral("muffin-filter-%1").arg(++sequence);
    } while (names.contains(result));
    names.insert(result);
    return result;
  };
  QByteArray output;
  QXmlStreamReader reader(source);
  QXmlStreamWriter writer(&output);
  QString previous;
  int filterDepth = -1, depth = 0;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      ++depth;
      if (reader.name() == QLatin1String("filter")) {
        filterDepth = depth;
        previous = QStringLiteral("SourceGraphic");
      } else if (filterDepth >= 0 && depth == filterDepth + 1 && reader.name() == QLatin1String("feDropShadow")) {
        const auto attributes = reader.attributes();
        const QString prefix = reader.prefix().isEmpty() ? QString() : reader.prefix().toString() + ':';
        const auto value = [&](const char* key, const QString& fallback) {
          return attributes.hasAttribute(QLatin1String(key)) ? attributes.value(QLatin1String(key)).toString()
                                                             : fallback;
        };
        const QString input = value("in", previous);
        const QString blur = unique(), offset = unique(), flood = unique(), shadow = unique();
        const QString result = value("result", unique());
        const auto primitive = [&](const char* name, std::initializer_list<QPair<QString, QString>> attrs) {
          writer.writeStartElement(prefix + QLatin1String(name));
          for (const auto& attr : attrs)
            writer.writeAttribute(attr.first, attr.second);
          writer.writeEndElement();
        };
        // Blurring RGBA followed by an 'in' composite uses precisely the
        // blurred input alpha, including opacity and earlier filter results.
        primitive("feGaussianBlur", {{"in", input}, {"stdDeviation", value("stdDeviation", "2")}, {"result", blur}});
        primitive("feOffset", {{"in", blur}, {"dx", value("dx", "2")}, {"dy", value("dy", "2")}, {"result", offset}});
        writer.writeStartElement(prefix + "feFlood");
        writer.writeAttribute("flood-color", value("flood-color", "black"));
        writer.writeAttribute("flood-opacity", value("flood-opacity", "1"));
        if (attributes.hasAttribute("style"))
          writer.writeAttribute("style", attributes.value("style").toString());
        writer.writeAttribute("result", flood);
        writer.writeEndElement();
        primitive("feComposite", {{"in", flood}, {"in2", offset}, {"operator", "in"}, {"result", shadow}});
        writer.writeStartElement(prefix + "feMerge");
        for (const auto* name : {"x", "y", "width", "height"})
          if (attributes.hasAttribute(QLatin1String(name)))
            writer.writeAttribute(QLatin1String(name), attributes.value(QLatin1String(name)).toString());
        writer.writeAttribute("result", result);
        primitive("feMergeNode", {{"in", shadow}});
        primitive("feMergeNode", {{"in", input}});
        writer.writeEndElement();
        previous = result;
        reader.skipCurrentElement();
        --depth;
        continue;
      } else if (filterDepth >= 0 && depth == filterDepth + 1 && reader.name().startsWith(QLatin1String("fe"))) {
        writer.writeStartElement(reader.qualifiedName().toString());
        writer.writeAttributes(reader.attributes());
        if (reader.attributes().hasAttribute("result"))
          previous = reader.attributes().value("result").toString();
        else {
          previous = unique();
          writer.writeAttribute("result", previous);
        }
        continue;
      }
    }
    writer.writeCurrentToken(reader);
    if (reader.isEndElement()) {
      if (depth == filterDepth)
        filterDepth = -1;
      --depth;
    }
  }
  return reader.hasError() ? source : output;
}

QImage rasterize(const QByteArray& source, QSizeF size, qreal dpr) {
  QSvgRenderer renderer(qtCompatible(source));
  if (!renderer.isValid())
    return {};
  if (size.isEmpty())
    size = renderer.defaultSize();
  if (size.isEmpty())
    size = {150, 150};
  if (!std::isfinite(dpr) || dpr <= 0)
    dpr = 1;
  QSizeF pixels = size * dpr;
  if (!std::isfinite(pixels.width()) || !std::isfinite(pixels.height()))
    return {};
  // Bound the actual allocation, rather than the logical size before DPI.
  const qreal scale = std::min<qreal>({1., 8192. / pixels.width(), 8192. / pixels.height(),
                                       std::sqrt(16. * 1024 * 1024 / (pixels.width() * pixels.height()))});
  pixels *= scale;
  QImage image(QSize(qMax(1, qCeil(pixels.width())), qMax(1, qCeil(pixels.height()))),
               QImage::Format_ARGB32_Premultiplied);
  if (image.isNull())
    return {};
  image.setDevicePixelRatio(dpr * scale);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter, QRectF(QPointF(), size));
  return image;
}
} // namespace muffin::svg
