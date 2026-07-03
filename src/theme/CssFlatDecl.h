#pragma once

#include "theme/CssSelectorAnalysis.h"  // SelInfo (FlatDecl member + bestValue/colorToken predicate)

#include <QColor>
#include <QHash>
#include <QString>
#include <functional>
#include <vector>

namespace muffin {

// A flat, ordered view of one (rule, selector, declaration) triple from the non-dark-scoped
// rules of a CSS sheet. flatten() / flattenHover() / flattenStatePseudo() (still in
// CssThemeMapper.cpp) build the vectors; bestValue() / colorToken() and the decoration
// extractors (CssDecorationExtractor) consume them. Pre-analysing the selector into SelInfo
// lets every consumer query by element/property without re-parsing the selector string.
struct FlatDecl {
  SelInfo info;
  QString property;  // lowercased
  QString value;     // raw (var() not yet resolved)
  bool important;
  int spec;
  int order;
};

// Best raw (un-resolved) value among declarations matching one of `properties` whose selector
// satisfies `target`. Returns empty if none. Defined in CssDecorationExtractor.cpp.
QString bestValue(const std::vector<FlatDecl>& flat, const std::vector<QString>& properties,
                  const std::function<bool(const SelInfo&)>& target);

// extractColor(bestValue(...)) — a colour token resolved through the shared colour chokepoint.
// Returns invalid (not black) when no matching declaration exists. Defined in
// CssDecorationExtractor.cpp.
QColor colorToken(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars,
                  const std::vector<QString>& properties, const std::function<bool(const SelInfo&)>& target);

}  // namespace muffin
