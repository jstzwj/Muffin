#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>
#include <optional>
#include <memory>

namespace muffin {
class CssImageResources;

// One `property: value` pair, with !important peeled off. `value` is raw —
// var(--x) references are NOT resolved here; CssThemeMapper resolves them via
// CssThemeParser::resolveVars() so variable scoping (dark @media overrides) can be
// applied at translation time.
struct CssDeclaration {
  QString property; // trimmed; standard names lowercased, custom names case-sensitive
  QString value;    // trimmed, still may contain var(...) references
  bool important = false;
  QString resourceBaseDir; // owning stylesheet, retained for url() in variables
};

// A location in the original file, retained across imports/nested conditions.
struct CssSourceLocation {
  QString file;
  int line = 1;
  int column = 1; // UTF-16 code units; location of the start of the rule
};

// Selectors are split at top-level commas; declarations retain source order.
struct CssRule {
  QStringList selectors;
  std::vector<CssDeclaration> declarations;
  bool darkScope = false;
  QStringList mediaQueries;   // Nested media lists are AND; each list is OR.
  QStringList supportQueries; // Nested @supports conditions must all match.
  CssSourceLocation source;
  bool userOverride = false; // application typography preferences, above author rules
};

// Unknown at-rule bodies remain skipped. Retain headers and font descriptors
// for explicit audits without loading resources or registering fonts.
struct CssAtRule {
  QString keyword;
  QString prelude;
  QString resourceTarget;
  QString importModifiers;
  QStringList mediaQueries;
  QStringList supportQueries;
  CssSourceLocation source;
  std::vector<CssDeclaration> descriptors; // @font-face only
};

struct CssEnvironment {
  qreal viewportWidth = 1024.0;
  qreal viewportHeight = 768.0;
  bool dark = false;
  bool print = false;
  qreal resolutionDppx = 1.0;
  qreal textScale = 1.0;
  // SVG's semantic projection supplies parent presentation/inline values and
  // initial values; preserve CSS-wide keywords until that used-value stage.
  bool deferComputedValues = false;
  bool reducedMotion = false;
  bool hoverAvailable = true;
  QString pointer = QStringLiteral("fine");
};

// An @font-face declaration. `srcPath` is the font file's ABSOLUTE path,
// resolved against the owning CSS file's directory (so a font declared in an
// @import'd base resolves relative to that base, not the top file). Only local
// files and embedded data are captured; remote sources are skipped. Used to register the
// font (QFontDatabase) so the theme's font-family stacks resolve to the bundled
// typefaces, and to know which font files to copy on import.
struct CssFontFace {
  QString family;  // quotes stripped, e.g. "LXGW WenKai" or "CascadiaCode"
  QString srcPath; // absolute, cleaned path to the .ttf/.otf/.woff(2) on disk
  QString weight = QStringLiteral("normal");
  QString style = QStringLiteral("normal");
  QStringList sources; // ordered src fallbacks; local: names, paths or data URLs
  QStringList mediaQueries;
  bool active(const CssEnvironment& environment) const;
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
  CssThemeSheet();
  quint64 revision() const { return revision_; }
  const std::vector<CssRule>& rules() const { return rules_; }
  const QHash<QString, QString>& variables() const { return variables_; }
  const std::vector<CssFontFace>& fontFaces() const { return fontFaces_; }
  const std::vector<CssKeyframes>& keyframes() const { return keyframes_; }
  const std::vector<CssAtRule>& atRules() const { return atRules_; }
  std::shared_ptr<CssImageResources> imageResources() const { return imageResources_; }

  void addRule(CssRule r) {
    rules_.push_back(std::move(r));
    touch();
  }
  void setVariable(const QString& name, const QString& value) {
    variables_.insert(name, value);
    touch();
  }
  void addFontFace(CssFontFace f) {
    fontFaces_.push_back(std::move(f));
    touch();
  }
  void addKeyframes(CssKeyframes k) {
    keyframes_.push_back(std::move(k));
    touch();
  }
  void addAtRule(CssAtRule r) {
    atRules_.push_back(std::move(r));
    touch();
  }
  CssThemeSheet evaluated(const CssEnvironment& environment) const;
  bool sameActiveRules(const CssEnvironment& before, const CssEnvironment& after) const;
  bool hasViewportUnits() const;
  static bool mediaMatches(const QString& query, const CssEnvironment& environment);
  // Evaluate a conservative @supports query against the properties Muffin's
  // CSS projection and painters understand. Unsupported branches stay out of
  // the cascade instead of being applied optimistically.
  static bool supportsMatches(const QString& query);
  // Merge another sheet into this one: rules appended after, variables inserted
  // (overriding on name clash), font-faces appended. Used to fold @import'd
  // sheets under the importer — each font-face already carries its own correctly
  // resolved srcPath, so the merge just collects them.
  void mergeIn(const CssThemeSheet& other) {
    touch();
    for (const CssRule& r : other.rules_) {
      rules_.push_back(r);
    }
    for (auto it = other.variables_.begin(); it != other.variables_.end(); ++it) {
      variables_.insert(it.key(), it.value());
    }
    for (const CssFontFace& f : other.fontFaces_) {
      fontFaces_.push_back(f);
    }
    for (const CssKeyframes& k : other.keyframes_) {
      keyframes_.push_back(k);
    }
    for (const CssAtRule& r : other.atRules_) {
      atRules_.push_back(r);
    }
  }

private:
  void touch();
  quint64 revision_ = 0;
  std::vector<CssRule> rules_;
  QHash<QString, QString> variables_;
  std::vector<CssFontFace> fontFaces_;
  std::vector<CssKeyframes> keyframes_;
  std::vector<CssAtRule> atRules_;
  std::shared_ptr<CssImageResources> imageResources_;
};

// CSS theme parser retaining source order, nested media conditions, custom
// properties, font faces and keyframes. Local @imports resolve relative to their
// owning sheet. CssComputedStyleEngine evaluates the environment and cascade;
// this parser leaves declarations and variable references intact.
class CssThemeParser {
public:
  static CssThemeSheet parse(const QString& text, const QString& baseDir, const QString& sourceFile = {});
  // Literal url() tokens outside strings/comments. No I/O or decoding.
  static QStringList resourceUrls(const QString& value);
  static QString resolveResourceUrls(const QString& value, const QString& baseDir);

  // Parse an element's `style` attribute using the same declaration grammar as
  // stylesheet rules. Exposed for semantic renderers (notably Mermaid) that
  // build a lightweight DOM projection and need the real author cascade:
  // stylesheet !important > inline normal, while inline !important wins by
  // specificity. Invalid/incomplete declarations are skipped exactly as in a
  // normal rule block.
  static std::vector<CssDeclaration> parseDeclarations(const QString& text);
  // Conservative dependency check, including custom-property values before
  // var() substitution. Shared by stylesheets and authored inline HTML.
  static bool usesViewportUnits(const QString& value);

  // Recursively substitute var(--x[, fallback]) references in `value` using the
  // variable table. Unknown variables with no fallback resolve to empty string.
  static QString resolveVars(const QString& value, const QHash<QString, QString>& variables);
  static std::optional<QString> substituteVars(const QString& value, const QHash<QString, QString>& computedVariables);
  static QHash<QString, QString> computeCustomProperties(const QHash<QString, QString>& variables);

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
  // Cache manifests also need absent targets: creating a formerly missing
  // import/font must invalidate the theme just like editing an existing file.
  static QStringList localResourcePaths(const QString& text, const QString& baseDir, bool includeMissing = false);
};

} // namespace muffin
