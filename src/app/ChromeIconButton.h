#pragma once

#include <QFile>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QSvgRenderer>
#include <QToolButton>

namespace muffin {

// Native tool-button labels can shift their icon by a pixel when checked or
// pressed. Paint the panel with the active style, but center the artwork in a
// stable rectangle for every state, font and display scale.
class ChromeIconButton : public QToolButton {
public:
  explicit ChromeIconButton(QWidget* parent = nullptr, int extent = 24) : QToolButton(parent), extent_(extent) {
    setFixedSize(extent, extent);
  }

  // QSS min-width/min-height can replace QWidget's fixed minimum during polish.
  // Keep the layout's preferred target square even after a theme/style change.
  QSize sizeHint() const override { return QSize(extent_, extent_); }
  QSize minimumSizeHint() const override { return sizeHint(); }

  void setSvgIcon(const QString& path) {
    QFile file(path);
    svg_ = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    setIcon(QIcon(path));
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const QIcon icon = option.icon;
    option.icon = QIcon();
    option.text.clear();
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    const QSize size = iconSize().boundedTo(this->size());
    const QRectF target((width() - size.width()) / 2.0, (height() - size.height()) / 2.0, size.width(), size.height());
    if (!svg_.isEmpty()) {
      QByteArray svg = svg_;
      svg.replace("#000000", palette().color(QPalette::ButtonText).name(QColor::HexRgb).toUtf8());
      QSvgRenderer renderer(svg);
      painter.setRenderHint(QPainter::Antialiasing);
      renderer.render(&painter, target);
    } else {
      const QIcon::Mode mode = !isEnabled() ? QIcon::Disabled : underMouse() ? QIcon::Active : QIcon::Normal;
      icon.paint(&painter, target.toRect(), Qt::AlignCenter, mode, isChecked() ? QIcon::On : QIcon::Off);
    }
  }

private:
  QByteArray svg_;
  int extent_;
};

} // namespace muffin
