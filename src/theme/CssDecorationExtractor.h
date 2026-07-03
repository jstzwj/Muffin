#pragma once

#include "theme/CssFlatDecl.h"      // FlatDecl
#include "theme/ThemeDefinition.h"  // PseudoElementRule / HoverEffect / … return types

#include <QHash>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <vector>

namespace muffin {

class CssThemeSheet;  // defined in CssThemeParser.h (extractKeyframes reads it directly)

// Filter the flat declaration list to a single host + pseudo-element channel
// (e.g. host="h3", pseudo="before"). Defined in CssDecorationExtractor.cpp.
std::vector<FlatDecl> filterPseudoFlat(const std::vector<FlatDecl>& flat, const QString& host, const QString& pseudo);

// Nested-list guide line from a `li::before { border-left/left/top/height: calc(100% - Npx) }`
// rule (phycat's per-item vertical tree line). Defined in CssDecorationExtractor.cpp.
ListGuide extractListGuide(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars, qreal bodyPx);

// ::before/::after capture grouped by host → PseudoElementRule recipes. `emPxForHost` resolves
// the em basis per host tag (heading vs blockquote differ). Defined in CssDecorationExtractor.cpp.
std::vector<PseudoElementRule> extractPseudoRules(const std::vector<FlatDecl>& flat,
                                                   const QHash<QString, QString>& vars,
                                                   const std::function<qreal(const QString&)>& emPxForHost);

// Host element OWN background-image gradients + top-border/radius box decoration (not pseudos).
// Defined in CssDecorationExtractor.cpp.
std::vector<ElementBackground> extractElementBackgrounds(const std::vector<FlatDecl>& flat,
                                                          const QHash<QString, QString>& vars, qreal emPx);

// Tractable :hover subset: box-shadow glow (colour + blur) + background tint, from the
// hover-only flat list (flattenHover). Defined in CssDecorationExtractor.cpp.
std::vector<HoverEffect> extractHoverEffects(const std::vector<FlatDecl>& flatHover, const QHash<QString, QString>& vars);

// Per-host `transition:` durations (ms). Defined in CssDecorationExtractor.cpp.
std::vector<TransitionSpec> extractTransitions(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars);

// @keyframes with var() resolved once at load. Defined in CssDecorationExtractor.cpp.
std::vector<KeyframesDef> extractKeyframes(const CssThemeSheet& sheet, const QHash<QString, QString>& vars);

// Parse one `animation:` shorthand value into an AnimationDef. Defined in CssDecorationExtractor.cpp.
AnimationDef parseAnimationShorthand(const QString& raw, const QHash<QString, QString>& vars, const QString& host);

// Always-on `animation:` on a host element. Defined in CssDecorationExtractor.cpp.
std::vector<AnimationDef> extractAnimations(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars);

}  // namespace muffin
