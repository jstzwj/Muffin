#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace muffin {

// Every colour the UI can theme, in one place. This is the single source of
// truth that the editor (via RenderTheme), the chrome (menu bar, sidebar,
// status bar) and the dialogs (Preferences, …) all read from. Split into two
// groups:
//   • document colours — mirror RenderTheme (the editor / HTML engine palette)
//   • chrome colours  — the window/dialog surface palette, previously
//                       hard-coded per-widget in QSS strings
struct ThemeColors {
  // --- Document / editor (mirror RenderTheme) ---
  QColor background;
  QColor text;
  QColor muted;
  QColor link;
  QColor codeBackground;
  QColor codeBorder;
  QColor quoteBorder;
  QColor tableBorder;
  QColor tableHeaderBackground;
  QColor tableAlternateBackground;
  QColor highlight;
  QColor selection;

  // --- Chrome / dialog (previously hard-coded per-widget) ---
  QColor chromeBackground;  // main window + dialog base background
  QColor chromeText;        // menu bar / dialog primary text
  QColor chromeMuted;       // secondary text, disabled controls
  QColor surface;           // cards, panels, sidebar, list backgrounds
  QColor canvas;            // tone behind cards/dialog panels (slightly distinct
                            //   from surface for depth; equals chromeBackground
                            //   when a theme doesn't distinguish them)
  QColor border;            // generic chrome border (menus, cards, splitter)
  QColor hover;             // hover background (menu items, list rows, buttons)
  QColor selected;          // selected item background (list rows, checked tabs)
  QColor accent;            // focus / links / checked-indicator

  // Typography: when true, paragraph + heading text renders in a serif family
  // (code/math stay monospace). Models themes whose identity is typography
  // rather than colour — e.g. Pixyll, which is a serif-body theme.
  bool serifBody = false;

  bool isDark = false;
};

// A complete, serializable theme. Built-in themes are produced by builtIns();
// custom themes are loaded from JSON files (see fromJson) and merged into the
// same registry, so the rest of the app never has to special-case them.
struct ThemeDefinition {
  QString id;         // machine name, e.g. "newsprint" (also the JSON file stem)
  QString label;      // display name, e.g. "Newsprint"
  ThemeColors colors;
  bool isBuiltIn = true;

  bool valid() const;

  // JSON serialization — used both to persist a built-in for inspection and to
  // load a user-supplied custom theme file. `id` overrides the JSON's name when
  // loading (e.g. derived from the file name).
  static ThemeDefinition fromJson(const QJsonObject& json, const QString& id = {});
  QJsonObject toJson() const;

  // The built-in themes, in display order. Each carries a chrome palette that
  // matches its document palette (so e.g. the warm "newsprint" theme warms the
  // whole chrome, not just the document).
  static const std::vector<ThemeDefinition>& builtIns();
  static std::optional<ThemeDefinition> builtIn(const QString& id);
};

}  // namespace muffin
