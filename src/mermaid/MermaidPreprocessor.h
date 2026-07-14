#pragma once

#include <QJsonObject>
#include <QString>

namespace muffin::mermaid {

struct MermaidPreprocessResult {
  QString code;
  QString title;
  bool hasTitle = false;
  QJsonObject config;
};

// Native port of Mermaid 11.16.0 src/preprocess.ts and its frontmatter,
// directive, and comment helpers. Throws std::runtime_error for invalid YAML,
// matching the upstream preprocessing failure boundary.
MermaidPreprocessResult preprocessDiagram(const QString& source);

}  // namespace muffin::mermaid
