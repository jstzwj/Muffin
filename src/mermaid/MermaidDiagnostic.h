#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace muffin::mermaid {

// Family-neutral diagnostic carried from a native Mermaid parser to the editor.
// line/column are always 1-based and offset/length address the original code
// fence literal, after mapping around front matter, directives, and comments.
struct MermaidSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 0;
  int column = 0;

  bool hasLocation() const {
    return offset >= 0 && line > 0 && column > 0;
  }
};

struct MermaidDiagnostic {
  QString diagramType;
  QString stage;
  QString code;
  MermaidSourceSpan span;
  QString message;
  QString production;
  QString actual;
  QStringList expected;

  bool isEmpty() const {
    return message.trimmed().isEmpty() && code.trimmed().isEmpty();
  }
};

QString formatMermaidDiagnosticHeader(const MermaidDiagnostic& diagnostic);
QString formatMermaidDiagnosticBody(const MermaidDiagnostic& diagnostic);
QString formatMermaidDiagnostic(const MermaidDiagnostic& diagnostic);
QJsonObject mermaidDiagnosticToJson(const MermaidDiagnostic& diagnostic);

}  // namespace muffin::mermaid
