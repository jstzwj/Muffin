#pragma once

#include "document/MarkdownNode.h"
#include "document/CooperativeTask.h"
#include "Export.h"
#include <QHash>
#include <QString>
#include <optional>

namespace muffin {
struct HtmlContainerBoundary {
  QString tag;
  bool closing = false;
};
// Shared syntax for rendering and structural source operations. Only standalone,
// matched container boundaries group Markdown siblings; malformed HTML stays literal.
MUFFIN_CORE_EXPORT std::optional<HtmlContainerBoundary> htmlContainerBoundary(const MarkdownNode& node);
MUFFIN_CORE_EXPORT QHash<size_t, size_t> htmlContainerPairs(const MarkdownNode& owner);
MUFFIN_CORE_EXPORT CooperativeTask htmlContainerPairSteps(const MarkdownNode& owner, QHash<size_t, size_t>& pairs);
} // namespace muffin
