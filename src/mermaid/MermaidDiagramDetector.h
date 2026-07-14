#pragma once

#include <QJsonObject>
#include <QString>

#include <stdexcept>

namespace muffin::mermaid {

class UnknownDiagramError final : public std::runtime_error {
public:
  explicit UnknownDiagramError(const QString& message);
};

// Returns Mermaid's registered diagram id (not merely its source keyword).
// The detector order and renderer-selection rules match Mermaid 11.16.0.
QString detectDiagramType(const QString& source, QJsonObject config = {});

}  // namespace muffin::mermaid
