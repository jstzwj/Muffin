#include "render/ImageDecoder.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QStringView>
#include <QSvgRenderer>

#include <webp/decode.h>

#include <avif/avif.h>

#include <jpeglib.h>
#include <png.h>

#include <csetjmp>
#include <cstring>
#include <vector>

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

bool isPng(const QByteArray& data) {
  // PNG signature: 89 50 4E 47 0D 0A 1A 0A
  static constexpr unsigned char kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return data.size() >= 8 && std::memcmp(data.constData(), kSig, 8) == 0;
}

QImage decodePng(const QByteArray& data) {
  // libpng's simplified png_image API: read from memory into an 8-bit RGBA buffer,
  // then wrap (and detach) it in a QImage. This is independent of Qt's qpng plugin.
  png_image image;
  std::memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_memory(&image, data.constData(),
                                       static_cast<png_alloc_size_t>(data.size())) == 0) {
    return QImage();
  }
  image.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> buffer(PNG_IMAGE_SIZE(image));
  if (png_image_finish_read(&image, nullptr, buffer.data(), 0, nullptr) == 0) {
    png_image_free(&image);
    return QImage();
  }
  const int width = static_cast<int>(image.width);
  const int height = static_cast<int>(image.height);
  png_image_free(&image);
  QImage img(buffer.data(), width, height, width * 4, QImage::Format_RGBA8888);
  return img.copy();
}

bool isJpeg(const QByteArray& data) {
  // JPEG SOI marker followed by an APPn/define marker: FF D8 FF
  return data.size() >= 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 &&
         (unsigned char)data[2] == 0xFF;
}

// libjpeg reports fatal errors by calling error_exit, which must not return. longjmp
// back to the decoder so it can bail and tear down cleanly.
struct JpegErrorHandler {
  jpeg_error_mgr base;
  jmp_buf setjmp_buffer;
};

void onJpegError(j_common_ptr cinfo) {
  auto* handler = reinterpret_cast<JpegErrorHandler*>(cinfo->err);
  longjmp(handler->setjmp_buffer, 1);
}

QImage decodeJpeg(const QByteArray& data) {
  jpeg_decompress_struct cinfo;
  JpegErrorHandler err;
  cinfo.err = jpeg_std_error(&err.base);
  err.base.error_exit = onJpegError;
  jpeg_create_decompress(&cinfo);

  if (setjmp(err.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    return QImage();
  }

  jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(data.constData()),
               static_cast<unsigned long>(data.size()));
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    return QImage();
  }
  // Force RGB output so output_components is always 3 (covers grayscale/CMYK inputs).
  cinfo.out_color_space = JCS_RGB;
  if (!jpeg_start_decompress(&cinfo)) {
    jpeg_destroy_decompress(&cinfo);
    return QImage();
  }

  const int width = static_cast<int>(cinfo.output_width);
  const int height = static_cast<int>(cinfo.output_height);
  if (width <= 0 || height <= 0 || cinfo.output_components != 3) {
    jpeg_destroy_decompress(&cinfo);
    return QImage();
  }

  // Decode into a tightly-packed RGB buffer, then wrap (and detach) in a QImage.
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * 3 * height);
  while (cinfo.output_scanline < static_cast<JDIMENSION>(height)) {
    unsigned char* row =
        pixels.data() + static_cast<size_t>(cinfo.output_scanline) * width * 3;
    jpeg_read_scanlines(&cinfo, &row, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  QImage img(pixels.data(), width, height, width * 3, QImage::Format_RGB888);
  return img.copy();
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
  if (isPng(data)) {
    return decodePng(data);
  }
  if (isJpeg(data)) {
    return decodeJpeg(data);
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

// Decode an inline `data:` URI (RFC 2397): data:[<mediatype>][;base64],<data>.
// Only the "data" scheme and an explicit ";base64" flag matter — the media type
// is ignored because decodeFallback() sniffs format from magic bytes, so a
// missing or wrong type still decodes. Non-base64 payloads are percent-decoded
// (the standard form for inline SVG and raw image bytes).
QImage decodeDataUri(const QString& uri) {
  const int comma = uri.indexOf(QLatin1Char(','));
  if (comma < 0) {
    return QImage();
  }
  const QStringView head = QStringView(uri).left(comma);
  const int colon = head.indexOf(QLatin1Char(':'));
  if (colon < 0 ||
      head.left(colon).trimmed().compare(QLatin1String("data"), Qt::CaseInsensitive) != 0) {
    return QImage();
  }

  const QStringView body = QStringView(uri).mid(comma + 1);

  bool isBase64 = false;
  for (const QStringView token : head.split(QLatin1Char(';'))) {
    if (token.trimmed().compare(QLatin1String("base64"), Qt::CaseInsensitive) == 0) {
      isBase64 = true;
      break;
    }
  }

  QByteArray bytes;
  if (isBase64) {
    // Some emitters wrap long base64 lines with newlines/spaces, which
    // QByteArray::fromBase64 rejects — strip them first.
    QByteArray raw = body.toUtf8();
    raw.replace('\n', QByteArray()).replace('\r', QByteArray())
        .replace(' ', QByteArray()).replace('\t', QByteArray());
    bytes = QByteArray::fromBase64(raw);
  } else {
    bytes = QByteArray::fromPercentEncoding(body.toUtf8());
  }
  if (bytes.isEmpty()) {
    return QImage();
  }

  QImage image = decodeFallback(bytes);
  if (image.isNull()) {
    image.loadFromData(bytes);  // last resort for formats we don't ship (gif/bmp/tiff…)
  }
  return image;
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
