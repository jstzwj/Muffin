#pragma once

#include <QObject>

class QApplication;

namespace muffin {

// Removes the DWM-drawn window border Windows 11 puts around Qt::Popup
// top-level windows (menus, combo dropdowns, completer popups).
//
// Why: on Windows 11 the QWindows11Style delegates popup rounding to DWM
// (DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)) and
// draws no frame of its own for the combo popup container. DWM additionally
// strokes a border around such windows — near-black under a light system
// theme, ~2 physical px at 150% scaling. The dialog stylesheet separately
// draws a 1px themed hairline on the popup's item view, so the two stack
// into the "thick black double border" seen around e.g. the Preferences
// dropdowns. Clearing DWMWA_BORDER_COLOR keeps the DWM rounded corners but
// leaves only the themed hairline, so popups match the app's QSS chrome.
//
// The filter watches QEvent::Show for top-level widgets whose windowType()
// is Qt::Popup and clears their border colour. QEvent::WinIdChange is too
// early (same reason QWindows11Style waits for Show), and Show re-fires on
// every open, which also re-applies the attribute if DWM reset it.
class DwmPopupBorderFilter : public QObject {
  Q_OBJECT

public:
  // Installs the filter on the application. No-op on non-Windows platforms
  // and on Windows versions without DWMWA_BORDER_COLOR (pre-Windows 11).
  static void install(QApplication& app);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  explicit DwmPopupBorderFilter(QObject* parent);
};

}  // namespace muffin
