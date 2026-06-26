#include "render/Blur.h"

#include <QtGlobal>

#include <algorithm>
#include <vector>

namespace muffin {

namespace {

// One separable box-blur pass (horizontal + vertical) using a running sum over a
// window of 2·radius+1, with edge clamping. O(width·height) per pass.
void blurPass(QImage& image, int radius) {
  if (radius <= 0 || image.width() < 1 || image.height() < 1) { return; }
  const int w = image.width();
  const int h = image.height();
  const int window = 2 * radius + 1;

  // Horizontal.
  std::vector<QRgb> row(static_cast<size_t>(w));
  for (int y = 0; y < h; ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    int sr = 0, sg = 0, sb = 0, sa = 0;
    for (int x = -radius; x <= radius; ++x) {
      const QRgb p = line[qBound(0, x, w - 1)];
      sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
    }
    for (int x = 0; x < w; ++x) {
      row[static_cast<size_t>(x)] = qRgba(sr / window, sg / window, sb / window, sa / window);
      const QRgb out = line[qBound(0, x - radius, w - 1)];
      const QRgb in = line[qBound(0, x + radius + 1, w - 1)];
      sr += qRed(in) - qRed(out); sg += qGreen(in) - qGreen(out);
      sb += qBlue(in) - qBlue(out); sa += qAlpha(in) - qAlpha(out);
    }
    std::copy(row.begin(), row.end(), line);
  }

  // Vertical (column gather + scatter).
  std::vector<QRgb> col(static_cast<size_t>(h));
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) {
      col[static_cast<size_t>(y)] = reinterpret_cast<QRgb*>(image.scanLine(y))[x];
    }
    int sr = 0, sg = 0, sb = 0, sa = 0;
    for (int y = -radius; y <= radius; ++y) {
      const QRgb p = col[static_cast<size_t>(qBound(0, y, h - 1))];
      sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
    }
    for (int y = 0; y < h; ++y) {
      reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgba(sr / window, sg / window, sb / window, sa / window);
      const QRgb out = col[static_cast<size_t>(qBound(0, y - radius, h - 1))];
      const QRgb in = col[static_cast<size_t>(qBound(0, y + radius + 1, h - 1))];
      sr += qRed(in) - qRed(out); sg += qGreen(in) - qGreen(out);
      sb += qBlue(in) - qBlue(out); sa += qAlpha(in) - qAlpha(out);
    }
  }
}

}  // namespace

void boxBlur(QImage& image, int radius, int passes) {
  if (radius <= 0 || passes <= 0) { return; }
  if (image.format() != QImage::Format_ARGB32_Premultiplied) { return; }  // caller converts
  for (int i = 0; i < passes; ++i) { blurPass(image, radius); }
}

QImage blurred(const QImage& src, qreal radius) {
  if (radius <= 0.0) { return src.copy(); }
  QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  boxBlur(img, qMax(1, qRound(radius)));
  return img;
}

}  // namespace muffin
