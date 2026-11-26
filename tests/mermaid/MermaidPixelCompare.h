#pragma once

// Compares a natively rendered diagram with the PNG that real Mermaid produced
// for the same source. Shared by the pixel oracles that were added after the
// per-family ones, which each carry their own copy of these two measures.

#include <QColor>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mermaid_pixel {

[[noreturn]] inline void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

inline void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}

// A pixel counts as ink from this alpha upwards; below it both sides are
// treated as background.
constexpr int kInkAlpha = 32;

// Overlap of the ink of both images: 1 when the same pixels are painted, 0
// when none are. A shape that moved by its own width scores close to 0.
inline qreal alphaIou(const QImage& a, const QImage& b) {
  int intersection = 0, united = 0;
  for (int y = 0; y < b.height(); ++y) {
    for (int x = 0; x < b.width(); ++x) {
      const bool aa = a.pixelColor(x, y).alpha() >= kInkAlpha;
      const bool bb = b.pixelColor(x, y).alpha() >= kInkAlpha;
      intersection += aa && bb;
      united += aa || bb;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}

// 1 minus the mean channel difference over the ink of either image. The
// difference of one pixel is the sum of its premultiplied channel distances,
// so 0 means identical colours and 1 a full swing on every channel.
inline qreal rgbaSimilarity(const QImage& a, const QImage& b) {
  qreal difference = 0.0;
  long long count = 0;
  const auto premul = [](int channel, int alpha) { return channel * alpha / 255; };
  for (int y = 0; y < b.height(); ++y) {
    for (int x = 0; x < b.width(); ++x) {
      const QColor x1 = a.pixelColor(x, y), x2 = b.pixelColor(x, y);
      if (x1.alpha() < kInkAlpha && x2.alpha() < kInkAlpha)
        continue;
      ++count;
      difference += std::abs(premul(x1.red(), x1.alpha()) - premul(x2.red(), x2.alpha()));
      difference += std::abs(premul(x1.green(), x1.alpha()) - premul(x2.green(), x2.alpha()));
      difference += std::abs(premul(x1.blue(), x1.alpha()) - premul(x2.blue(), x2.alpha()));
      difference += std::abs(x1.alpha() - x2.alpha());
    }
  }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}

// The largest per-pixel channel distance, as a fraction of full scale, over the
// ink of both images. The mean in rgbaSimilarity hides a few very wrong pixels
// (a wrong stroke colour on a short edge) behind many right ones; this does not.
inline qreal worstPixelDifference(const QImage& a, const QImage& b) {
  int worst = 0;
  const auto premul = [](int channel, int alpha) { return channel * alpha / 255; };
  for (int y = 0; y < b.height(); ++y) {
    for (int x = 0; x < b.width(); ++x) {
      const QColor x1 = a.pixelColor(x, y), x2 = b.pixelColor(x, y);
      if (x1.alpha() < kInkAlpha && x2.alpha() < kInkAlpha)
        continue;
      for (const int distance : {std::abs(premul(x1.red(), x1.alpha()) - premul(x2.red(), x2.alpha())),
                                 std::abs(premul(x1.green(), x1.alpha()) - premul(x2.green(), x2.alpha())),
                                 std::abs(premul(x1.blue(), x1.alpha()) - premul(x2.blue(), x2.alpha())),
                                 std::abs(x1.alpha() - x2.alpha())})
        worst = qMax(worst, distance);
    }
  }
  return worst / 255.0;
}

// 1 minus the mean channel difference over every pixel of one square tile, at
// the worst tile of a grid over the image. The mean over a whole image lets a
// region that is uniformly a little wrong — a table row filled with the other
// row color, a label background a shade off — hide behind all that is right;
// the worst tile does not.
inline qreal worstTileSimilarity(const QImage& a, const QImage& b, int tileSize = 32) {
  const auto premul = [](int channel, int alpha) { return channel * alpha / 255; };
  qreal worst = 1.0;
  for (int top = 0; top < b.height(); top += tileSize) {
    for (int left = 0; left < b.width(); left += tileSize) {
      long long difference = 0;
      long long count = 0;
      const int bottom = std::min(top + tileSize, b.height()), right = std::min(left + tileSize, b.width());
      for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
          const QColor x1 = a.pixelColor(x, y), x2 = b.pixelColor(x, y);
          difference += std::abs(premul(x1.red(), x1.alpha()) - premul(x2.red(), x2.alpha()));
          difference += std::abs(premul(x1.green(), x1.alpha()) - premul(x2.green(), x2.alpha()));
          difference += std::abs(premul(x1.blue(), x1.alpha()) - premul(x2.blue(), x2.alpha()));
          difference += std::abs(x1.alpha() - x2.alpha());
          ++count;
        }
      }
      worst = std::min(worst, count ? 1.0 - qreal(difference) / (count * 1020.0) : 1.0);
    }
  }
  return worst;
}

// Decodes the data URL the native renderer returns.
inline QImage decodeDataUrl(const QString& url) {
  QImage image;
  const qsizetype comma = url.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

struct Measures {
  qreal iou = 0.0;    // the overlap of the ink of both images
  qreal rgba = 0.0;   // the mean color similarity over the ink
  qreal worst = 0.0;  // the largest per-pixel channel distance over the ink
  qreal tile = 1.0;   // the mean color similarity of the worst 32 px tile
  qreal area = 0.0;   // the largest area difference of a dominant color
};

QHash<QRgb, long long> colorCounts(const QImage& image) {
  QHash<QRgb, long long> counts;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      ++counts[image.pixel(x, y)];
  return counts;
}

// The largest difference, as a share of the image, between the area a color
// covers in both images, over every color that covers a notable part of the
// reference. The antialiasing of glyphs and a fraction of a pixel of shift
// barely move a color's area; a table row filled with the other row color, or
// a label with another background, moves two of them by the area of that part.
inline qreal dominantColorAreaDifference(const QImage& native, const QImage& reference, qreal minimumShare = 0.005) {
  const QHash<QRgb, long long> nativeColors = colorCounts(native);
  const QHash<QRgb, long long> referenceColors = colorCounts(reference);
  const qreal total = qreal(reference.width()) * reference.height();
  qreal worst = 0.0;
  for (auto it = referenceColors.cbegin(); it != referenceColors.cend(); ++it) {
    const qreal share = it.value() / total;
    if (share >= minimumShare)
      worst = std::max(worst, std::abs(share - nativeColors.value(it.key()) / total));
  }
  return worst;
}

inline Measures measure(const QImage& native, const QImage& reference) {
  Measures measures;
  measures.iou = alphaIou(native, reference);
  measures.rgba = rgbaSimilarity(native, reference);
  measures.worst = worstPixelDifference(native, reference);
  measures.tile = worstTileSimilarity(native, reference);
  measures.area = dominantColorAreaDifference(native, reference);
  return measures;
}

} // namespace mermaid_pixel
