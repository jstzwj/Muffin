#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

namespace muffin::image_decoder {

/// Try to decode image data that Qt couldn't handle.
/// Detects format by magic bytes and delegates to libwebp, libavif, or QSvgRenderer.
/// Returns a null QImage if no fallback decoder matched or decoding failed.
QImage decodeFallback(const QByteArray& data);

/// Read a local file and try decodeFallback on its contents.
/// Returns a null QImage if the file can't be read or decoding failed.
QImage decodeFileFallback(const QString& filePath);

/// Detect the natural size of a local image file.
/// Tries QImageReader first, then QSvgRenderer for SVG files.
/// Returns an invalid QSize if detection fails.
QSize detectSize(const QString& filePath);

}  // namespace muffin::image_decoder
