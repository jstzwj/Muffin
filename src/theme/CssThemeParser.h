#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace muffin {

// One `property: value` pair, with !important peeled off. `value` is raw —
// var(--x) references are NOT resolved here; CssThemeMapper resolves them via
// CssThemeParser::resolveVars() so variable scoping (dark @media overrides) can be
// applied at translation time.
struct CssDeclaration {
  QString property;  // lowercased + trimmed, e.g. "background-color" or "--accent"
  QString value;     // trimmed, still may contain var(...) references
  bool important = false;
};

// A style rule. `selectors` are the raw selector strings split on top-level
// commas (each trimmed). darkScope marks rules that came from inside an
// `@media (prefers-color-scheme: dark)` block, so the mapper can prefer them
// when building a dark theme.
struct CssRule {
  QStringList selectors;
  std::vector<CssDeclaration> declarations;
  bool darkScope = false;
};

// An @font-face declaration. `srcPath` is the font file's ABSOLUTE path,
// resolved against the owning CSS file's directory (so a font declared in an
// @import'd base resolves relative to that base, not the top file). Only local
// files are captured; data:/remote sources are skipped. Used to register the
// font (QFontDatabase) so the theme's font-family stacks resolve to the bundled
// typefaces, and to know which font files to copy on import.
struct CssFontFace {
  QString family;   // quotes stripped, e.g. "LXGW WenKai" or "CascadiaCode"
  QString srcPath;  // absolute, cleaned path to the .ttf/.otf/.woff(2) on disk
};

// One keyframe stop: a position along the timeline (0..1, from `from`/`to`/`N%`)
// and the raw declarations active there. var() is NOT resolved here — the
// sampler resolves + interpolates at paint time.
struct CssKeyframeStop {
  qreal position = 0.0;
  std::vector<CssDeclaration> declarations;
};

// A named `@keyframes` definition (e.g. `@keyframes pulse { 0%{opacity:1} 50%{opacity:.3} 100%{opacity:1} }`).
// Stops are sorted by position by the parser.
struct CssKeyframes {
  QString name;
  std::vector<CssKeyframeStop> stops;
};

// A parsed stylesheet. Rules retain source order (later wins on ties), with
// imported sheets merged BEFORE the importing file's rules (CSS cascade). The
// variable table holds `:root` custom properties (--name -> raw value).
class CssThemeSheet {
public:
  const std::vector<CssRule>& rules() const { return rules_; }
  const QHash<QString, QString>& variables() const { return variables_; }
  const std::vector<CssFontFace>& fontFaces() const { return fontFaces_; }
  const std::vector<CssKeyframes>& keyframes() const { return keyframes_; }

  void addRule(CssRule r) { rules_.push_back(std::move(r)); }
  void setVariable(const QString& name, const QString& value) { variables_.insert(name, value); }
  void addFontFace(CssFontFace f) { fontFaces_.push_back(std::move(f)); }
  void addKeyframes(CssKeyframes k) { keyframes_.push_back(std::move(k)); }
  // Merge another sheet into this one: rules appended after, variables inserted
  // (overriding on name clash), font-faces appended. Used to fold @import'd
  // sheets under the importer — each font-face already carries its own correctly
  // resolved srcPath, so the merge just collects them.
  void mergeIn(const CssThemeSheet& other) {
    for (const CssRule& r : other.rules_) { rules_.push_back(r); }
    for (auto it = other.variables_.begin(); it != other.variables_.end(); ++it) {
      variables_.insert(it.key(), it.value());
    }
    for (const CssFontFace& f : other.fontFaces_) { fontFaces_.push_back(f); }
    for (const CssKeyframes& k : other.keyframes_) { keyframes_.push_back(k); }
  }

private:
  std::vector<CssRule> rules_;
  QHash<QString, QString> variables_;
  std::vector<CssFontFace> fontFaces_;
  std::vector<CssKeyframes> keyframes_;
};

// Minimal, robust-enough CSS parser tailored for CSS theme files. It is NOT a
// browser-grade cascade engine — it collects rules + :root variables + @font-face
// declarations in source order and lets CssThemeMapper do the (deliberately
// simplified) winning-decl selection. Handles: comments, "..." / '...' strings,
// url(...)/var(...) parens, `@import url(...)` (resolved relative to baseDir,
// merged first, recursive, missing/remote skipped), `@media (prefers-color-
// scheme: dark)` (marks rules), static screen media (`screen`, `all`, `not print`),
// `@font-face` (family + local src captured for
// font registration), `:root` (→ variables), and skips other at-rules
// (@keyframes/@page/…) by balanced-brace matching.
class CssThemeParser {
public:
  static CssThemeSheet parse(const QString& text, const QString& baseDir);

  // Recursively substitute var(--x[, fallback]) references in `value` using the
  // variable table. Unknown variables with no fallback resolve to empty string.
  static QString resolveVars(const QString& value, const QHash<QString, QString>& variables);

  // Split a top-level comma list (selectors or font-family lists), respecting
  // parens, brackets and strings. Exposed so the mapper can parse font stacks.
  static QStringList splitTopLevelCommas(const QString& text);

  // Discover every LOCAL file a CSS theme transitively references: @import
  // targets (recursed, so an @import'd base and its own @imports are included)
  // and url() resources in each file (@font-face fonts, background images, …).
  // Returns absolute, cleaned, de-duplicated paths. data:/remote URLs and
  // missing files are skipped. Used at import time to mirror the theme's folder
  // (fonts/base sheets) next to the installed top file so @import/@font-face
  // resolve at runtime.
  static QStringList localResourcePaths(const QString& text, const QString& baseDir);
};

}  // namespace muffin
