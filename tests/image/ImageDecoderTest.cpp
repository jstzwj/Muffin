#include "render/ImageDecoder.h"

#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QSize>
#include <cstdlib>

// Round-trips tiny embedded PNG/JPEG through our bundled libpng/libjpeg decoders
// (independent of Qt imageformat plugins) to verify local image display no longer
// depends on qjpeg/qpng.

namespace {
static const unsigned char kPng[] = {
    137,80,78,71,13,10,26,10,0,0,0,13,
    73,72,68,82,0,0,0,4,0,0,0,3,
    8,2,0,0,0,59,150,57,145,0,0,0,
    30,73,68,65,84,120,156,99,252,207,192,192,
    45,34,199,0,6,140,92,48,22,3,3,3,
    19,156,245,237,245,35,0,57,137,4,64,150,
    16,83,158,0,0,0,0,73,69,78,68,174,
    66,96,130,
};
static const int kPng_size = 87;
static const unsigned char kJpeg[] = {
    255,216,255,224,0,16,74,70,73,70,0,1,
    1,0,0,1,0,1,0,0,255,219,0,67,
    0,3,2,2,3,2,2,3,3,3,3,4,
    3,3,4,5,8,5,5,4,4,5,10,7,
    7,6,8,12,10,12,12,11,10,11,11,13,
    14,18,16,13,14,17,14,11,11,16,22,16,
    17,19,20,21,21,21,12,15,23,24,22,20,
    24,18,20,21,20,255,219,0,67,1,3,4,
    4,5,4,5,9,5,5,9,20,13,11,13,
    20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,20,20,20,20,20,20,20,20,20,20,
    20,20,255,192,0,17,8,0,3,0,4,3,
    1,34,0,2,17,1,3,17,1,255,196,0,
    31,0,0,1,5,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,1,2,3,4,5,
    6,7,8,9,10,11,255,196,0,181,16,0,
    2,1,3,3,2,4,3,5,5,4,4,0,
    0,1,125,1,2,3,0,4,17,5,18,33,
    49,65,6,19,81,97,7,34,113,20,50,129,
    145,161,8,35,66,177,193,21,82,209,240,36,
    51,98,114,130,9,10,22,23,24,25,26,37,
    38,39,40,41,42,52,53,54,55,56,57,58,
    67,68,69,70,71,72,73,74,83,84,85,86,
    87,88,89,90,99,100,101,102,103,104,105,106,
    115,116,117,118,119,120,121,122,131,132,133,134,
    135,136,137,138,146,147,148,149,150,151,152,153,
    154,162,163,164,165,166,167,168,169,170,178,179,
    180,181,182,183,184,185,186,194,195,196,197,198,
    199,200,201,202,210,211,212,213,214,215,216,217,
    218,225,226,227,228,229,230,231,232,233,234,241,
    242,243,244,245,246,247,248,249,250,255,196,0,
    31,1,0,3,1,1,1,1,1,1,1,1,
    1,0,0,0,0,0,0,1,2,3,4,5,
    6,7,8,9,10,11,255,196,0,181,17,0,
    2,1,2,4,4,3,4,7,5,4,4,0,
    1,2,119,0,1,2,3,17,4,5,33,49,
    6,18,65,81,7,97,113,19,34,50,129,8,
    20,66,145,161,177,193,9,35,51,82,240,21,
    98,114,209,10,22,36,52,225,37,241,23,24,
    25,26,38,39,40,41,42,53,54,55,56,57,
    58,67,68,69,70,71,72,73,74,83,84,85,
    86,87,88,89,90,99,100,101,102,103,104,105,
    106,115,116,117,118,119,120,121,122,130,131,132,
    133,134,135,136,137,138,146,147,148,149,150,151,
    152,153,154,162,163,164,165,166,167,168,169,170,
    178,179,180,181,182,183,184,185,186,194,195,196,
    197,198,199,200,201,202,210,211,212,213,214,215,
    216,217,218,226,227,228,229,230,231,232,233,234,
    242,243,244,245,246,247,248,249,250,255,218,0,
    12,3,1,0,2,17,3,17,0,63,0,240,
    31,217,123,224,215,131,254,39,120,15,83,213,
    252,77,164,182,171,168,174,171,44,34,225,238,
    231,140,236,242,162,108,16,142,1,59,157,142,
    79,60,209,69,21,252,203,196,249,206,103,134,
    206,113,52,104,226,106,70,42,86,73,78,73,
    45,22,201,59,35,248,199,139,115,236,222,134,
    123,139,167,75,23,82,49,82,118,74,114,75,
    238,76,255,217,
};
static const int kJpeg_size = 712;
void require(bool cond, const QString& msg){ if(!cond){ qCritical().noquote()<<msg; std::exit(1);} }
} // namespace

int main(int argc,char**argv){
  QCoreApplication app(argc,argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  const QImage png = muffin::image_decoder::decodeFallback(QByteArray::fromRawData(reinterpret_cast<const char*>(kPng), kPng_size));
  require(!png.isNull(), QStringLiteral("PNG decode failed"));
  require(png.width()==4 && png.height()==3, QStringLiteral("PNG size mismatch: %1x%2").arg(png.width()).arg(png.height()));
  const QImage jpg = muffin::image_decoder::decodeFallback(QByteArray::fromRawData(reinterpret_cast<const char*>(kJpeg), kJpeg_size));
  require(!jpg.isNull(), QStringLiteral("JPEG decode failed (libjpeg not linked?)"));
  require(jpg.width()==4 && jpg.height()==3, QStringLiteral("JPEG size mismatch: %1x%2").arg(jpg.width()).arg(jpg.height()));

  // --- data: URI (RFC 2397): inline base64 / percent-encoded images ---
  const QByteArray pngRaw = QByteArray::fromRawData(reinterpret_cast<const char*>(kPng), kPng_size);
  const QImage fromBase64 = muffin::image_decoder::decodeDataUri(
      QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngRaw.toBase64()));
  require(!fromBase64.isNull(), QStringLiteral("base64 data URI decode failed"));
  require(fromBase64.width()==4 && fromBase64.height()==3,
          QStringLiteral("base64 data URI size mismatch: %1x%2").arg(fromBase64.width()).arg(fromBase64.height()));

  // Percent-encoded payload (the standard form for raw bytes / inline SVG).
  const QImage fromPercent = muffin::image_decoder::decodeDataUri(
      QStringLiteral("data:image/png,") + QString::fromLatin1(pngRaw.toPercentEncoding()));
  require(!fromPercent.isNull(), QStringLiteral("percent-encoded data URI decode failed"));
  require(fromPercent.width()==4 && fromPercent.height()==3,
          QStringLiteral("percent-encoded data URI size mismatch: %1x%2").arg(fromPercent.width()).arg(fromPercent.height()));

  // Scheme and the ;base64 flag are case-insensitive; a wrong media type still
  // decodes because the format is sniffed from magic bytes, not the type label.
  const QImage fromUpper = muffin::image_decoder::decodeDataUri(
      QStringLiteral("DATA:image/jpeg;BASE64,") + QString::fromLatin1(pngRaw.toBase64()));
  require(!fromUpper.isNull() && fromUpper.width()==4 && fromUpper.height()==3,
          QStringLiteral("uppercase-scheme / mistyped-media data URI should still decode"));

  // Malformed inputs must return null, never crash.
  require(muffin::image_decoder::decodeDataUri(QStringLiteral("data:image/png;base64")).isNull(),
          QStringLiteral("data URI without a comma should be null"));
  require(muffin::image_decoder::decodeDataUri(QStringLiteral("http://example.com/a.png")).isNull(),
          QStringLiteral("non-data scheme should be null"));
  require(muffin::image_decoder::decodeDataUri(QStringLiteral("data:image/png;base64,AAAA")).isNull(),
          QStringLiteral("valid base64 of non-image bytes should be null"));
  return 0;
}
