#include "theme/ThemeDefinition.h"

#include "theme/CssThemeMapper.h"
#include "theme/CssThemeParser.h"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

namespace muffin {

namespace {

QColor parseColor(const QJsonObject& colors, const char* key) {
  const QJsonValue v = colors.value(QLatin1String(key));
  if (!v.isString()) {
    return QColor();
  }
  QColor c = cssColor(v.toString());
  return c.isValid() ? c : QColor();
}

Qt::Alignment alignmentFromString(const QString& value) {
  const QString v = value.trimmed().toLower();
  if (v == QStringLiteral("left")) { return Qt::AlignLeft; }
  if (v == QStringLiteral("right")) { return Qt::AlignRight; }
  if (v == QStringLiteral("center")) { return Qt::AlignHCenter; }
  if (v == QStringLiteral("justify")) { return Qt::AlignJustify; }
  return Qt::Alignment();
}

QString alignmentToString(Qt::Alignment alignment) {
  if (alignment & Qt::AlignJustify) { return QStringLiteral("justify"); }
  if (alignment & Qt::AlignHCenter) { return QStringLiteral("center"); }
  if (alignment & Qt::AlignRight) { return QStringLiteral("right"); }
  if (alignment & Qt::AlignLeft) { return QStringLiteral("left"); }
  return QString();
}

// @font-face declared family name (lowercased) → the family name QFontDatabase
// actually registered the font under. CSS @font-face aliases a declared name to
// the font file regardless of the file's internal name; QFontDatabase registers
// only the internal name. So a theme that declares `font-family: CascadiaCode`
// (file Cascadia-Code-Regular.ttf, internal "Cascadia Code") or `"LXGW WenKai"`
// (internal Chinese name 霞鹜文楷) would NOT resolve without this map.
// RenderTheme's font-stack builder substitutes the declared name with the
// registered one (ThemeDefinition::fontFamilyAlias).
QHash<QString, QString>& fontFaceAliases() {
  static QHash<QString, QString> map;
  return map;
}

// Register every @font-face font declared in the sheet with QFontDatabase so the
// theme's font-family stacks resolve to the bundled typefaces (e.g. phycat's
// LXGW WenKai / CascadiaCode). Each font's srcPath is already absolute and
// resolved against its owning CSS file's dir (a font in an @import'd base
// resolves against that base). Process-global side-effect, guarded by a static
// path set so repeated loadDefinitions() calls (startup + every import) don't
// re-register the same file — mirrors MathFontRegistry's static-loaded guard.
// Must run before any render queries QFontDatabase::families(); ThemeManager
// runs fromCss (and thus this) at load time, ahead of painting.
void registerThemeFonts(const CssThemeSheet& sheet) {
  static QSet<QString> registered;
  for (const CssFontFace& ff : sheet.fontFaces()) {
    if (ff.srcPath.isEmpty() || registered.contains(ff.srcPath)) { continue; }
    if (!QFileInfo(ff.srcPath).isFile()) {
      // A silently-skipped @font-face (wrong path, file not installed) makes the
      // theme fall back to a heavier system typeface — the most common reason a
      // theme's text looks "blacker/sharper" than in Typora. Surface it.
      qWarning("Muffin theme: @font-face file not found for declared family \"%s\": %s",
               qPrintable(ff.family), qPrintable(ff.srcPath));
      registered.insert(ff.srcPath);
      continue;
    }
    const int id = QFontDatabase::addApplicationFont(ff.srcPath);
    registered.insert(ff.srcPath);
    if (id < 0) {
      qWarning("Muffin theme: QFontDatabase::addApplicationFont failed for declared family \"%s\": %s",
               qPrintable(ff.family), qPrintable(ff.srcPath));
      continue;
    }
    // Map the @font-face declared name to the family name Qt actually registered
    // (the font's internal name, which often differs from the declared alias).
    const QStringList fams = QFontDatabase::applicationFontFamilies(id);
    if (!fams.isEmpty()) {
      fontFaceAliases().insert(ff.family.toLower(), fams.first());
      qWarning("Muffin theme: registered @font-face family \"%s\" -> \"%s\" (%s)",
               qPrintable(ff.family), qPrintable(fams.first()), qPrintable(ff.srcPath));
    }
  }
}

}  // namespace

bool ThemeDefinition::valid() const {
  // Text is the one essential token: a theme with no readable text colour is
  // unusable. A missing background is legitimate (a CSS theme may leave the
  // viewport to a browser-like default) and is synthesised by the CSS loader /
  // derived for chrome, so it is not a validity gate. Chrome fields all fall
  // back to document colours, so they need not be present either.
  return colors.text.isValid();
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
  k.codeBlockBackground = parseColor(c, "codeBlockBackground");
  k.headingAccentColor = parseColor(c, "headingAccentColor");
  k.blockquoteBackground = parseColor(c, "blockquoteBackground");
  k.chromeBackground = parseColor(c, "chromeBackground");
  k.chromeText = parseColor(c, "chromeText");
  k.chromeMuted = parseColor(c, "chromeMuted");
  k.chromeDisabled = parseColor(c, "chromeDisabled");
  k.surface = parseColor(c, "surface");
  k.canvas = parseColor(c, "canvas");
  k.border = parseColor(c, "border");
  k.hover = parseColor(c, "hover");
  k.selected = parseColor(c, "selected");
  k.accent = parseColor(c, "accent");
  k.serifBody = c.value(QStringLiteral("serifBody")).toBool(false);

  // Optional typography block (Muffin-native JSON themes; CSS themes go
  // through fromCss instead). Absent → all fields stay default/empty.
  const QJsonObject t = json.value(QStringLiteral("typography")).toObject();
  if (!t.isEmpty()) {
    ThemeTypography& ty = d.typography;
    ty.bodyFont = t.value(QStringLiteral("bodyFont")).toString();
    ty.headingFont = t.value(QStringLiteral("headingFont")).toString();
    ty.codeFont = t.value(QStringLiteral("codeFont")).toString();
    ty.mathFont = t.value(QStringLiteral("mathFont")).toString();
    ty.bodySizePt = t.value(QStringLiteral("bodySizePt")).toDouble(0.0);
    ty.lineHeight = t.value(QStringLiteral("lineHeight")).toDouble(0.0);
    const QJsonArray sizes = t.value(QStringLiteral("headingSizePt")).toArray();
    for (int i = 0; i < 6 && i < sizes.size(); ++i) {
      ty.headingSizePt[i] = sizes.at(i).toDouble(0.0);
    }
    const QJsonArray hcols = t.value(QStringLiteral("headingColor")).toArray();
    for (int i = 0; i < 6 && i < hcols.size(); ++i) {
      QColor hc(hcols.at(i).toString());
      if (hc.isValid()) { ty.headingColor[i] = hc; }
    }
    ty.bodyAlignment = alignmentFromString(t.value(QStringLiteral("bodyAlignment")).toString());
    const QJsonArray aligns = t.value(QStringLiteral("headingAlignment")).toArray();
    for (int i = 0; i < 6 && i < aligns.size(); ++i) {
      ty.headingAlignment[i] = alignmentFromString(aligns.at(i).toString());
    }
    const QJsonArray weights = t.value(QStringLiteral("headingFontWeight")).toArray();
    for (int i = 0; i < 6 && i < weights.size(); ++i) {
      if (weights.at(i).isDouble()) {
        ty.headingFontWeight[i] = weights.at(i).toInt();
        ty.headingFontWeightSet[i] = true;
      }
    }
    const QJsonArray italics = t.value(QStringLiteral("headingItalic")).toArray();
    for (int i = 0; i < 6 && i < italics.size(); ++i) {
      if (italics.at(i).isBool()) {
        ty.headingItalic[i] = italics.at(i).toBool();
        ty.headingItalicSet[i] = true;
      }
    }
  }

  // Derive chrome defaults from the document palette when a theme file omits
  // them, so a minimal (document-colours-only) JSON still renders sanely.
  deriveChromeDefaults(k);
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
  put("codeBlockBackground", colors.codeBlockBackground);
  put("headingAccentColor", colors.headingAccentColor);
  put("blockquoteBackground", colors.blockquoteBackground);
  put("chromeBackground", colors.chromeBackground);
  put("chromeText", colors.chromeText);
  put("chromeMuted", colors.chromeMuted);
  put("chromeDisabled", colors.chromeDisabled);
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

  // Only emit typography when the theme actually sets any of it, so legacy
  // themes round-trip unchanged.
  const ThemeTypography& ty = typography;
  bool anyHeadingTypography = false;
  for (int i = 0; i < 6; ++i) {
    anyHeadingTypography = anyHeadingTypography || ty.headingAlignment[i] != Qt::Alignment() ||
                           ty.headingFontWeightSet[i] || ty.headingItalicSet[i];
  }
  const bool hasTypo = !(ty.bodyFont.isEmpty() && ty.headingFont.isEmpty() &&
                         ty.codeFont.isEmpty() && ty.mathFont.isEmpty() &&
                         ty.bodySizePt == 0.0 && ty.lineHeight == 0.0 &&
                         ty.bodyAlignment == Qt::Alignment() && !anyHeadingTypography);
  if (hasTypo) {
    QJsonObject t;
    if (!ty.bodyFont.isEmpty()) t.insert(QStringLiteral("bodyFont"), ty.bodyFont);
    if (!ty.headingFont.isEmpty()) t.insert(QStringLiteral("headingFont"), ty.headingFont);
    if (!ty.codeFont.isEmpty()) t.insert(QStringLiteral("codeFont"), ty.codeFont);
    if (!ty.mathFont.isEmpty()) t.insert(QStringLiteral("mathFont"), ty.mathFont);
    if (ty.bodySizePt != 0.0) t.insert(QStringLiteral("bodySizePt"), ty.bodySizePt);
    if (ty.lineHeight != 0.0) t.insert(QStringLiteral("lineHeight"), ty.lineHeight);
    QJsonArray sizes;
    bool anySize = false;
    for (int i = 0; i < 6; ++i) {
      sizes.append(ty.headingSizePt[i]);
      if (ty.headingSizePt[i] != 0.0) anySize = true;
    }
    if (anySize) t.insert(QStringLiteral("headingSizePt"), sizes);
    QJsonArray hcols;
    bool anyHcol = false;
    for (int i = 0; i < 6; ++i) {
      if (ty.headingColor[i].isValid()) {
        hcols.append(ty.headingColor[i].name(QColor::HexArgb));
        anyHcol = true;
      } else {
        hcols.append(QJsonValue::Null);
      }
    }
    if (anyHcol) t.insert(QStringLiteral("headingColor"), hcols);
    const QString bodyAlignment = alignmentToString(ty.bodyAlignment);
    if (!bodyAlignment.isEmpty()) { t.insert(QStringLiteral("bodyAlignment"), bodyAlignment); }
    QJsonArray aligns;
    bool anyAlign = false;
    for (int i = 0; i < 6; ++i) {
      const QString a = alignmentToString(ty.headingAlignment[i]);
      if (!a.isEmpty()) {
        aligns.append(a);
        anyAlign = true;
      } else {
        aligns.append(QJsonValue::Null);
      }
    }
    if (anyAlign) { t.insert(QStringLiteral("headingAlignment"), aligns); }
    QJsonArray weights;
    bool anyWeight = false;
    for (int i = 0; i < 6; ++i) {
      if (ty.headingFontWeightSet[i]) {
        weights.append(ty.headingFontWeight[i]);
        anyWeight = true;
      } else {
        weights.append(QJsonValue::Null);
      }
    }
    if (anyWeight) { t.insert(QStringLiteral("headingFontWeight"), weights); }
    QJsonArray italics;
    bool anyItalic = false;
    for (int i = 0; i < 6; ++i) {
      if (ty.headingItalicSet[i]) {
        italics.append(ty.headingItalic[i]);
        anyItalic = true;
      } else {
        italics.append(QJsonValue::Null);
      }
    }
    if (anyItalic) { t.insert(QStringLiteral("headingItalic"), italics); }
    root.insert(QStringLiteral("typography"), t);
  }
  return root;
}

void ThemeDefinition::deriveChromeDefaults(ThemeColors& k) {
  // Derive chrome defaults from the document palette when a theme omits them, so
  // a minimal (document-colours-only) theme still renders sane chrome. Shared by
  // fromJson and the CSS mapper. isDark is intentionally NOT set here.
  if (!k.chromeBackground.isValid()) k.chromeBackground = k.background;
  if (!k.chromeText.isValid()) k.chromeText = k.text;
  if (!k.chromeMuted.isValid()) k.chromeMuted = k.muted;
  // Disabled/ghosted text + controls (menu items, dialog buttons/inputs/…).
  // Distinct from chromeMuted, which is ACTIVE secondary text (toolbar/sidebar/
  // scrollbar): muted needs only a gentle de-emphasis, disabled must read as
  // clearly unavailable. Reusing chromeMuted for both (the old design) left
  // disabled only ~1 step above the ink on light themes (e.g. GitHub #333 ink
  // → #515 "disabled") — indistinguishable from enabled. Fade the body ink
  // halfway toward the page background instead: a strong ghosting cue that
  // needs no isDark branch (blending toward the bg fades correctly on light
  // AND dark pages, so this is safe to derive before isDark is set).
  if (!k.chromeDisabled.isValid()) {
    const QColor ink = k.text.isValid() ? k.text : QColor(0x33, 0x33, 0x33);
    const QColor bg = k.background.isValid() ? k.background : QColor(0xff, 0xff, 0xff);
    k.chromeDisabled = QColor(int(ink.red() + (bg.red() - ink.red()) * 0.5),
                              int(ink.green() + (bg.green() - ink.green()) * 0.5),
                              int(ink.blue() + (bg.blue() - ink.blue()) * 0.5));
  }
  if (!k.surface.isValid()) k.surface = k.background;
  // canvas = the tone behind cards. A minimal theme usually omits it, so derive
  // a subtle step off the base tones for depth (lighter themes get a faint gray
  // canvas; darker themes get a marginally darker one) rather than going flat.
  if (!k.canvas.isValid()) {
    const QColor base = k.chromeBackground.isValid() ? k.chromeBackground : k.background;
    k.canvas = base.lightness() < 128 ? base.darker(112) : k.surface.darker(104);
  }
  if (!k.border.isValid()) {
    // Chrome hairline (splitter, menu/status-bar separators, control outlines).
    // This is a UI-delimiter job: it needs ENOUGH contrast against the chrome
    // background to read. codeBorder is a DIFFERENT job — theme authors tune it
    // deliberately faint (e.g. github #e7eaed) so `code` blends into prose.
    // Reusing codeBorder here made every control/menu/splitter edge inherit a
    // prose-blend tone, washing borders out on light themes (the "preferences
    // inputs look too pale under github" report). Derive the hairline
    // independently with a stronger step so it reads on every theme; codeBorder
    // (and thus code-fence outlines) stays exactly as the theme declared it.
    const QColor base = k.chromeBackground.isValid() ? k.chromeBackground : k.background;
    k.border = base.lightness() < 128 ? base.lighter(140) : base.darker(125);
  }
  // Code background — the FILL of inline code spans / code fences / math+HTML
  // block bodies. A theme that declares no `background` on `code` / `.md-fences`
  // (pixyll: `code { color:#7a7a7a }` only; variable-only themes) leaves this
  // invalid. InlineLayout::paintTextLayoutCodeSpans sets a QBrush from it, and Qt
  // paints an unset brush as solid black — the black chip behind every inline code
  // span. The hover / selected tokens below fall back to it too, so an invalid
  // codeBackground ALSO turns menu-item hover, tool-button hover and sidebar-row
  // selection black (via hexRgb's invalid → #000000). Derive a subtle step off the
  // page background so the chip is always visible-but-soft. Runs BEFORE codeBorder
  // so the border derives from a real code background, not the page background.
  if (!k.codeBackground.isValid()) {
    const QColor base = k.background.isValid() ? k.background : QColor(0xff, 0xff, 0xff);
    k.codeBackground = base.lightness() < 128 ? base.lighter(125) : base.darker(106);
  }
  // Code-block background follows the inline code background (the RenderTheme
  // getter already falls back this way; centralize it so ThemeDefinition is sane).
  if (!k.codeBlockBackground.isValid()) { k.codeBlockBackground = k.codeBackground; }
  // Code border — inline code spans, code fences, math/HTML block outlines, plus
  // the soft UI lines that borrow it (thematic-break rule, code-fence scrollbar
  // track, heading badge). A theme that declares no `border` on `code` /
  // `.md-fences` leaves this invalid, and Qt renders an unset QPen/QBrush as
  // solid black — the black box around every inline code span on Night/Pixyll/
  // Newsprint/Whitey. Derive a subtle edge off the code background (one step
  // darker on light pages, lighter on dark) so the outline stays
  // visible-but-soft on every theme. Themes that DO declare a border (e.g.
  // github #e7eaed) keep their explicit colour. This runs AFTER the chrome-
  // hairline block above, so the hairline still sees the original (possibly
  // invalid) codeBorder before this fallback fills it in.
  if (!k.codeBorder.isValid()) {
    const QColor base = k.codeBackground;  // now always valid (filled above)
    k.codeBorder = base.lightness() < 128 ? base.lighter(140) : base.darker(112);
  }
  if (!k.hover.isValid()) k.hover = k.codeBackground;
  if (!k.selected.isValid()) k.selected = k.codeBackground;
  if (!k.accent.isValid()) k.accent = k.link;
  // Highlight (==mark==) background — most themes declare no `mark`, leaving it
  // invalid (the `==text==` fill, and BlockLayoutBuilder's HTML palette, both
  // paint an unset brush as solid black). Default to a soft amber (dark amber on
  // dark pages). isDark is not set here, so derive from the page luminance.
  if (!k.highlight.isValid()) {
    const QColor base = k.background.isValid() ? k.background : QColor(0xff, 0xff, 0xff);
    k.highlight = base.lightness() < 128 ? QColor(0x6a, 0x53, 0x16) : QColor(0xff, 0xf1, 0x73);
  }
  // Text-selection background — a theme with no ::selection leaves it invalid and
  // the source-mode selection paints black. Default to a soft accent blue.
  if (!k.selection.isValid()) {
    k.selection = k.accent.isValid() ? k.accent : (k.link.isValid() ? k.link : QColor(0x3b, 0x82, 0xf6));
  }
}

ThemeDefinition ThemeDefinition::fromCss(const QString& cssPath, const QString& id) {
  QFile f(cssPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return ThemeDefinition{};  // default-constructed → invalid (no background/text)
  }
  const QString text = QString::fromUtf8(f.readAll());
  f.close();
  // @import urls resolve relative to the CSS file's directory. Works for both
  // filesystem paths and :/resource paths (QFile/QDir handle both).
  const QString baseDir = QFileInfo(cssPath).absolutePath();
  const CssThemeSheet sheet = CssThemeParser::parse(text, baseDir);
  // Register @font-face fonts before translation so the font-family stacks the
  // mapper reads are backed by registered typefaces by the time anything paints.
  registerThemeFonts(sheet);
  return CssThemeMapper::fromSheet(sheet, id);
}

QString ThemeDefinition::fontFamilyAlias(const QString& declaredName) {
  // Resolve a CSS @font-face declared family name to the family name QFontDatabase
  // registered (the font file's internal name), or empty if it isn't an @font-face
  // alias. See fontFaceAliases() for why this mapping is necessary.
  if (declaredName.isEmpty()) { return {}; }
  const auto& map = fontFaceAliases();
  const auto it = map.constFind(declaredName.toLower());
  return it != map.constEnd() ? it.value() : QString();
}

const std::vector<ThemeDefinition>& ThemeDefinition::builtIns() {
  // Built-ins are authored as community-CSS CSS at :/themes/<id>.css and
  // loaded through the SAME fromCss path as user themes — one format, one
  // code path. The display order + labels are fixed here; the colours live in
  // the CSS. testFromDefinitionReproducesBuiltIns guards that each CSS built-in
  // still reproduces the matching RenderTheme factory bit-for-bit.
  static const std::vector<ThemeDefinition> kBuiltIns = [] {
    struct Spec { const char* id; const char* label; };
    static constexpr Spec specs[] = {
        {"github", "GitHub"}, {"newsprint", "Newsprint"}, {"night", "Night"},
        {"pixyll", "Pixyll"}, {"whitey", "Whitey"},
    };
    std::vector<ThemeDefinition> out;
    for (const Spec& s : specs) {
      const QString path = QStringLiteral(":/themes/%1.css").arg(QString::fromLatin1(s.id));
      ThemeDefinition d = ThemeDefinition::fromCss(path, QString::fromLatin1(s.id));
      d.id = QString::fromLatin1(s.id);
      d.label = QString::fromLatin1(s.label);
      d.isBuiltIn = true;
      if (d.valid()) { out.push_back(std::move(d)); }
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
