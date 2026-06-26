#include "render/Filter.h"

#include "render/Blur.h"
#include "theme/ThemeDefinition.h"

#include <QColor>
#include <QtGlobal>
#include <QtMath>

namespace muffin {

namespace {

qreal clamp01(qreal v) { return qBound(qreal(0.0), v, qreal(1.0)); }

// One-pixel colour filter: brightness × contrast, then grayscale/sepia blend, then
// hue rotation, all in linear 0..1 RGB. The pixel is premultiplied; we unpremult,
// transform, and re-premultiply. Fully-transparent pixels are left untouched.
void filterPixel(QRgb& p, qreal bright, qreal contrast, qreal gray, qreal sepia, qreal hueDeg) {
  const int a = qAlpha(p);
  if (a == 0) { return; }
  const qreal ia = a / 255.0;
  qreal r = (qRed(p) / 255.0) / ia;
  qreal g = (qGreen(p) / 255.0) / ia;
  qreal b = (qBlue(p) / 255.0) / ia;

  // brightness (scale) then contrast (about 0.5).
  r = clamp01(r * bright);
  g = clamp01(g * bright);
  b = clamp01(b * bright);
  if (qAbs(contrast - 1.0) > 1e-4) {
    r = clamp01((r - 0.5) * contrast + 0.5);
    g = clamp01((g - 0.5) * contrast + 0.5);
    b = clamp01((b - 0.5) * contrast + 0.5);
  }
  // grayscale: blend toward luma.
  if (gray > 1e-4) {
    const qreal l = 0.299 * r + 0.587 * g + 0.114 * b;
    r = r * (1.0 - gray) + l * gray;
    g = g * (1.0 - gray) + l * gray;
    b = b * (1.0 - gray) + l * gray;
  }
  // sepia: blend toward the sepia-mapped colour.
  if (sepia > 1e-4) {
    const qreal sr = clamp01(0.393 * r + 0.769 * g + 0.189 * b);
    const qreal sg = clamp01(0.349 * r + 0.686 * g + 0.168 * b);
    const qreal sb = clamp01(0.272 * r + 0.534 * g + 0.131 * b);
    r = r * (1.0 - sepia) + sr * sepia;
    g = g * (1.0 - sepia) + sg * sepia;
    b = b * (1.0 - sepia) + sb * sepia;
  }
  // hue-rotate (degrees) via HSV — rare, so the QColor round-trip cost is fine.
  if (qAbs(hueDeg) > 1e-4) {
    QColor c;
    c.setRgbF(r, g, b);
    int h, s, v, alpha;
    c.getHsv(&h, &s, &v, &alpha);
    h = (h + qRound(hueDeg)) % 360;
    if (h < 0) { h += 360; }
    c.setHsv(h, s, v);
    r = c.redF();
    g = c.greenF();
    b = c.blueF();
  }

  const int rr = qRound(clamp01(r) * a);
  const int gg = qRound(clamp01(g) * a);
  const int bb = qRound(clamp01(b) * a);
  p = qRgba(rr, gg, bb, a);  // re-premultiplied
}

}  // namespace

bool hasElementFilter(const ThemeElementPaintStyle& s) {
  return s.filterBlur > 0.0 || qAbs(s.filterBrightness - 1.0) > 1e-4 || qAbs(s.filterContrast - 1.0) > 1e-4 ||
         s.filterGrayscale > 1e-4 || s.filterSepia > 1e-4 || qAbs(s.filterHueRotateDeg) > 1e-4 ||
         qAbs(s.filterOpacity - 1.0) > 1e-4;
}

bool hasElementBackdrop(const ThemeElementPaintStyle& s) {
  return s.backdropBlur > 0.0 || qAbs(s.backdropBrightness - 1.0) > 1e-4 || qAbs(s.backdropContrast - 1.0) > 1e-4 ||
         s.backdropGrayscale > 1e-4 || s.backdropSepia > 1e-4 || qAbs(s.backdropHueRotateDeg) > 1e-4 ||
         qAbs(s.backdropOpacity - 1.0) > 1e-4;
}

// Core filter pipeline (colour matrix → blur → opacity), shared by `filter` and
// `backdrop-filter`.
void applyFilterValues(QImage& image, qreal blur, qreal bright, qreal contrast, qreal gray,
                       qreal sepia, qreal hue, qreal opacity) {
  if (image.format() != QImage::Format_ARGB32_Premultiplied) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  const bool colorOps = qAbs(bright - 1.0) > 1e-4 || qAbs(contrast - 1.0) > 1e-4 ||
                        gray > 1e-4 || sepia > 1e-4 || qAbs(hue) > 1e-4;
  if (colorOps) {
    for (int y = 0; y < image.height(); ++y) {
      auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        filterPixel(line[x], bright, contrast, gray, sepia, hue);
      }
    }
  }
  if (blur > 0.0) { boxBlur(image, qMax(1, qRound(blur))); }
  if (qAbs(opacity - 1.0) > 1e-4) {
    const qreal o = clamp01(opacity);
    for (int y = 0; y < image.height(); ++y) {
      auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const int a = qAlpha(line[x]);
        if (a == 0) { continue; }
        const int na = qRound(a * o);
        const qreal k = qreal(na) / a;  // keep the premultiplied channels in step
        line[x] = qRgba(qRound(qRed(line[x]) * k), qRound(qGreen(line[x]) * k), qRound(qBlue(line[x]) * k), na);
      }
    }
  }
}

void applyElementFilter(QImage& image, const ThemeElementPaintStyle& s) {
  applyFilterValues(image, s.filterBlur, s.filterBrightness, s.filterContrast, s.filterGrayscale,
                    s.filterSepia, s.filterHueRotateDeg, s.filterOpacity);
}

void applyElementBackdrop(QImage& image, const ThemeElementPaintStyle& s) {
  applyFilterValues(image, s.backdropBlur, s.backdropBrightness, s.backdropContrast, s.backdropGrayscale,
                    s.backdropSepia, s.backdropHueRotateDeg, s.backdropOpacity);
}

}  // namespace muffin
