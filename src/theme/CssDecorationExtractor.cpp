#include "theme/CssDecorationExtractor.h"

#include "theme/CssContent.h"
#include "theme/CssFlatDecl.h"
#include "theme/CssSelectorAnalysis.h"
#include "theme/CssThemeParser.h"
#include "theme/CssValueParser.h"
#include "theme/ThemeDefinition.h"

#include <QColor>
#include <QHash>
#include <QMarginsF>
#include <QRegularExpression>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <vector>

namespace muffin {

namespace {

// Cascade candidate for bestValue(): a (value, important, specificity, source-order) tuple
// compared by importance → specificity → order (last wins on ties, mirroring CSS).
struct Candidate {
  QString rawValue;
  bool important = false;
  int spec = 0;
  int order = 0;
};
bool beats(const Candidate& a, const Candidate& b) {
  if (a.important != b.important) { return a.important; }
  if (a.spec != b.spec) { return a.spec > b.spec; }
  return a.order > b.order;
}

// --- data: URI decoding (for url(data:image/svg+xml,…) pseudo-element icons) ---

QByteArray decodePercentEncoding(const QString& s) {
  QByteArray out;
  for (int i = 0; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if (c == QLatin1Char('%') && i + 2 < s.size()) {
      bool ok = false;
      const int b = s.mid(i + 1, 2).toInt(&ok, 16);
      if (ok) { out.append(static_cast<char>(b)); i += 2; continue; }
    }
    out.append(c.toLatin1());
  }
  return out;
}

QByteArray extractDataUri(const QString& value) {
  if (value.isEmpty()) { return QByteArray(); }
  static const QRegularExpression re(QStringLiteral("url\\(\\s*['\"]?(data:[^)]*)['\"]?\\s*\\)"),
                                     QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(value);
  if (!m.hasMatch()) { return QByteArray(); }
  const QString dataUri = m.captured(1);
  const int comma = dataUri.indexOf(QLatin1Char(','));
  if (comma < 0) { return QByteArray(); }
  const QString meta = dataUri.mid(5, comma - 5).toLower();  // "image/svg+xml;base64" etc.
  const QString payload = dataUri.mid(comma + 1);
  if (meta.contains(QStringLiteral("base64"))) { return QByteArray::fromBase64(payload.toUtf8()); }
  return decodePercentEncoding(payload);
}

qreal opacityValue(const QString& raw, const QHash<QString, QString>& vars) {
  const QString resolved = CssThemeParser::resolveVars(raw, vars).trimmed();
  bool ok = false;
  const qreal v = resolved.toDouble(&ok);
  return ok ? qBound(0.0, v, 1.0) : 1.0;
}

// Minimal `calc(100% - <len>)` parser for a li::before guide-line height, where
// the line spans the item minus a fixed inset (phycat's `height: calc(100% - 45px)`
// ⇒ bottom inset 45px). Returns the inset in px; any other calc/% form ⇒ 0
// (line spans the full item). Full calc() is out of scope for this one use.
qreal parseCalcPercentMinusPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx) {
  const QString v = CssThemeParser::resolveVars(value, vars).trimmed();
  static const QRegularExpression re(QStringLiteral("calc\\(\\s*100%\\s*-\\s*([0-9.]+)(px|em|rem)?\\s*\\)"),
                                     QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(v);
  if (!m.hasMatch()) { return 0.0; }
  bool ok = false;
  qreal n = m.captured(1).toDouble(&ok);
  if (!ok) { return 0.0; }
  const QString unit = m.captured(2).toLower();
  if (unit == QStringLiteral("em") || unit == QStringLiteral("rem")) { n *= emPx; }
  return n;
}

qreal transitionMs(const QString& raw) {
  // Match a duration with optional leading dot (.3s, 0.3s, 300ms, 2s).
  static const QRegularExpression re(QStringLiteral("(\\d*\\.?\\d+)(s|ms)"));
  const QRegularExpressionMatch m = re.match(raw);
  if (!m.hasMatch()) { return 0.0; }
  const qreal v = m.captured(1).toDouble();
  return m.captured(2) == QStringLiteral("s") ? v * 1000.0 : v;
}

}  // namespace

// --- flat-list query primitives (declared in CssFlatDecl.h) ------------------

// Best raw (un-resolved) value among declarations matching one of `properties`
// whose selector satisfies `target`. Returns empty if none.
QString bestValue(const std::vector<FlatDecl>& flat, const std::vector<QString>& properties,
                  const std::function<bool(const SelInfo&)>& target) {
  Candidate best;
  bool have = false;
  for (const FlatDecl& fd : flat) {
    if (!target(fd.info)) { continue; }
    if (std::find(properties.begin(), properties.end(), fd.property) == properties.end()) { continue; }
    Candidate c{fd.value, fd.important, fd.spec, fd.order};
    if (!have || beats(c, best)) { best = c; have = true; }
  }
  return have ? best.rawValue : QString();
}

QColor colorToken(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars,
                  const std::vector<QString>& properties, const std::function<bool(const SelInfo&)>& target) {
  return extractColor(bestValue(flat, properties, target), vars);
}

// --- ::before/::after capture → PseudoElementRule ----------------------------
// flatten() keeps pseudo-element rules; group them by host and resolve each into
// a paint recipe. Host = "#write" | tag | ".md-fences" (skip when unanchored).

std::vector<FlatDecl> filterPseudoFlat(const std::vector<FlatDecl>& flat, const QString& host, const QString& pseudo) {
  std::vector<FlatDecl> out;
  for (const FlatDecl& fd : flat) {
    if (fd.info.pseudoElement != pseudo) { continue; }
    if (pseudoHostKey(fd.info) != host) { continue; }
    out.push_back(fd);
  }
  return out;
}

// Nested-list guide line from a `li::before { border-left: …; left; top; height:
// calc(100% - Npx) }` rule. phycat draws the per-item vertical tree line this way;
// it is a list decoration rather than a generic pseudo marker, so it gets its own
// model. present ⇒ the theme styled it (valid colour + positive width).
ListGuide extractListGuide(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars, qreal bodyPx) {
  ListGuide g;
  const std::vector<FlatDecl> sub = filterPseudoFlat(flat, QStringLiteral("li"), QStringLiteral("before"));
  if (sub.empty()) { return g; }
  const auto allPred = [](const SelInfo&) { return true; };
  const QString blColorRaw = bestValue(sub, {QStringLiteral("border-left-color"), QStringLiteral("border-left"),
                                              QStringLiteral("border-color"), QStringLiteral("border")}, allPred);
  const QString blWidthRaw = bestValue(sub, {QStringLiteral("border-left-width"), QStringLiteral("border-left"),
                                              QStringLiteral("border-width"), QStringLiteral("border")}, allPred);
  g.color = extractColor(blColorRaw, vars);
  g.width = borderWidthPx(blWidthRaw, vars, bodyPx);
  g.leftOffset = lengthToPx(bestValue(sub, {QStringLiteral("left")}, allPred), vars, bodyPx);
  const QString topRaw = bestValue(sub, {QStringLiteral("top")}, allPred);
  if (!topRaw.isEmpty()) { g.topInset = lengthToPx(topRaw, vars, bodyPx); }
  g.bottomInset = parseCalcPercentMinusPx(bestValue(sub, {QStringLiteral("height")}, allPred), vars, bodyPx);
  g.present = g.color.isValid() && g.width > 0.0;
  return g;
}

std::vector<PseudoElementRule> extractPseudoRules(const std::vector<FlatDecl>& flat,
                                                   const QHash<QString, QString>& vars,
                                                   const std::function<qreal(const QString&)>& emPxForHost) {
  struct Key { QString host; QString pseudo; };
  std::vector<Key> keys;
  const auto seen = [&](const QString& h, const QString& p) {
    for (const Key& k : keys) { if (k.host == h && k.pseudo == p) { return true; } }
    return false;
  };
  for (const FlatDecl& fd : flat) {
    if (fd.info.pseudoElement != QStringLiteral("before") && fd.info.pseudoElement != QStringLiteral("after")) { continue; }
    const QString h = pseudoHostKey(fd.info);
    if (h.isEmpty()) { continue; }
    // `li::before` is the nested-list guide-line channel, consumed exclusively by
    // extractListGuide → decorations.listGuide. No renderer paints a generic `li`
    // pseudo (paintPseudoDecorations is only called for headings/blockquote), so
    // emitting one here is dead data — skip it. (`li::after` is untouched.)
    if (h == QStringLiteral("li") && fd.info.pseudoElement == QStringLiteral("before")) { continue; }
    if (!seen(h, fd.info.pseudoElement)) { keys.push_back({h, fd.info.pseudoElement}); }
  }
  std::vector<PseudoElementRule> out;
  const auto allPred = [](const SelInfo&) { return true; };
  for (const Key& k : keys) {
    const std::vector<FlatDecl> sub = filterPseudoFlat(flat, k.host, k.pseudo);
    if (sub.empty()) { continue; }
    const qreal emPx = emPxForHost(k.host);
    PseudoElementRule rule;
    rule.host = k.host;
    rule.pseudo = k.pseudo;
    rule.present = true;
    const QString contentRaw = bestValue(sub, {QStringLiteral("content")}, allPred);
    QString content = CssThemeParser::resolveVars(contentRaw, vars).trimmed();
    if (content.size() >= 2 && ((content.front() == QLatin1Char('"') && content.back() == QLatin1Char('"')) ||
                                (content.front() == QLatin1Char('\'') && content.back() == QLatin1Char('\'')))) {
      content = content.mid(1, content.size() - 2);
    }
    // CSS `content: none` / `normal` on ::before/::after means "no generated content"
    // (suppress the pseudo), NOT the literal text "none". newsprint declares
    // `blockquote:before { content: ''; content: none }` and bestValue picks the later
    // `none`; without this it rendered the word "none" before every blockquote. Mirrors
    // the listMarkerContent none/normal guard below.
    if (content.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
        content.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0) {
      content.clear();
    }
    rule.content = content;
    // Tokenize so a heading ::before content like `counter(h1) ". "` can be
    // resolved against live counter state at layout time. Pure-literal content
    // yields an all-Literal vector (the painter still draws `rule.content` when
    // no counter token is present — see DecorationPainter's resolved-text branch).
    rule.contentTokens = parseContentTokens(content);
    rule.color = colorToken(sub, vars, {QStringLiteral("color")}, allPred);
    rule.backgroundColor = colorToken(sub, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, allPred);
    const QString bgImg = bestValue(sub, {QStringLiteral("background-image"), QStringLiteral("background")}, allPred);
    rule.background = parseGradientSpec(bgImg, vars);
    const QString maskImg = bestValue(sub, {QStringLiteral("mask-image"), QStringLiteral("-webkit-mask-image"),
                                             QStringLiteral("mask"), QStringLiteral("-webkit-mask")}, allPred);
    rule.maskPattern = parseGradientSpec(maskImg, vars);
    rule.maskTint = rule.backgroundColor.isValid() ? rule.backgroundColor
                                                   : colorToken(sub, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, allPred);
    const QString opacityRaw = bestValue(sub, {QStringLiteral("opacity")}, allPred);
    if (!opacityRaw.isEmpty()) { rule.opacity = opacityValue(opacityRaw, vars); }
    const QString maskSize = bestValue(sub, {QStringLiteral("mask-size"), QStringLiteral("-webkit-mask-size")}, allPred);
    if (!maskSize.isEmpty()) {
      const QStringList ms = splitTopLevelSpaces(CssThemeParser::resolveVars(maskSize, vars));
      if (!ms.isEmpty()) {
        const qreal w = lengthToPx(ms.at(0), vars, emPx);
        const qreal h = ms.size() > 1 ? lengthToPx(ms.at(1), vars, emPx) : w;
        if (w > 0 && h > 0) { rule.maskTile = QSizeF(w, h); }
      }
    }
    // Keep the var-resolved raw width/height so the painter can resolve a `%`
    // against the host box later (map-time `lengthToPx` is em-relative, which is
    // wrong for box-relative `%` like phycat's `h3::before { height: 61% }`).
    const QString widthRaw = bestValue(sub, {QStringLiteral("width")}, allPred);
    const QString heightRaw = bestValue(sub, {QStringLiteral("height")}, allPred);
    rule.sizeRawWidth = CssThemeParser::resolveVars(widthRaw, vars).trimmed();
    rule.sizeRawHeight = CssThemeParser::resolveVars(heightRaw, vars).trimmed();
    const qreal w = lengthToPx(widthRaw, vars, emPx);
    const qreal h = lengthToPx(heightRaw, vars, emPx);
    if (w > 0 || h > 0) { rule.size = QSizeF(w, h); }
    // Phase 2b geometry: position/border-radius/outline-border/margin for
    // heading ::before/::after markers (phycat h3 left bar, h4/h5 discs, etc.).
    const QString posRaw = CssThemeParser::resolveVars(bestValue(sub, {QStringLiteral("position")}, allPred), vars).trimmed().toLower();
    rule.absolute = (posRaw == QStringLiteral("absolute"));
    rule.insets.setLeft(lengthToPx(bestValue(sub, {QStringLiteral("left")}, allPred), vars, emPx));
    // `top` carries its own sentinel slot so the painter can tell an explicit
    // top:0 from "no top declared" (QMarginsF defaults every side to 0). `font-size`
    // drives pseudo text size (blockquote ✨ at font-size:20px); 0 ⇒ inherit host font.
    const QString topRaw = bestValue(sub, {QStringLiteral("top")}, allPred);
    if (!topRaw.isEmpty()) { rule.insetsTop = lengthToPx(topRaw, vars, emPx); }
    rule.fontSizePx = lengthToPx(bestValue(sub, {QStringLiteral("font-size")}, allPred), vars, emPx);
    rule.borderRadius = lengthToPx(bestValue(sub, {QStringLiteral("border-radius")}, allPred), vars, emPx);
    const QString bord = bestValue(sub, {QStringLiteral("border"), QStringLiteral("border-color")}, allPred);
    rule.borderColor = extractColor(bord, vars);
    rule.borderWidth = borderWidthPx(bord, vars, emPx);
    const QMarginsF pmargin = boxToMarginsPx(bestValue(sub, {QStringLiteral("margin")}, allPred), vars, emPx);
    qreal marginLeft = pmargin.left();
    qreal marginRight = pmargin.right();
    const QString mright = bestValue(sub, {QStringLiteral("margin-right")}, allPred);
    if (!mright.isEmpty()) { marginRight = lengthToPx(mright, vars, emPx); }
    const QString mleft = bestValue(sub, {QStringLiteral("margin-left")}, allPred);
    if (!mleft.isEmpty()) { marginLeft = lengthToPx(mleft, vars, emPx); }
    rule.marginLeft = marginLeft;
    rule.marginRight = marginRight;
    const QString bb = bestValue(sub,
        {QStringLiteral("border-bottom"), QStringLiteral("border-bottom-color"),
         QStringLiteral("border-color"), QStringLiteral("border")}, allPred);
    rule.borderBottomColor = extractColor(bb, vars);
    rule.borderBottomWidth = borderWidthPx(bb, vars, emPx);
    const QByteArray contentSvg = extractDataUri(CssThemeParser::resolveVars(contentRaw, vars));
    const QByteArray bgSvg = extractDataUri(CssThemeParser::resolveVars(bgImg, vars));
    const QByteArray maskSvg = extractDataUri(CssThemeParser::resolveVars(maskImg, vars));
    rule.svgData = !contentSvg.isEmpty() ? contentSvg : (!bgSvg.isEmpty() ? bgSvg : maskSvg);
    // An icon sourced from `mask:` is an alpha shape tinted with the
    // background-color (e.g. phycat's link ::before); render recoloured, not as-is.
    rule.svgFromMask = !maskSvg.isEmpty() && rule.svgData == maskSvg;
    out.push_back(std::move(rule));
  }
  return out;
}

// Host element OWN background-image gradients (not pseudos). Only gradients are
// captured here — solid background-colours remain on the existing theme tokens
// (codeBackground/highlight/blockquoteBackground/…) so they aren't double-painted.
std::vector<ElementBackground> extractElementBackgrounds(const std::vector<FlatDecl>& flat,
                                                          const QHash<QString, QString>& vars,
                                                          qreal emPx) {
  static const std::vector<QString> hosts = {
      QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"),
      QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"), QStringLiteral("hr"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("em"),
      QStringLiteral("a"), QStringLiteral("li")};
  std::vector<ElementBackground> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && !s.focus && !s.active && !s.visited && !s.mdFocus && s.tag == host;
    };
    const QString bgImg = bestValue(flat, {QStringLiteral("background-image"), QStringLiteral("background")}, pred);
    const GradientSpec grad = parseGradientSpec(bgImg, vars);
    // Phase 2c: a host may carry a rounded pill / top hairline WITHOUT a gradient
    // (e.g. a heading with only `border-top` or `border-radius`). Capture the box
    // decorations before gating so such hosts still produce an entry; only skip
    // when there is nothing decorative at all (preserves the built-in "no host
    // gradient → no entry" contract, since built-ins use border-bottom, not
    // border-top/border-radius, on these hosts).
    const qreal borderRadius = lengthToPx(bestValue(flat, {QStringLiteral("border-radius")}, pred), vars, emPx);
    const QString bt = bestValue(flat, {QStringLiteral("border-top"), QStringLiteral("border-top-color")}, pred);
    const QColor borderTopColor = extractColor(bt, vars);
    const qreal borderTopWidth = borderWidthPx(bt, vars, emPx);
    const bool hasBoxDecoration = borderRadius > 0.0 || (borderTopColor.isValid() && borderTopWidth > 0.0);
    if (grad.kind == GradientSpec::Kind::None && !hasBoxDecoration) { continue; }
    ElementBackground eb;
    eb.host = host;
    eb.gradient = grad;
    eb.color = colorToken(flat, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, pred);
    eb.opacity = 1.0;
    eb.borderRadius = borderRadius;
    eb.borderTopColor = borderTopColor;
    eb.borderTopWidth = borderTopWidth;
    eb.present = true;
    out.push_back(std::move(eb));
  }
  return out;
}

// Tractable :hover subset: box-shadow glow (colour + blur) + background tint.
std::vector<HoverEffect> extractHoverEffects(const std::vector<FlatDecl>& flatHover, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<HoverEffect> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) { return s.tag == host; };
    HoverEffect he;
    he.host = host;
    const QString shadow = bestValue(flatHover, {QStringLiteral("box-shadow")}, pred);
    if (!shadow.isEmpty() && !shadow.contains(QStringLiteral("none"))) {
      he.glowColor = extractColor(shadow, vars);
      he.glowBlur = shadowBlurPx(shadow, vars);
    }
    he.bgTint = colorToken(flatHover, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, pred);
    if (!he.glowColor.isValid() && !he.bgTint.isValid()) { continue; }
    he.present = true;
    out.push_back(std::move(he));
  }
  return out;
}

std::vector<TransitionSpec> extractTransitions(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<TransitionSpec> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && s.tag == host;
    };
    const QString raw = bestValue(flat, {QStringLiteral("transition")}, pred);
    if (raw.isEmpty()) { continue; }
    const qreal ms = transitionMs(CssThemeParser::resolveVars(raw, vars));
    if (ms <= 0.0) { continue; }
    TransitionSpec ts;
    ts.host = host;
    ts.durationMs = ms;
    out.push_back(std::move(ts));
  }
  return out;
}

std::vector<KeyframesDef> extractKeyframes(const CssThemeSheet& sheet, const QHash<QString, QString>& vars) {
  // var() is resolved here (once, at load) so the runtime sampler needs no
  // variable table — ThemeDefinition/RenderTheme don't carry one.
  std::vector<KeyframesDef> out;
  for (const CssKeyframes& k : sheet.keyframes()) {
    KeyframesDef def;
    def.name = k.name;
    for (const CssKeyframeStop& s : k.stops) {
      KeyframeStop st;
      st.position = s.position;
      for (const CssDeclaration& d : s.declarations) {
        st.declarations.insert(d.property, CssThemeParser::resolveVars(d.value, vars));
      }
      def.stops.push_back(std::move(st));
    }
    out.push_back(std::move(def));
  }
  return out;
}

// Parse an `animation:` shorthand into an AnimationDef. Heuristic token walk:
// the name is the leftover identifier; times → duration (then delay); keywords
// → iterations/direction/easing; fill/play-state keywords are ignored.
AnimationDef parseAnimationShorthand(const QString& raw, const QHash<QString, QString>& vars, const QString& host) {
  AnimationDef a;
  a.host = host;
  a.iterations = 1;
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(raw, vars));
  bool gotDuration = false;
  static const QSet<QString> kSkip = {QStringLiteral("forwards"), QStringLiteral("backwards"), QStringLiteral("both"),
      QStringLiteral("none"), QStringLiteral("running"), QStringLiteral("paused"), QStringLiteral("initial"), QStringLiteral("inherit")};
  static const QSet<QString> kEase = {QStringLiteral("linear"), QStringLiteral("ease"), QStringLiteral("ease-in"),
      QStringLiteral("ease-out"), QStringLiteral("ease-in-out"), QStringLiteral("step-start"), QStringLiteral("step-end")};
  for (const QString& p : parts) {
    const QString t = p.toLower();
    if (t == QStringLiteral("infinite")) { a.iterations = -1; }
    else if (t == QStringLiteral("reverse")) { a.direction = AnimationDef::Direction::Reverse; }
    else if (t == QStringLiteral("alternate")) { a.direction = AnimationDef::Direction::Alternate; }
    else if (t == QStringLiteral("alternate-reverse")) { a.direction = AnimationDef::Direction::AlternateReverse; }
    else if (kEase.contains(t) || t.startsWith(QStringLiteral("cubic-bezier")) || t.startsWith(QStringLiteral("steps"))) {
      if (a.easing.isEmpty()) { a.easing = t; }
    }
    else if (kSkip.contains(t)) { /* fill/play-state — ignored */ }
    else if (transitionMs(t) > 0.0) {
      const qreal ms = transitionMs(t);
      if (!gotDuration) { a.durationMs = ms; gotDuration = true; } else { a.delayMs = ms; }
    }
    else {
      bool ok = false;
      const int n = t.toInt(&ok);
      if (ok) { a.iterations = n; }
      else if (a.name.isEmpty()) { a.name = p; }  // the animation name (identifier)
    }
  }
  return a;
}

// Always-on `animation:` on a host element (hover/state-triggered animations
// are captured separately when that wiring lands). Resolved against keyframes
// at drive time; entries with no matching keyframes are skipped by the driver.
std::vector<AnimationDef> extractAnimations(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<AnimationDef> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && s.tag == host;
    };
    const QString raw = bestValue(flat, {QStringLiteral("animation")}, pred);
    if (raw.isEmpty()) { continue; }
    AnimationDef a = parseAnimationShorthand(raw, vars, host);
    if (a.name.isEmpty() || a.durationMs <= 0.0) { continue; }
    out.push_back(std::move(a));
  }
  return out;
}

}  // namespace muffin
