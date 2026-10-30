#pragma once

#include <QAbstractSpinBox>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSpinBox>

namespace muffin {

// Keep Qt's editing, stepping and accessibility; paint only the two chevrons.
// The stylesheet places the buttons, and those same rectangles drive painting
// and Qt's hit testing at every display scale.
template <typename Base> class PreferencesSpinBox final : public Base {
public:
  explicit PreferencesSpinBox(QWidget* parent = nullptr) : Base(parent) {
    this->setProperty("preferencesStepButtons", true);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Base::paintEvent(event);
    if (this->buttonSymbols() != QAbstractSpinBox::UpDownArrows)
      return;

    QStyleOptionSpinBox option;
    this->initStyleOption(&option);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto chevron = [&](QStyle::SubControl control, QAbstractSpinBox::StepEnabledFlag step) {
      const QRect rect = this->style()->subControlRect(QStyle::CC_SpinBox, &option, control, this);
      if (rect.isEmpty())
        return;
      const bool enabled = (option.state & QStyle::State_Enabled) && (option.stepEnabled & step);
      const bool hovered = (option.state & QStyle::State_MouseOver) && (option.activeSubControls & control);
      const QColor ink = option.palette.color(enabled ? QPalette::Active : QPalette::Disabled,
                                              enabled && !hovered ? QPalette::PlaceholderText : QPalette::ButtonText);
      painter.setPen(QPen(ink, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const QPointF center = QRectF(rect).center();
      const qreal direction = control == QStyle::SC_SpinBoxUp ? -1.0 : 1.0;
      painter.drawPolyline(QPolygonF{center + QPointF(-3.0, -direction * 1.5), center + QPointF(0, direction * 1.5),
                                     center + QPointF(3.0, -direction * 1.5)});
    };
    chevron(QStyle::SC_SpinBoxUp, QAbstractSpinBox::StepUpEnabled);
    chevron(QStyle::SC_SpinBoxDown, QAbstractSpinBox::StepDownEnabled);
  }
};

} // namespace muffin
