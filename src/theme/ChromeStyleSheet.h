#pragma once

#include "theme/ThemeDefinition.h"

class QString;

namespace muffin {

// Stylesheet generators: turn a ThemeDefinition into the QSS for a given surface.
// These replace the old hard-coded per-theme (and two-state night/light) QSS
// strings — every theme now tints the chrome from its own tokens.

// Main window chrome: QMainWindow, menu bar, menu popups, generic tool buttons.
QString mainWindowStyleSheet(const ThemeDefinition& theme);

// Sidebar: tab buttons, file/outline trees, empty-state label, new-file button.
// outlineFoldable toggles the expand-arrow (::branch) rule, matching the
// previous behaviour.
QString sidebarStyleSheet(const ThemeDefinition& theme, bool outlineFoldable);

// Preferences dialog: base canvas, sidebar, settings cards, and every control
// (buttons / checks / radios / combos / line edits / scrollbars / list items).
// Replaces the previously hard-coded LIGHT-only PreferencesDialog stylesheet so
// the dialog follows the active theme (the original "preferences always light"
// bug). The sidebar-title / page-title / placeholder labels are themed via
// object names (preferencesSidebarTitle / preferencesPageTitle / preferencesPlaceholder).
QString dialogStyleSheet(const ThemeDefinition& theme);

}  // namespace muffin
