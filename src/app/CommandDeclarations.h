#pragma once

// Single source of truth for Muffin's commands and menu layout.
//
// Each CommandDeclaration bundles everything about one command: its id, the
// translatable label and shortcut shown in menus, whether it is checkable / part
// of an exclusive radio group, its handler, and the predicates that drive its
// dynamic enabled/checked state. MainWindow::bindCommands() binds every handler
// from this table; MainWindow::updateActionsForCategory() applies every
// predicate. Menus are built by walking mainMenuSpec(), which references
// commands by id — so adding a command is one table row plus one menu position,
// not edits across three files.
//
// Behavioral code (handlers) lives here as std::function rather than in a
// scattered bindCommands switch because the table is the single source: the
// same row that declares the label also declares what the command does and when
// it is enabled. Handlers take MainWindow& by value-semantic reference so the
// table can be built as a free function (no instance capture) and reused across
// windows.

#include <QKeySequence>
#include <QString>

#include <functional>
#include <vector>

namespace muffin {

class MainWindow;

enum class CommandCategory {
  File,
  Edit,
  Format,
  Table,
  Paragraph,
  Code,
  Html,
  Math,
  Image,
  View,
  Theme,
  Help,
  Other,
};

struct CommandDeclaration {
  QString id;
  CommandCategory category = CommandCategory::Other;
  QString text;        // label, authored as muffin::MainWindow::tr(...)
  QKeySequence shortcut;
  bool checkable = false;
  bool checkedInitial = false;
  bool enabledInitial = true;
  // Some navigation actions must not steal keystrokes from the focused editor or
  // line edits, so they use Qt::WidgetShortcut (shown in the menu, not global).
  bool shortcutWidgetContext = false;
  // Non-empty actions are grouped into an exclusive QActionGroup (radio items).
  QString actionGroup;

  // null handler => menu-only placeholder (no command bound).
  std::function<void(MainWindow&)> handler;
  // null => always enabled (subject to enabledInitial at creation).
  std::function<bool(const MainWindow&)> enabled;
  // null => no checked state to sync. Only meaningful for checkable actions.
  std::function<bool(const MainWindow&)> checked;
};

// All commands, in a stable order. Cached after first build (predicates are
// evaluated on cursor moves, so the table must not rebuild each call).
const std::vector<CommandDeclaration>& commandDeclarations();
// Lookup by id, or nullptr.
const CommandDeclaration* commandDeclaration(const QString& id);
// Drop the cached tables so the next access rebuilds them with fresh tr() labels.
// Called on locale change (retranslateUi); the cache otherwise stays put.
void refreshDeclarations();

// ---- Declarative menu layout ------------------------------------------------

// A submenu whose QMenu* must be retained for later (dynamic) population, e.g.
// "Open Recent" and "Reopen with Encoding". buildMenus assigns the matching
// MainWindow member pointer when it creates the submenu.
enum class DynamicMenu {
  None,
  RecentFiles,
  ReopenEncoding,
  DeleteRange,
  Themes,  // top-level Theme menu: enumerated from ThemeManager::definitions()
};

struct MenuItem {
  enum class Kind {
    Action,             // a registered command, looked up by commandId
    Separator,
    Submenu,            // nested menu with static children
    PlaceholderSubmenu, // disabled menu (future feature), title only
    DynamicSubmenu,     // member-pointer submenu populated elsewhere
  };
  Kind kind = Kind::Action;

  QString commandId;               // Action
  QString title;                   // Submenu / PlaceholderSubmenu
  std::vector<MenuItem> children;  // Submenu
  bool hidden = false;             // Submenu: created with menuAction() hidden
  DynamicMenu dynamicId = DynamicMenu::None;  // DynamicSubmenu
};

struct MenuSpec {
  QString title;  // top-level menu title (tr())
  std::vector<MenuItem> items;
  bool hidden = false;  // top-level menu created hidden (Table/Code/Math/Html)
  // When not None, buildMenus captures this top-level menu into the matching
  // MainWindow member for dynamic population (e.g. the Theme menu, which is
  // enumerated from ThemeManager::definitions() at runtime rather than a fixed
  // command list).
  DynamicMenu dynamicId = DynamicMenu::None;
};

// The full menu bar, in order. Top-level menus in display order; each menu's
// items in the order they appear, with explicit separators.
const std::vector<MenuSpec>& mainMenuSpec();

}  // namespace muffin
