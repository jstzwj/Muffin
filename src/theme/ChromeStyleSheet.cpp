#include "theme/ChromeStyleSheet.h"

#include <QColor>

namespace muffin {

namespace {

QString hexRgb(const QColor& c) {
  if (!c.isValid()) {
    return QStringLiteral("#000000");
  }
  // Use HexArgb to preserve alpha channel. When alpha == 255, this is identical
  // to HexRgb but correctly formats colors with alpha (e.g., rgba hover states).
  // Qt stylesheet accepts #RRGGBBAA notation.
  return c.name(QColor::HexArgb);
}

// Accent-colour tint used for menu-popup item selection, so the blue highlight
// cue survives across themes. Darker themes need a stronger alpha to read.
QString accentSelectionTint(const ThemeColors& c) {
  const QColor base = c.accent.isValid() ? c.accent : c.link;
  const int alpha = c.isDark ? 90 : 45;
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(base.red())
      .arg(base.green())
      .arg(base.blue())
      .arg(alpha);
}

}  // namespace

QString mainWindowStyleSheet(const ThemeDefinition& d) {
  const ThemeColors& c = d.colors;
  // Menu labels are primary application chrome, so they use chromeText. Keep the
  // colour on the item subcontrols too: QSS inheritance into QMenu::item and
  // QMenuBar::item varies by platform style, while an explicit colour is stable.
  return QStringLiteral(
      "QMainWindow { background: %1; }"
      "QMenuBar { background: %1; color: %2; padding: 0; font-size: 13px; }"
      "QMenuBar::item { padding: 4px 9px; background: transparent; color: %2; }"
      "QMenuBar::item:selected { background: %3; }"
      "QMenu { background: %4; color: %2; border: 1px solid %5; padding: 4px 0 4px 8px; }"
      "QMenu::item { padding: 5px 34px 5px 16px; color: %2; }"
      "QMenu::item:selected { background: %6; }"
      // %8 = chromeDisabled (the body ink faded toward the page background), so
      // disabled items read as clearly unavailable — NOT chromeMuted (%7), which
      // is only a hair lighter than the ink on light themes and also drives the
      // toolbar/sidebar's ACTIVE secondary text.
      "QMenu::item:disabled { color: %8; }"
      "QToolButton { background: transparent; border: 0; color: %7; padding: 0 8px;"
      "  min-width: 22px; min-height: 18px; font-size: 13px; }"
      "QToolButton:hover { background: %3; }"
      "QToolButton:checked { color: %2; background: %3; }")
      .arg(hexRgb(c.chromeBackground),
           hexRgb(c.chromeText),
           hexRgb(c.hover),
           hexRgb(c.surface),
           hexRgb(c.border),
           accentSelectionTint(c),
           hexRgb(c.chromeMuted),
           hexRgb(c.chromeDisabled));
}

QString sidebarStyleSheet(const ThemeDefinition& d, bool outlineFoldable) {
  const ThemeColors& c = d.colors;
  QString sheet = QStringLiteral(
      "#MuffinSidebar { background: %1; border-right: 1px solid %2; }"
      "#MuffinSidebar QToolButton { background: transparent; border: 0; color: %3; padding: 5px 4px; }"
      "#MuffinSidebar QToolButton:hover { background: %4; }"
      "#MuffinSidebar QToolButton:checked { color: %5; border-bottom: 3px solid %3; }"
      "#OutlineEmptyLabel { color: %3; }"
      "#FileTree, #OutlineTree { background: %1; color: %5; border: 0; padding: 4px 0; outline: 0; }"
      "#FileTree::item, #OutlineTree::item { min-height: 22px; padding: 1px 4px; border: 0; }"
      "#FileTree::item:hover, #OutlineTree::item:hover { background: %4; color: %5; }"
      "#FileTree::item:selected, #OutlineTree::item:selected { background: %6; color: %7; }")
      .arg(hexRgb(c.surface),
           hexRgb(c.border),
           hexRgb(c.chromeMuted),
           hexRgb(c.hover),
           hexRgb(c.chromeText),
           hexRgb(c.chromeSelection),
           hexRgb(c.chromeSelectionText));
  if (!outlineFoldable) {
    sheet += QStringLiteral("#OutlineTree::branch { image:none; width:0; }");
  }
  return sheet;
}

QString dialogStyleSheet(const ThemeDefinition& d) {
  const ThemeColors& c = d.colors;
  // Token map (every previously hard-coded colour is now a token):
  //   %1 canvas      dialog base background (tone behind cards)
  //   %2 chromeText  primary text (dialog, buttons, inputs, titles)
  //   %3 surface     cards / sidebar / buttons / inputs / combo dropdown bg
  //   %4 border      hairline borders + page-scrollbar handle
  //   %5 chromeMuted active muted text (sidebar nav, captions), scroll handle:hover
  //   %6 hover       hover + pressed + disabled fills
  //   %7 accent      focus rings, checked indicators, selected-item accent bar
  //   %8 chromeSelection list / combo selected-row fill (opaque, flattened from the
  //                   possibly-translucent editor `selected` onto surface)
  //   %9 chromeDisabled disabled/ghosted text + controls (clearly faded)
  //   %10 chromeSelectionText text paired with %8 (white on dark fills)
  return QStringLiteral(
      // Dialog base + sidebar card
      "QDialog { background:%1; color:%2; }"
      // Qt stylesheet `color` does NOT cascade to child widgets the way CSS does
      // — `QDialog { color }` only colours QDialog itself. A plain QLabel with no
      // matching rule falls back to the application palette (dark text), which is
      // invisible on a dark canvas. Match every QLabel explicitly so row labels
      // (e.g. "上传服务", "标题样式") follow the theme; the objectName rules below
      // override this for the muted/section variants.
      "QLabel { color:%2; background:transparent; }"
      "QWidget#preferencesSidebar { background:%3; border:1px solid %4; border-radius:8px; }"

      // Settings groups and rows
      "QWidget#settingsGroup { background:%3; border:1px solid %4; border-radius:8px; }"
      "QWidget#settingsCard { background:transparent; border:0; border-bottom:1px solid %4; border-radius:0; }"
      "QWidget#settingsCard[lastSettingsRow=\"true\"] { border-bottom:0; }"

      // QPushButton
      "QPushButton { border:1px solid %4; border-radius:6px; background:%3; min-height:30px; padding:0 13px; color:%2; }"
      "QPushButton:hover { background:%6; }"
      "QPushButton:pressed { background:%6; }"
      "QPushButton:focus { border-color:%7; }"
      "QPushButton:disabled { background:%6; color:%9; }"

      // QCheckBox
      "QCheckBox { spacing:8px; color:%2; min-height:24px; }"
      "QCheckBox:disabled { color:%9; }"
      "QCheckBox::indicator { width:16px; height:16px; border:1px solid %5; border-radius:4px; background:%3; }"
      "QCheckBox::indicator:hover { border-color:%7; }"
      "QCheckBox::indicator:checked { border:1px solid %7; background:%7; image:url(:/icons/ui/check.svg); }"
      "QCheckBox::indicator:checked:hover { background:%7; border-color:%7; }"
      "QCheckBox::indicator:disabled { background:%6; border-color:%4; }"
      "QCheckBox::indicator:checked:disabled { background:%9; border-color:%9; }"

      // QRadioButton
      "QRadioButton { spacing:8px; color:%2; min-height:24px; }"
      "QRadioButton:disabled { color:%9; }"
      "QRadioButton::indicator { width:16px; height:16px; border:1px solid %5; border-radius:8px; background:%3; }"
      "QRadioButton::indicator:hover { border-color:%7; }"
      "QRadioButton::indicator:checked { border:1px solid %7; background:%3; image:url(:/icons/ui/radio-dot.svg); }"
      "QRadioButton::indicator:disabled { background:%6; border-color:%4; }"

      // QComboBox
      "QComboBox { border:1px solid %4; border-radius:6px; background:%3; padding:4px 30px 4px 10px; min-height:24px; color:%2; }"
      "QComboBox:hover { border-color:%7; }"
      "QComboBox:focus, QComboBox:on { border-color:%7; }"
      "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:center right; width:28px; border:0; }"
      // Popup container (named by DwmPopupBorderFilter on show). The Windows 11
      // style leaves the container surface unpainted around the item view, which
      // renders as a black ring inside the popup window; fill it with the same
      // surface colour as the dropdown so the band blends into the list.
      "QWidget#comboPopupContainer { background:%3; border:none; }"
      // padding stays 0: with the Windows 11 style's transparent-popup handling
      // the view's padding strip is not painted and shows as a black band inside
      // the popup window (item insets come from the ::item rule's own padding).
      "QComboBox QAbstractItemView { border:1px solid %4; border-radius:6px; background:%3; color:%2;"
      "  selection-background-color:%8; selection-color:%10; outline:0; padding:0; }"
      "QComboBox QAbstractItemView::item { min-height:28px; padding:4px 10px; background:%3; color:%2; }"
      "QComboBox QAbstractItemView::item:hover { background:%6; color:%2; }"
      "QComboBox QAbstractItemView::item:selected { background:%8; color:%10; }"

      // QLineEdit
      "QLineEdit { border:1px solid %4; border-radius:6px; background:%3; padding:4px 10px; min-height:24px; color:%2; }"
      "QLineEdit:hover { border-color:%7; }"
      "QLineEdit:focus { border-color:%7; }"
      "QLineEdit:disabled { background:%6; color:%9; }"

      // QComboBox dropdown scrollbar (more specific than the page scrollbar below).
      // Trough is opaque surface (not transparent): inside the Win11 transparent
      // popup an unpainted trough renders black.
      "QComboBox QAbstractItemView QScrollBar:vertical { width:8px; background:%3; margin:0; border:0; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical { background:%5; min-height:28px; border-radius:4px; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical:hover { background:%5; }"
      "QComboBox QAbstractItemView QScrollBar::add-line:vertical, QComboBox QAbstractItemView QScrollBar::sub-line:vertical { height:0; }"
      "QComboBox QAbstractItemView QScrollBar::add-page:vertical, QComboBox QAbstractItemView QScrollBar::sub-page:vertical { background:%3; }"

      // Sidebar QListWidget (category nav)
      "QListWidget { border:0; outline:0; background:transparent; }"
      "QListWidget::item { min-height:34px; padding-left:14px; margin:2px 6px; color:%5; border-radius:6px; border-left:3px solid transparent; }"
      "QListWidget::item:hover { background:%6; color:%2; }"
      "QListWidget::item:selected { background:%8; color:%10; border-left:3px solid %7; font-weight:500; }"

      // Page QScrollArea
      "QScrollArea { border:0; background:transparent; }"
      "QScrollArea > QWidget > QWidget { background:transparent; }"

      // Page scrollbar (applies to every per-page scroll area)
      "QScrollBar:vertical { width:8px; background:transparent; margin:0; border:0; }"
      "QScrollBar::handle:vertical { background:%4; min-height:36px; border-radius:4px; }"
      "QScrollBar::handle:vertical:hover { background:%5; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"

      // Themed labels — set via object name so they pick up the token from QSS
      // without needing the ThemeDefinition at label-construction time.
      "QLabel#preferencesSidebarTitle { font-size:15px; font-weight:600; color:%2; padding-left:8px; padding-bottom:4px; }"
      "QLabel#preferencesPageTitle { font-size:26px; font-weight:600; color:%2; }"
      "QLabel#preferencesPlaceholder { color:%5; }"
      // Section headings ("Theme", "Zoom", …) and helper captions on every page.
      "QLabel#preferencesSectionLabel { font-size:13px; font-weight:600; color:%2; }"
      "QLabel#preferencesMutedLabel { font-size:12px; color:%5; }"
      // Export page: its own format-list panel + header.
      "QWidget#exportPanel { background:%3; border-right:1px solid %4; }"
      "QLabel#exportPanelHeader { font-size:12px; font-weight:600; color:%5; }")
      .arg(hexRgb(c.canvas),
           hexRgb(c.chromeText),
           hexRgb(c.surface),
           hexRgb(c.border),
           hexRgb(c.chromeMuted),
           hexRgb(c.hover),
           hexRgb(c.accent),
           hexRgb(c.chromeSelection),
           hexRgb(c.chromeDisabled),
           hexRgb(c.chromeSelectionText));
}

}  // namespace muffin
