#include "app/DwmPopupBorderFilter.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QWidget>

#if defined(Q_OS_WIN)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <dwmapi.h>

#  include <QOperatingSystemVersion>

#  ifndef DWMWA_BORDER_COLOR
#    define DWMWA_BORDER_COLOR 34
#  endif
#  ifndef DWMWA_COLOR_NONE
#    define DWMWA_COLOR_NONE 0xFFFFFFFE
#  endif
#endif

namespace muffin {

void muffin::DwmPopupBorderFilter::install(QApplication& app) {
#if defined(Q_OS_WIN)
  // DWMWA_BORDER_COLOR only exists on Windows 11; older systems have no
  // DWM popup border to remove in the first place.
  if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11_21H2) {
    app.installEventFilter(new DwmPopupBorderFilter(&app));
  }
#else
  Q_UNUSED(app);
#endif
}

muffin::DwmPopupBorderFilter::DwmPopupBorderFilter(QObject* parent) : QObject(parent) {}

bool muffin::DwmPopupBorderFilter::eventFilter(QObject* watched, QEvent* event) {
#if defined(Q_OS_WIN)
  if (event->type() == QEvent::Show) {
    if (auto* widget = qobject_cast<QWidget*>(watched);
        widget != nullptr && widget->isWindow() && widget->windowType() == Qt::Popup &&
        widget->testAttribute(Qt::WA_WState_Created)) {
      // Combo dropdown containers (a top-level QFrame hosting a QAbstractItemView)
      // are left partly unpainted by the Windows 11 style's transparent-popup
      // handling: it clears the item view's viewport autofill and makes PE_Frame
      // draw nothing for the container, so the view's QSS padding strip and
      // scrollbar gutter render as a black ring inside the popup window.
      // 1) Restore the viewport fill so the whole view interior paints with the
      //    QSS background colour.
      // 2) Give the container a name the chrome stylesheets can target (they
      //    paint it with the themed surface colour), then force a QSS
      //    re-evaluation — renaming alone does not restyle an already-polished
      //    widget. Menus paint their own background and must keep their QMenu
      //    rule, so they are skipped.
      if (qobject_cast<QMenu*>(widget) == nullptr) {
        if (QAbstractItemView* view = widget->findChild<QAbstractItemView*>()) {
          if (view->viewport() != nullptr && !view->viewport()->autoFillBackground()) {
            view->viewport()->setAutoFillBackground(true);
            view->viewport()->update();
          }
        }
        if (widget->objectName() != QLatin1String("comboPopupContainer")) {
          widget->setObjectName(QStringLiteral("comboPopupContainer"));
          widget->style()->unpolish(widget);
          widget->style()->polish(widget);
          // Ensure the #comboPopupContainer background rule actually paints:
          // force the styled-background path for this already-created window.
          widget->setAttribute(Qt::WA_StyledBackground, true);
          widget->update();
        }
      }
      // Only touch windows that already have a native handle; calling winId()
      // here would force early native creation.
      if (HWND hwnd = reinterpret_cast<HWND>(widget->winId())) {
        const COLORREF noBorder = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
      }
    }
  }
#else
  Q_UNUSED(watched);
  Q_UNUSED(event);
#endif
  return QObject::eventFilter(watched, event);
}

}  // namespace muffin
