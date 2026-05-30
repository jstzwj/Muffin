#pragma once

#include "parser/SourceSpan.h"
#include "renderer/MarkerProjection.h"

#include <optional>

namespace Muffin {

struct MarkerSelection {
    enum class Kind {
        SingleMarker,
        MarkerPairWithContent
    };

    Kind kind = Kind::SingleMarker;
    SourceSpan source;
    int projectedStart = -1;
    int projectedEnd = -1;
    MarkerProjectionSpan marker;

    bool isValid() const { return source.isValid() && projectedStart >= 0 && projectedEnd >= projectedStart; }
};

class MarkerSelectionMapper {
public:
    static std::optional<MarkerSelection> selectionForProjectedRange(const MarkerProjection& projection,
                                                                     int projectedStart,
                                                                     int projectedEnd);
};

} // namespace Muffin
