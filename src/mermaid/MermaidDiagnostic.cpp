#include "mermaid/MermaidDiagnostic.h"

#include <QJsonArray>

#include <algorithm>

namespace muffin::mermaid {
namespace {

QString stageLabel(const QString& stage) {
  if (stage == QLatin1String("detector")) return QStringLiteral("Diagram detection error");
  if (stage == QLatin1String("preprocess")) return QStringLiteral("Preprocessing error");
  if (stage == QLatin1String("lexer")) return QStringLiteral("Lexical error");
  if (stage == QLatin1String("parser")) return QStringLiteral("Syntax error");
  if (stage == QLatin1String("semantic")) return QStringLiteral("Semantic error");
  if (stage == QLatin1String("resource")) return QStringLiteral("Resource limit");
  if (stage == QLatin1String("security")) return QStringLiteral("Security error");
  if (stage == QLatin1String("render")) return QStringLiteral("Rendering error");
  if (stage == QLatin1String("unsupported")) return QStringLiteral("Unsupported Mermaid diagram");
  return QStringLiteral("Mermaid error");
}

QString humanizeCode(QString code) {
  code.replace(QLatin1Char('-'), QLatin1Char(' '));
  if (!code.isEmpty()) code[0] = code.at(0).toUpper();
  return code;
}

QString detailLine(const QString& label, const QString& value) {
  return value.trimmed().isEmpty()
      ? QString()
      : QStringLiteral("%1: %2").arg(label, value.trimmed());
}

}  // namespace

QString formatMermaidDiagnosticHeader(const MermaidDiagnostic& diagnostic) {
  QString header;
  if (diagnostic.span.hasLocation()) {
    header = QStringLiteral("Line %1, column %2")
                 .arg(diagnostic.span.line)
                 .arg(diagnostic.span.column);
  }
  const QString label = stageLabel(diagnostic.stage);
  if (!label.isEmpty()) {
    if (!header.isEmpty()) header += QStringLiteral("  |  ");
    header += label;
  }
  if (!diagnostic.code.isEmpty()) {
    header += QStringLiteral(" [%1]").arg(diagnostic.code);
  }
  return header;
}

QString formatMermaidDiagnosticBody(const MermaidDiagnostic& diagnostic) {
  QStringList lines;
  QString message = diagnostic.message.trimmed();
  if (message.isEmpty() && !diagnostic.code.isEmpty()) {
    message = humanizeCode(diagnostic.code) + QLatin1Char('.');
  }
  if (!message.isEmpty()) lines.push_back(message);

  if (!diagnostic.production.isEmpty() &&
      !message.contains(diagnostic.production, Qt::CaseInsensitive)) {
    lines.push_back(detailLine(QStringLiteral("Context"), diagnostic.production));
  }
  if (!diagnostic.actual.isEmpty() &&
      !message.contains(diagnostic.actual, Qt::CaseInsensitive)) {
    lines.push_back(detailLine(QStringLiteral("Found"), diagnostic.actual));
  }
  if (!diagnostic.expected.isEmpty()) {
    const QString expected = diagnostic.expected.join(QStringLiteral(" or "));
    bool alreadyExplained = !expected.isEmpty() &&
                            message.contains(expected, Qt::CaseInsensitive);
    if (!alreadyExplained) {
      alreadyExplained = std::all_of(
          diagnostic.expected.cbegin(), diagnostic.expected.cend(),
          [&message](const QString& value) {
            return value.isEmpty() || message.contains(value, Qt::CaseInsensitive);
          });
    }
    if (!alreadyExplained) {
      lines.push_back(detailLine(QStringLiteral("Expected"), expected));
    }
  }
  lines.removeAll(QString());
  return lines.join(QLatin1Char('\n'));
}

QString formatMermaidDiagnostic(const MermaidDiagnostic& diagnostic) {
  const QString header = formatMermaidDiagnosticHeader(diagnostic);
  const QString body = formatMermaidDiagnosticBody(diagnostic);
  if (header.isEmpty()) return body;
  if (body.isEmpty()) return header;
  return header + QLatin1Char('\n') + body;
}

QJsonObject mermaidDiagnosticToJson(const MermaidDiagnostic& diagnostic) {
  QJsonObject result{
      {QStringLiteral("diagramType"), diagnostic.diagramType},
      {QStringLiteral("stage"), diagnostic.stage},
      {QStringLiteral("code"), diagnostic.code},
      {QStringLiteral("message"), diagnostic.message},
      {QStringLiteral("production"), diagnostic.production},
      {QStringLiteral("actual"), diagnostic.actual},
      {QStringLiteral("offset"), static_cast<double>(diagnostic.span.offset)},
      {QStringLiteral("length"), static_cast<double>(diagnostic.span.length)},
      {QStringLiteral("line"), diagnostic.span.line},
      {QStringLiteral("column"), diagnostic.span.column},
  };
  QJsonArray expected;
  for (const QString& value : diagnostic.expected) expected.push_back(value);
  result.insert(QStringLiteral("expected"), expected);
  return result;
}

}  // namespace muffin::mermaid
