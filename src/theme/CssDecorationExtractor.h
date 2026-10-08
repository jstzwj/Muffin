#pragma once
#include "theme/CssComputedStyleEngine.h"
#include "theme/ThemeDefinition.h"
namespace muffin {
using ComputedDecorationStyles = QHash<QString, CssComputedStyle>;
ListGuide extractListGuide(const CssComputedStyle& style);
std::vector<PseudoElementRule> extractPseudoRules(const ComputedDecorationStyles& styles);
std::vector<ElementBackground> extractElementBackgrounds(const ComputedDecorationStyles& styles);
std::vector<HoverEffect> extractHoverEffects(const ComputedDecorationStyles& styles);
std::vector<TransitionSpec> extractTransitions(const ComputedDecorationStyles& styles);
std::vector<KeyframesDef> extractKeyframes(const CssThemeSheet& sheet, const QHash<QString, QString>& vars);
AnimationDef parseAnimationShorthand(const QString& raw, const QHash<QString, QString>& vars, const QString& host);
std::vector<AnimationDef> extractAnimations(const ComputedDecorationStyles& styles);
}  // namespace muffin
