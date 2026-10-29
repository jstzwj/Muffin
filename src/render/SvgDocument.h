#pragma once

#include <QByteArray>
#include <QImage>
#include <QSizeF>

namespace muffin::svg {
// Lower unsupported shorthand to SVG filter primitives supported by Qt. Source
// files remain untouched; invalid XML is left to the renderer's error path.
QByteArray qtCompatible(const QByteArray& source);
QImage rasterize(const QByteArray& source, QSizeF logicalSize = {}, qreal devicePixelRatio = 1);
} // namespace muffin::svg
