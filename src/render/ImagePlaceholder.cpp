#include "render/ImagePlaceholder.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

#include <cmath>

namespace muffin::image_placeholder {
namespace {

QImage renderSvg(const QString& resourcePath, QSizeF logicalSize) {
  QFile svgFile(resourcePath);
  if (!svgFile.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray svgData = svgFile.readAll();
  QSvgRenderer renderer(svgData);
  if (!renderer.isValid()) {
    return {};
  }

  const qreal dpr = qApp->devicePixelRatio();
  const int w = static_cast<int>(std::ceil(logicalSize.width() * dpr));
  const int h = static_cast<int>(std::ceil(logicalSize.height() * dpr));
  if (w <= 0 || h <= 0) {
    return {};
  }

  QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  renderer.render(&p);
  image.setDevicePixelRatio(dpr);
  return image;
}

}  // namespace

QImage loading(QSizeF logicalSize) {
  return renderSvg(QStringLiteral(":/icons/image/image-placeholder.svg"), logicalSize);
}

QImage broken(QSizeF logicalSize) {
  return renderSvg(QStringLiteral(":/icons/image/image-broken.svg"), logicalSize);
}

}  // namespace muffin::image_placeholder
