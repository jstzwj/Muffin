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
  return QStringLiteral("rgba(%1,%2,%3,%4)").arg(base.red()).arg(base.green()).arg(base.blue()).arg(alpha);
}

} // namespace

QString scrollBarStyleSheet(const QColor& background, const QColor& foreground, const QString& scope) {
  const auto tint = [&](qreal amount) {
    return hexRgb(QColor::fromRgbF(background.redF() * (1 - amount) + foreground.redF() * amount,
                                   background.greenF() * (1 - amount) + foreground.greenF() * amount,
                                   background.blueF() * (1 - amount) + foreground.blueF() * amount));
  };
  const QString prefix = scope.isEmpty() ? QString() : scope + QLatin1Char(' ');
  // 14px interaction gutter, 8px visible thumb. Keep the grab target generous
  // without depending on the platform's native scrollbar width or arrows.
  return QStringLiteral(
             "%1QScrollBar:vertical { background:%2; width:14px; margin:0; border:0; }"
             "%1QScrollBar::handle:vertical { background:%3; min-height:54px; border-radius:4px; margin:2px 3px; }"
             "%1QScrollBar::handle:vertical:hover { background:%4; }"
             "%1QScrollBar::handle:vertical:pressed { background:%4; }"
             "%1QScrollBar::add-line:vertical, %1QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
             "%1QScrollBar::add-page:vertical, %1QScrollBar::sub-page:vertical { background:%2; }"
             "%1QScrollBar:horizontal { background:%2; height:14px; margin:0; border:0; }"
             "%1QScrollBar::handle:horizontal { background:%3; min-width:54px; border-radius:4px; margin:3px 2px; }"
             "%1QScrollBar::handle:horizontal:hover { background:%4; }"
             "%1QScrollBar::handle:horizontal:pressed { background:%4; }"
             "%1QScrollBar::add-line:horizontal, %1QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
             "%1QScrollBar::add-page:horizontal, %1QScrollBar::sub-page:horizontal { background:%2; }"
             "%1QAbstractScrollArea::corner { background:%2; border:0; }")
      .arg(prefix, hexRgb(background), tint(0.32), tint(0.50));
}

QString mainWindowStyleSheet(const ThemeDefinition& d) {
  const ThemeColors& c = d.colors;
  // A faint ink tint keeps the close button quieter than the whole-tab hover,
  // even when a theme uses a strong menu/toolbar hover colour.
  QColor closeHover = c.chromeText;
  closeHover.setAlpha(18);
  QColor closePressed = c.chromeText;
  closePressed.setAlpha(32);
  // Menu labels are primary application chrome, so they use chromeText. Keep the
  // colour on the item subcontrols too: QSS inheritance into QMenu::item and
  // QMenuBar::item varies by platform style, while an explicit colour is stable.
  return QStringLiteral(
             "QMainWindow { background: %1; }"
             "QStatusBar { background:%1; border:0; padding:0; }"
             "QStatusBar::item { border:0; }"
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
             "QToolButton:checked { color: %2; background: %3; }"
             "QTabWidget#DocumentTabs::pane { border: 0; }"
             "QTabWidget#DocumentTabs QTabBar { background: %1; font-size: 13px; font-weight: normal; }"
             // Match Qt's reserved scroller geometry. Global tool-button padding
             // otherwise expands each 16px arrow to 38px and overlaps its peer.
             "QTabWidget#DocumentTabs QTabBar::scroller { width: 56px; }"
             "QTabWidget#DocumentTabs QToolButton#ScrollLeftButton,"
             "QTabWidget#DocumentTabs QToolButton#ScrollRightButton {"
             " min-width: 0; min-height: 0; padding: 0; border: 0; border-radius: 0;"
             " background: %1; color: %7; }"
             "QTabWidget#DocumentTabs QToolButton#ScrollLeftButton:hover,"
             "QTabWidget#DocumentTabs QToolButton#ScrollRightButton:hover { background: %3; color: %2; }"
             "QTabWidget#DocumentTabs QToolButton#ScrollLeftButton:disabled,"
             "QTabWidget#DocumentTabs QToolButton#ScrollRightButton:disabled { color: %8; }"
             "QTabWidget#DocumentTabs QTabBar::tab { background: transparent; color: %7;"
             " margin: 4px 3px; padding: 4px 10px; min-width: 104px; max-width: 220px;"
             " border: 1px solid transparent; border-radius: 6px; }"
             "QTabWidget#DocumentTabs QTabBar::tab:hover { background: %3; color: %2; }"
             "QTabWidget#DocumentTabs QTabBar::tab:selected { background: %4; color: %2; border-color: %5; }"
             "QWidget#TabControls { background: transparent; }"
             "QWidget#TabLockIndicator { background: transparent; color: %7; }"
             "QToolButton#CloseTabButton, QToolButton#PinTabButton { background: transparent; border: 0; color: %7;"
             " min-width: 0; min-height: 0; padding: 0; border-radius: 10px;"
             " font-size: 16px; font-weight: normal; }"
             "QToolButton#CloseTabButton:hover, QToolButton#PinTabButton:hover { background: %9; color: %2; }"
             "QToolButton#CloseTabButton:pressed, QToolButton#PinTabButton:pressed { background: %10; color: %2; }"
             "QToolButton#NewTabButton { min-width: 0; min-height: 0; padding: 0; border-radius: 5px;"
             " font-size: 18px; font-weight: normal; }")
      .arg(hexRgb(c.chromeBackground), hexRgb(c.chromeText), hexRgb(c.hover), hexRgb(c.surface), hexRgb(c.border),
           accentSelectionTint(c), hexRgb(c.chromeMuted), hexRgb(c.chromeDisabled), hexRgb(closeHover),
           hexRgb(closePressed));
}

QString sidebarStyleSheet(const ThemeDefinition& d, bool outlineFoldable) {
  const ThemeColors& c = d.colors;
  // Toolbar toggles need a quiet state cue even when the theme uses a strong
  // hover colour for list rows. Keep their artwork and geometry unchanged.
  QColor toolbarHover = c.chromeText;
  toolbarHover.setAlpha(18);
  QColor toolbarChecked = c.chromeText;
  toolbarChecked.setAlpha(12);
  QColor toolbarBorder = c.chromeText;
  toolbarBorder.setAlpha(32);
  QColor toolbarPressed = c.chromeText;
  toolbarPressed.setAlpha(30);
  QString sheet =
      QStringLiteral(
          "#MuffinSidebar { background: %1; border-right: 1px solid %2; }"
          "#MuffinSidebar QToolButton { background: transparent; border: 0; color: %3; padding: 5px 4px; }"
          "#MuffinSidebar QToolButton:hover { background: %4; }"
          "#MuffinSidebar QToolButton:checked { color: %5; border-bottom: 3px solid %3; }"
          "#OutlineEmptyLabel { color: %3; }"
          "#FilesEmptyLabel { color: %3; }"
          "#MuffinSidebar QLabel, #MuffinSidebar QCheckBox { color:%5; background:transparent; }"
          "#MuffinSidebar QLineEdit { color:%5; background:%1; border:1px solid %2; border-radius:4px; padding:5px; }"
          "#MuffinSidebar QPushButton { color:%5; background:%1; border:1px solid %2; border-radius:4px; padding:4px; }"
          "#MuffinSidebar QPushButton:hover { background:%4; }"
          "#MuffinSidebar QPushButton:disabled { color:%3; }"
          "#WorkspaceSearchResults { background:%1; color:%5; border:0; }"
          "#WorkspaceSearchResults::item { min-height:24px; padding:2px; }"
          "#WorkspaceSearchResults::item:selected { background:%6; color:%7; }"
          "#FolderToolbar QToolButton { padding: 0; min-width: 0; min-height: 0; border:1px solid transparent; border-radius: 4px; }"
          "#FolderToolbar QToolButton:hover { background: %8; }"
          "#FolderToolbar QToolButton:checked { background: %9; border: 1px solid %10; }"
          "#FolderToolbar QToolButton:checked:hover { background: %8; }"
          "#FolderToolbar QToolButton:pressed, #FolderToolbar QToolButton:checked:pressed { background: %11; }"
          "#FolderToolbar QToolButton::menu-indicator { image: none; width: 0; }"
          "#FileTree, #OutlineTree { background: %1; color: %5; border: 0; padding: 4px 0; outline: 0; }"
          "#FileTree::item, #OutlineTree::item { min-height: 22px; padding: 1px 4px; border: 0; }"
          "#FileTree::item:hover, #OutlineTree::item:hover { background: %4; color: %5; }"
          "#FileTree::item:selected, #OutlineTree::item:selected { background: %6; color: %7; }")
          .arg(hexRgb(c.surface), hexRgb(c.border), hexRgb(c.chromeMuted), hexRgb(c.hover), hexRgb(c.chromeText),
               hexRgb(c.chromeSelection), hexRgb(c.chromeSelectionText), hexRgb(toolbarHover), hexRgb(toolbarChecked),
               hexRgb(toolbarBorder), hexRgb(toolbarPressed));
  if (!outlineFoldable) {
    sheet += QStringLiteral("#OutlineTree::branch { image:none; width:0; }");
  }
  return sheet + scrollBarStyleSheet(c.surface, c.chromeText);
}

QString quickOpenStyleSheet(const ThemeDefinition& d) {
  const auto& c = d.colors;
  return QStringLiteral(
             "QDialog#QuickOpenDialog { background:%1; color:%2; }"
             "QDialog#QuickOpenDialog QLabel { color:%3; background:transparent; }"
             "QLineEdit#QuickOpenFilter { background:%4; color:%2; border:1px solid %5; border-radius:5px; padding:7px; }"
             "QLineEdit#QuickOpenFilter:focus { border-color:%6; }"
             "QListWidget#QuickOpenResults { background:%4; color:%2; border:0; outline:0; }")
             .arg(hexRgb(c.canvas), hexRgb(c.chromeText), hexRgb(c.chromeMuted), hexRgb(c.surface), hexRgb(c.border),
                  hexRgb(c.accent)) +
         scrollBarStyleSheet(c.surface, c.chromeText);
}

QPalette dialogPalette(const ThemeDefinition& d) {
  const auto& c = d.colors;
  QPalette palette;
  palette.setColor(QPalette::Window, c.canvas);
  palette.setColor(QPalette::WindowText, c.chromeText);
  palette.setColor(QPalette::Base, c.surface);
  palette.setColor(QPalette::AlternateBase, c.hover);
  palette.setColor(QPalette::Text, c.chromeText);
  palette.setColor(QPalette::Button, c.surface);
  palette.setColor(QPalette::ButtonText, c.chromeText);
  palette.setColor(QPalette::PlaceholderText, c.chromeMuted);
  palette.setColor(QPalette::Highlight, c.chromeSelection);
  palette.setColor(QPalette::HighlightedText, c.chromeSelectionText);
  palette.setColor(QPalette::ToolTipBase, c.surface);
  palette.setColor(QPalette::ToolTipText, c.chromeText);
  palette.setColor(QPalette::Link, c.accent);
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
    palette.setColor(QPalette::Disabled, role, c.chromeDisabled);
  return palette;
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
             "QLabel:disabled { color:%9; }"
             "QWidget#preferencesPage { background:transparent; }"
             "QPlainTextEdit { background:%3; color:%2; border:1px solid %4;"
             " border-radius:6px; padding:6px; selection-background-color:%8; selection-color:%10; }"
             "QPlainTextEdit:focus { border-color:%7; }"
             "QWidget#preferencesSidebar { background:%3; border:1px solid %4; border-radius:8px; }"

             // Settings groups and rows
             "QWidget#settingsGroup { background:%3; border:1px solid %4; border-radius:8px; }"
             "QWidget#settingsCard { background:transparent; border:0; border-bottom:1px solid %4; border-radius:0; }"
             "QWidget#settingsCard[lastSettingsRow=\"true\"] { border-bottom:0; }"
             "QWidget#fileOpenProtection { background:transparent; border:0; border-bottom:1px solid %4; }"
             "QWidget#fileProtectionOption { background:%1; border:1px solid %4; border-radius:8px; }"
             "QCheckBox#extensionProtection, QCheckBox#sizeProtection { font-weight:600; }"
             "QPushButton#resetTextExtensions { background:transparent; border:0; min-height:24px; padding:0 2px; color:%5; font-size:12px; }"
             "QPushButton#resetTextExtensions:hover { color:%7; }"
             "QPushButton#resetTextExtensions:disabled { color:%9; }"
             "QWidget#textExtensionChip { background:%3; border:1px solid %4; border-radius:6px; }"
             "QWidget#textExtensionChip:disabled { background:%6; }"
             "QWidget#textExtensionChip QLabel { color:%2; font-size:12px; }"
             "QWidget#textExtensionChip QLabel:disabled { color:%9; }"
             "QToolButton#removeTextExtension { background:transparent; border:0; border-radius:4px; padding:0; color:%5; font-size:15px; }"
             "QToolButton#removeTextExtension:hover { background:%6; color:%2; }"
             "QToolButton#removeTextExtension:focus { border:1px solid %7; }"
             "QToolButton#removeTextExtension:disabled { color:%9; }"

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
             "QComboBox#sizeLimitUnit::down-arrow { image:url(:/icons/ui/chevron-down.svg); width:12px; height:12px; }"
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
             "QLineEdit { selection-background-color:%8; selection-color:%10; }"
             "QSpinBox, QDoubleSpinBox { border:1px solid %4; border-radius:6px; background:%3; padding:4px 10px; min-height:24px; color:%2; }"
             "QSpinBox:hover:!disabled, QDoubleSpinBox:hover:!disabled { border-color:%7; }"
             "QSpinBox:focus, QDoubleSpinBox:focus { border-color:%7; }"
             "QSpinBox:disabled, QDoubleSpinBox:disabled { background:%1; color:%9; }"
             // Fully style the step controls: a styled frame alone leaves the
             // native Windows side-by-side, raised buttons inside the field.
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::up-button { subcontrol-origin:border; subcontrol-position:top right;"
             "  width:24px; height:16px; top:1px; right:1px; border:0; border-top-right-radius:5px; background:transparent; }"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::down-button { subcontrol-origin:border; subcontrol-position:bottom right;"
             "  width:24px; height:16px; bottom:1px; right:1px; border:0; border-bottom-right-radius:5px; background:transparent; }"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::up-button:hover:!disabled:!off,"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::down-button:hover:!disabled:!off { background:%6; }"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::up-button:pressed:!disabled:!off,"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::down-button:pressed:!disabled:!off { background:%8; }"
             // PreferencesSpinBox paints palette-based vector chevrons in these
             // hit rectangles; suppress the platform's bitmap/font arrows.
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::up-arrow,"
             "QAbstractSpinBox[preferencesStepButtons=\"true\"]::down-arrow { image:none; }"

             // Shortcut command list and editor. Do not inherit the OS palette:
             // it may be light while the selected document theme is dark.
             "QTreeWidget#ShortcutCommands { background:%3; color:%2; border:1px solid %4; border-radius:8px;"
             "  selection-background-color:%8; selection-color:%10; outline:0; font-size:14px; }"
             "QTreeWidget#ShortcutCommands::item { min-height:20px; padding:6px 10px; border:0; }"
             "QTreeWidget#ShortcutCommands::item:hover { background:%6; color:%2; }"
             "QTreeWidget#ShortcutCommands::item:selected { background:%8; color:%10; }"
             "QTreeWidget#ShortcutCommands QHeaderView { background:%3; }"
             "QTreeWidget#ShortcutCommands QHeaderView::section { background:%3; color:%5; border:0;"
             "  border-bottom:1px solid %4; padding:9px 10px; font-size:12px; font-weight:600; }"
             "QWidget#ShortcutEditor { background:%3; border:1px solid %4; border-radius:8px; }"
             "QKeySequenceEdit { background:transparent; }"
             "QPushButton#ShortcutApply { font-weight:600; border-color:%7; }"
             "QPushButton#ShortcutApply:disabled { border-color:%4; color:%9; }"
             "QPushButton#ShortcutReset { background:transparent; color:%5; }"
             "QPushButton#ShortcutReset:hover { background:%6; color:%2; }"
             "QMessageBox { background:%1; color:%2; }"
             "QMessageBox QLabel#qt_msgbox_label { font-size:14px; font-weight:600; }"
             "QMessageBox#ShortcutResetConfirmation QLabel#qt_msgbox_label { min-width:300px; padding-top:6px; }"
             "QMessageBox#ShortcutResetConfirmation QLabel#qt_msgbox_informativelabel { color:%5; min-width:300px;"
             "  padding-top:4px; padding-bottom:12px; font-size:13px; }"

             // Sidebar QListWidget (category nav)
             "QListWidget { border:0; outline:0; background:transparent; }"
             "QListWidget::item { min-height:34px; padding-left:14px; margin:2px 6px; color:%5; border-radius:6px; border-left:3px solid transparent; }"
             "QListWidget::item:hover { background:%6; color:%2; }"
             "QListWidget::item:selected { background:%8; color:%10; border-left:3px solid %7; font-weight:500; }"

             // Page QScrollArea
             "QScrollArea { border:0; background:transparent; }"
             "QScrollArea > QWidget > QWidget { background:transparent; }"

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
             .arg(hexRgb(c.canvas), hexRgb(c.chromeText), hexRgb(c.surface), hexRgb(c.border), hexRgb(c.chromeMuted),
                  hexRgb(c.hover), hexRgb(c.accent), hexRgb(c.chromeSelection), hexRgb(c.chromeDisabled),
                  hexRgb(c.chromeSelectionText)) +
         scrollBarStyleSheet(c.canvas, c.chromeText) +
         // Popups need an opaque surface with the same inset/geometry, otherwise
         // the transparent Windows 11 combo window can show black gutters.
         scrollBarStyleSheet(c.surface, c.chromeText, QStringLiteral("QComboBox QAbstractItemView"));
}

} // namespace muffin
