#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QCoreApplication>
#include <QWheelEvent>

namespace muffin {

class PreferencesComboBox : public QComboBox {
public:
  using QComboBox::QComboBox;

protected:
  void wheelEvent(QWheelEvent* event) override {
    // Preferences save immediately. Focus does not imply an intention to change
    // a value while scrolling the page; only an open list should handle wheels.
    if (view()->isVisible()) {
      QComboBox::wheelEvent(event);
      return;
    }
    // Route to the enclosing viewport once, preserving smooth scrolling and
    // gesture phases. Merely ignoring can leave the gesture on the control.
    for (auto* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
      if (auto* scroll = qobject_cast<QAbstractScrollArea*>(ancestor)) {
        auto* viewport = scroll->viewport();
        QWheelEvent forwarded(viewport->mapFromGlobal(event->globalPosition()), event->globalPosition(),
                              event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(),
                              event->phase(), event->inverted(), event->source(), event->pointingDevice());
        QCoreApplication::sendEvent(viewport, &forwarded);
        event->setAccepted(forwarded.isAccepted());
        return;
      }
    }
    event->ignore();
  }
};

} // namespace muffin
