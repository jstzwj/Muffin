#include "render/ImageDecoder.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>

#include <webp/decode.h>

#include <avif/avif.h>

namespace muffin::image_decoder {

namespace {

bool isWebp(const QByteArray& data) {
  // RIFF....WEBP
  return data.size() >= 12 && data.startsWith("RIFF") && data.mid(8, 4) == "WEBP";
}

bool isAvif(const QByteArray& data) {
  // ISO Base Media File Format: offset 4-7 == "ftyp"
  if (data.size() < 12) return false;
  if (data.mid(4, 4) != "ftyp") return false;
  // Check major brand or compatible brands for AVIF signatures
  const QByteArray brand = data.mid(8, 4);
  if (brand == "avif" || brand == "avis" || brand == "mif1" || brand == "msf1"
      || brand == "heic" || brand == "heix") {
    return true;
  }
  // Scan compatible brands (starting at offset 16, each 4 bytes)
  for (int i = 16; i + 4 <= data.size(); i += 4) {
    const QByteArray b = data.mid(i, 4);
    if (b == "avif" || b == "avis" || b == "mif1" || b == "msf1") return true;
  }
  return false;
}

bool isSvg(const QByteArray& data) {
  // SVG is an XML format. Look for "<svg" tag in the first 1 KB.
  if (data.size() < 4) return false;
  const QByteArray header = data.left(1024);
  return header.contains("<svg");
}

QImage decodeWebp(const QByteArray& data) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.constData());
  const size_t size = static_cast<size_t>(data.size());

  int width = 0, height = 0;
  if (!WebPGetInfo(ptr, size, &width, &height)) {
    return QImage();
  }

  uint8_t* rgba = WebPDecodeRGBA(ptr, size, &width, &height);
  if (!rgba) {
    return QImage();
  }

  // Wrap the decoded pixels in a QImage, then deep-copy so we can free the libwebp buffer.
  const int bytesPerLine = width * 4;
  QImage img(rgba, width, height, bytesPerLine, QImage::Format_RGBA8888);
  QImage copy = img.copy();
  WebPFree(rgba);
  return copy;
}

QImage decodeAvif(const QByteArray& data) {
  avifDecoder* decoder = avifDecoderCreate();
  if (!decoder) {
    return QImage();
  }

  avifResult result = avifDecoderSetIOMemory(decoder,
      reinterpret_cast<const uint8_t*>(data.constData()),
      static_cast<size_t>(data.size()));
  if (result != AVIF_RESULT_OK) {
    avifDecoderDestroy(decoder);
    return QImage();
  }

  result = avifDecoderParse(decoder);
  if (result != AVIF_RESULT_OK) {
    avifDecoderDestroy(decoder);
    return QImage();
  }

  result = avifDecoderNextImage(decoder);
  if (result != AVIF_RESULT_OK) {
    avifDecoderDestroy(decoder);
    return QImage();
  }

  const avifImage* avifImg = decoder->image;
  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, avifImg);
  rgb.format = AVIF_RGB_FORMAT_RGBA;

  result = avifRGBImageAllocatePixels(&rgb);
  if (result != AVIF_RESULT_OK) {
    avifDecoderDestroy(decoder);
    return QImage();
  }

  result = avifImageYUVToRGB(avifImg, &rgb);
  if (result != AVIF_RESULT_OK) {
    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return QImage();
  }

  QImage img(rgb.pixels, static_cast<int>(rgb.width), static_cast<int>(rgb.height),
             static_cast<int>(rgb.rowBytes), QImage::Format_RGBA8888);
  QImage copy = img.copy();

  avifRGBImageFreePixels(&rgb);
  avifDecoderDestroy(decoder);
  return copy;
}

QImage decodeSvg(const QByteArray& data) {
  QSvgRenderer renderer(data);
  if (!renderer.isValid()) {
    return QImage();
  }

  QSize svgSize = renderer.defaultSize();
  if (svgSize.isEmpty()) {
    svgSize = QSize(150, 150);
  }

  // Cap to prevent unreasonably large rasterization
  static constexpr int kMaxSvgDimension = 2048;
  if (svgSize.width() > kMaxSvgDimension || svgSize.height() > kMaxSvgDimension) {
    svgSize.scale(kMaxSvgDimension, kMaxSvgDimension, Qt::KeepAspectRatio);
  }

  // Apply device pixel ratio for crisp rendering on HiDPI displays
  const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : qreal(1.0);
  const int pixelW = static_cast<int>(std::ceil(svgSize.width() * dpr));
  const int pixelH = static_cast<int>(std::ceil(svgSize.height() * dpr));

  QImage image(pixelW, pixelH, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  image.setDevicePixelRatio(dpr);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter);

  return image;
}

}  // namespace

QImage decodeFallback(const QByteArray& data) {
  if (data.isEmpty()) {
    return QImage();
  }
  if (isWebp(data)) {
    return decodeWebp(data);
  }
  if (isAvif(data)) {
    return decodeAvif(data);
  }
  if (isSvg(data)) {
    return decodeSvg(data);
  }
  return QImage();
}

QImage decodeFileFallback(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QImage();
  }
  const QByteArray data = file.readAll();
  file.close();
  return decodeFallback(data);
}

QSize detectSize(const QString& filePath) {
  // Try QImageReader first (covers PNG, JPG, GIF, BMP, etc.)
  QImageReader reader(filePath);
  if (const QSize s = reader.size(); s.isValid()) {
    return s;
  }

  // Try SVG via QSvgRenderer
  QSvgRenderer renderer(filePath);
  if (renderer.isValid()) {
    return renderer.defaultSize();
  }

  return {};
}

}  // namespace muffin::image_decoder
