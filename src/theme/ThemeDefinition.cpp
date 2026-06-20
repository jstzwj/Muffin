#include "theme/ThemeDefinition.h"

namespace muffin {

namespace {

QColor parseColor(const QJsonObject& colors, const char* key) {
  const QJsonValue v = colors.value(QLatin1String(key));
  if (!v.isString()) {
    return QColor();
  }
  QColor c(v.toString());
  return c.isValid() ? c : QColor();
}

}  // namespace

bool ThemeDefinition::valid() const {
  // Usable as long as the core document colours resolved. Chrome fields fall
  // back to document colours in fromJson(), so they need not be present.
  return colors.background.isValid() && colors.text.isValid();
}

ThemeDefinition ThemeDefinition::fromJson(const QJsonObject& json, const QString& idHint) {
  ThemeDefinition d;
  d.isBuiltIn = false;
  d.id = (idHint.isEmpty() ? json.value(QStringLiteral("name")).toString() : idHint).toLower();
  d.label = json.value(QStringLiteral("label")).toString(d.id);
  const QJsonObject c = json.value(QStringLiteral("colors")).toObject();

  ThemeColors& k = d.colors;
  k.background = parseColor(c, "background");
  k.text = parseColor(c, "text");
  k.muted = parseColor(c, "muted");
  k.link = parseColor(c, "link");
  k.codeBackground = parseColor(c, "codeBackground");
  k.codeBorder = parseColor(c, "codeBorder");
  k.quoteBorder = parseColor(c, "quoteBorder");
  k.tableBorder = parseColor(c, "tableBorder");
  k.tableHeaderBackground = parseColor(c, "tableHeaderBackground");
  k.tableAlternateBackground = parseColor(c, "tableAlternateBackground");
  k.highlight = parseColor(c, "highlight");
  k.selection = parseColor(c, "selection");
  k.chromeBackground = parseColor(c, "chromeBackground");
  k.chromeText = parseColor(c, "chromeText");
  k.chromeMuted = parseColor(c, "chromeMuted");
  k.surface = parseColor(c, "surface");
  k.canvas = parseColor(c, "canvas");
  k.border = parseColor(c, "border");
  k.hover = parseColor(c, "hover");
  k.selected = parseColor(c, "selected");
  k.accent = parseColor(c, "accent");
  k.serifBody = c.value(QStringLiteral("serifBody")).toBool(false);

  // Derive chrome defaults from the document palette when a theme file omits
  // them, so a minimal (document-colours-only) JSON still renders sanely.
  if (!k.chromeBackground.isValid()) k.chromeBackground = k.background;
  if (!k.chromeText.isValid()) k.chromeText = k.text;
  if (!k.chromeMuted.isValid()) k.chromeMuted = k.muted;
  if (!k.surface.isValid()) k.surface = k.background;
  // canvas = the tone behind cards. A minimal theme usually omits it, so derive
  // a subtle step off the base tones for depth (lighter themes get a faint gray
  // canvas; darker themes get a marginally darker one) rather than going flat.
  if (!k.canvas.isValid()) {
    const QColor base = k.chromeBackground.isValid() ? k.chromeBackground : k.background;
    k.canvas = base.lightness() < 128 ? base.darker(112) : k.surface.darker(104);
  }
  if (!k.border.isValid()) k.border = k.codeBorder;
  if (!k.hover.isValid()) k.hover = k.codeBackground;
  if (!k.selected.isValid()) k.selected = k.codeBackground;
  if (!k.accent.isValid()) k.accent = k.link;
  k.isDark = c.value(QStringLiteral("isDark")).toBool(k.background.lightness() < 128);
  return d;
}

QJsonObject ThemeDefinition::toJson() const {
  QJsonObject c;
  auto put = [&c](const char* key, const QColor& col) {
    if (col.isValid()) {
      c.insert(QLatin1String(key), col.name(QColor::HexArgb));
    }
  };
  put("background", colors.background);
  put("text", colors.text);
  put("muted", colors.muted);
  put("link", colors.link);
  put("codeBackground", colors.codeBackground);
  put("codeBorder", colors.codeBorder);
  put("quoteBorder", colors.quoteBorder);
  put("tableBorder", colors.tableBorder);
  put("tableHeaderBackground", colors.tableHeaderBackground);
  put("tableAlternateBackground", colors.tableAlternateBackground);
  put("highlight", colors.highlight);
  put("selection", colors.selection);
  put("chromeBackground", colors.chromeBackground);
  put("chromeText", colors.chromeText);
  put("chromeMuted", colors.chromeMuted);
  put("surface", colors.surface);
  put("canvas", colors.canvas);
  put("border", colors.border);
  put("hover", colors.hover);
  put("selected", colors.selected);
  put("accent", colors.accent);
  if (colors.serifBody) {
    c.insert(QStringLiteral("serifBody"), true);
  }
  c.insert(QStringLiteral("isDark"), colors.isDark);

  QJsonObject root;
  root.insert(QStringLiteral("name"), id);
  root.insert(QStringLiteral("label"), label);
  root.insert(QStringLiteral("colors"), c);
  return root;
}

const std::vector<ThemeDefinition>& ThemeDefinition::builtIns() {
  static const std::vector<ThemeDefinition> kBuiltIns = [] {
    std::vector<ThemeDefinition> out;
    auto add = [&out](const char* id, const char* label, ThemeColors c) {
      ThemeDefinition d;
      d.id = QString::fromLatin1(id);
      d.label = QString::fromLatin1(label);
      d.colors = std::move(c);
      d.isBuiltIn = true;
      out.push_back(std::move(d));
    };

    // github — default, light/neutral. Chrome palette matches the current
    // hard-coded light chrome so this theme is visually unchanged.
    {
      ThemeColors c;
      c.background = QColor("#ffffff");
      c.text = QColor("#202124");
      c.muted = QColor("#57606a");
      c.link = QColor("#4183c4");
      c.codeBackground = QColor("#f6f8fa");
      c.codeBorder = QColor("#e5e7eb");
      c.quoteBorder = QColor("#d0d7de");
      c.tableBorder = QColor("#dfe2e5");
      c.tableHeaderBackground = QColor("#edf4ff");
      c.tableAlternateBackground = QColor("#f6f8fa");
      c.highlight = QColor("#fff8c5");
      c.selection = QColor("#d7e8ff");
      c.chromeBackground = QColor("#ffffff");
      c.chromeText = QColor("#1f2328");
      c.chromeMuted = QColor("#57606a");
      c.surface = QColor("#ffffff");
      c.canvas = QColor("#f6f7f9");
      c.border = QColor("#d0d7de");
      c.hover = QColor("#f6f8fa");
      c.selected = QColor("#e9e9e9");
      c.accent = QColor("#0969da");
      c.isDark = false;
      add("github", "GitHub", c);
    }
    // newsprint — warm. Chrome warmed to match, so the theme tints the whole
    // UI (previously the chrome stayed neutral light for every non-night theme).
    {
      ThemeColors c;
      c.background = QColor("#fbfaf7");
      c.text = QColor("#1f2328");
      c.muted = QColor("#6b665d");
      c.link = QColor("#2f6f9f");
      c.codeBackground = QColor("#f1eee8");
      c.codeBorder = QColor("#ded8cc");
      c.quoteBorder = QColor("#c8bfae");
      c.tableBorder = QColor("#d8d0c2");
      c.tableHeaderBackground = QColor("#efe3ce");
      c.tableAlternateBackground = QColor("#f6f3ed");
      c.highlight = QColor("#fff8c5");
      c.selection = QColor("#d9e8ef");
      c.chromeBackground = QColor("#fbfaf7");
      c.chromeText = QColor("#1f2328");
      c.chromeMuted = QColor("#6b665d");
      c.surface = QColor("#ffffff");
      c.canvas = QColor("#f4f1ea");
      c.border = QColor("#ded8cc");
      c.hover = QColor("#f1eee8");
      c.selected = QColor("#efe3ce");
      c.accent = QColor("#2f6f9f");
      c.isDark = false;
      add("newsprint", "Newsprint", c);
    }
    // night — dark.
    {
      ThemeColors c;
      c.background = QColor("#1f2328");
      c.text = QColor("#e6edf3");
      c.muted = QColor("#9aa4af");
      c.link = QColor("#7fb4f5");
      c.codeBackground = QColor("#2b3138");
      c.codeBorder = QColor("#3d444d");
      c.quoteBorder = QColor("#56616d");
      c.tableBorder = QColor("#3d444d");
      c.tableHeaderBackground = QColor("#303b4a");
      c.tableAlternateBackground = QColor("#242a31");
      c.highlight = QColor("#3a341a");
      c.selection = QColor("#264f78");
      c.chromeBackground = QColor("#1f2328");
      c.chromeText = QColor("#e6edf3");
      c.chromeMuted = QColor("#9aa4af");
      c.surface = QColor("#242a31");
      c.canvas = QColor("#1b1f24");
      c.border = QColor("#3d444d");
      c.hover = QColor("#2b3138");
      c.selected = QColor("#30363d");
      c.accent = QColor("#7fb4f5");
      c.isDark = true;
      add("night", "Night", c);
    }
    // pixyll — serif body on warm paper with a teal accent. Its identity is
    // typography (a serif body), so serifBody is set; the palette is warmed and
    // shifted to teal so it reads as clearly distinct from github at a glance.
    {
      ThemeColors c;
      c.background = QColor("#fafaf7");
      c.text = QColor("#2c2c2c");
      c.muted = QColor("#6a6a6a");
      c.link = QColor("#0e8a7a");
      c.codeBackground = QColor("#f0eee6");
      c.codeBorder = QColor("#ddd9cc");
      c.quoteBorder = QColor("#c2b280");
      c.tableBorder = QColor("#ddd9cc");
      c.tableHeaderBackground = QColor("#eef3ec");
      c.tableAlternateBackground = QColor("#f4f2ea");
      c.highlight = QColor("#fff8c5");
      c.selection = QColor("#cdeae3");
      c.chromeBackground = QColor("#fafaf7");
      c.chromeText = QColor("#2c2c2c");
      c.chromeMuted = QColor("#6a6a6a");
      c.surface = QColor("#ffffff");
      c.canvas = QColor("#f3f1e8");
      c.border = QColor("#ddd9cc");
      c.hover = QColor("#f0eee6");
      c.selected = QColor("#eef3ec");
      c.accent = QColor("#0e8a7a");
      c.serifBody = true;
      c.isDark = false;
      add("pixyll", "Pixyll", c);
    }
    // whitey — minimal / airy: noticeably lighter body text and faint chrome on
    // a near-white canvas, so it reads softer and less dense than github rather
    // than just "another dark-on-white" theme.
    {
      ThemeColors c;
      c.background = QColor("#fcfcfc");
      c.text = QColor("#4a4a4a");
      c.muted = QColor("#8a8a8a");
      c.link = QColor("#6a7d9a");
      c.codeBackground = QColor("#f4f4f4");
      c.codeBorder = QColor("#ececec");
      c.quoteBorder = QColor("#d4d4d4");
      c.tableBorder = QColor("#ececec");
      c.tableHeaderBackground = QColor("#f2f5f8");
      c.tableAlternateBackground = QColor("#f8f8f8");
      c.highlight = QColor("#fff8c5");
      c.selection = QColor("#e0e8f0");
      c.chromeBackground = QColor("#fcfcfc");
      c.chromeText = QColor("#4a4a4a");
      c.chromeMuted = QColor("#8a8a8a");
      c.surface = QColor("#ffffff");
      c.canvas = QColor("#f5f5f5");
      c.border = QColor("#ececec");
      c.hover = QColor("#f4f4f4");
      c.selected = QColor("#f2f5f8");
      c.accent = QColor("#6a7d9a");
      c.isDark = false;
      add("whitey", "Whitey", c);
    }
    return out;
  }();
  return kBuiltIns;
}

std::optional<ThemeDefinition> ThemeDefinition::builtIn(const QString& id) {
  for (const auto& d : builtIns()) {
    if (d.id == id) {
      return d;
    }
  }
  return std::nullopt;
}

}  // namespace muffin
