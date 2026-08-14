#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace muffin::mermaid {

struct MermaidPreprocessResult {
  QString code;
  QString title;
  bool hasTitle = false;
  QJsonObject config;
  // One original-source UTF-16 offset per character in code. Parsers consume
  // code after front matter/directive/comment removal; this map restores their
  // diagnostics to the code-fence literal shown by the editor.
  QVector<qsizetype> codeSourceOffsets;
  qsizetype codeSourceEndOffset = 0;
};

// Native port of Mermaid 11.16.0 src/preprocess.ts and its frontmatter,
// directive, and comment helpers. Throws std::runtime_error for invalid YAML,
// matching the upstream preprocessing failure boundary.
MermaidPreprocessResult preprocessDiagram(const QString& source);

// Applies the second configuration phase used by Mermaid's render/parse entry
// point after preprocessDiagram(): addDirective() sanitizes the directive
// object unconditionally, mirrors a top-level fontFamily into themeVariables,
// then updateCurrentConfig() removes secure keys and unsafe string values.
// preprocessDiagram() deliberately remains the observable intermediate result
// because Mermaid exposes that helper independently.
QJsonObject mermaidRenderConfig(const QJsonObject& preprocessedConfig);

}  // namespace muffin::mermaid
