#include "mermaid/MermaidDiagramDetector.h"

#include <QJsonValue>
#include <QRegularExpression>

namespace muffin::mermaid {
namespace {

QString nestedString(const QJsonObject& config, const QString& section, const QString& key) {
  return config.value(section).toObject().value(key).toString();
}

bool starts(const QString& text, const QString& expression,
            QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption) {
  return QRegularExpression(expression, options).match(text).hasMatch();
}

bool isEcmaScriptWhitespace(QChar ch) {
  const ushort u = ch.unicode();
  return (u >= 0x0009 && u <= 0x000d) || u == 0x0020 || u == 0x00a0 ||
         u == 0x1680 || (u >= 0x2000 && u <= 0x200a) || u == 0x2028 ||
         u == 0x2029 || u == 0x202f || u == 0x205f || u == 0x3000 ||
         u == 0xfeff;
}

QString stripDetectionPreamble(QString text) {
  // Mermaid detectors use JavaScript `^\s*`; PCRE2's `\s` does not cover the
  // same Unicode set. Normalize only the leading prefix consumed by that
  // expression, leaving diagram contents byte-for-byte intact.
  for (qsizetype i = 0; i < text.size() && isEcmaScriptWhitespace(text.at(i)); ++i)
    text[i] = QLatin1Char(' ');
  static const QRegularExpression frontMatter(
      QStringLiteral(R"(^([^\S\n\r]*)-{3}\s*[\n\r](.*?)[\n\r]\1-{3}\s*[\n\r]+)"),
      QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression directive(
      QStringLiteral(R"(%{2}\{\s*(?:(\w+)\s*:|(\w+))\s*(?:(\w+)|((?:(?!\}%{2}).|\r?\n)*))?\s*(?:\}%{2})?)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression comment(QStringLiteral(R"(\s*%%.*\n)"),
                                           QRegularExpression::MultilineOption);
  text.remove(frontMatter);
  text.remove(directive);
  text.replace(comment, QStringLiteral("\n"));
  return text;
}

}  // namespace

UnknownDiagramError::UnknownDiagramError(const QString& message)
    : std::runtime_error(message.toUtf8().constData()) {}

QString detectDiagramType(const QString& source, QJsonObject config) {
  const QString text = stripDetectionPreamble(source);
  const QString flowRenderer = nestedString(config, QStringLiteral("flowchart"), QStringLiteral("defaultRenderer"));
  const QString classRenderer = nestedString(config, QStringLiteral("class"), QStringLiteral("defaultRenderer"));
  const QString stateRenderer = nestedString(config, QStringLiteral("state"), QStringLiteral("defaultRenderer"));
  const auto insensitive = QRegularExpression::CaseInsensitiveOption;

  // addDiagrams() registration order is observable: the first matching detector wins.
  if (text.toLower().trimmed() == QLatin1String("error")) return QStringLiteral("error");
  if (text.toLower().trimmed().startsWith(QLatin1String("---"))) return QStringLiteral("---");
  if (starts(text, QStringLiteral(R"(^\s*flowchart-elk)")) ||
      (starts(text, QStringLiteral(R"(^\s*(flowchart|graph))")) && flowRenderer == QLatin1String("elk"))) {
    return QStringLiteral("flowchart-elk");
  }
  if (starts(text, QStringLiteral(R"(^\s*mindmap)"))) return QStringLiteral("mindmap");
  if (starts(text, QStringLiteral(R"(^\s*architecture)"))) return QStringLiteral("architecture");
  // Preserve the upstream C4 detector's intentionally ungrouped alternation.
  if (starts(text, QStringLiteral(R"(^\s*C4Context|C4Container|C4Component|C4Dynamic|C4Deployment)"))) return QStringLiteral("c4");
  if (starts(text, QStringLiteral(R"(^\s*kanban)"))) return QStringLiteral("kanban");
  if ((starts(text, QStringLiteral(R"(^\s*classDiagram)")) && classRenderer == QLatin1String("dagre-wrapper")) ||
      starts(text, QStringLiteral(R"(^\s*classDiagram-v2)"))) return QStringLiteral("classDiagram");
  if (classRenderer != QLatin1String("dagre-wrapper") && starts(text, QStringLiteral(R"(^\s*classDiagram)"))) return QStringLiteral("class");
  if (starts(text, QStringLiteral(R"(^\s*erDiagram)"))) return QStringLiteral("er");
  if (starts(text, QStringLiteral(R"(^\s*gantt)"))) return QStringLiteral("gantt");
  if (starts(text, QStringLiteral(R"(^\s*info)"))) return QStringLiteral("info");
  if (starts(text, QStringLiteral(R"(^\s*pie)"))) return QStringLiteral("pie");
  if (starts(text, QStringLiteral(R"(^\s*requirement(Diagram)?)"))) return QStringLiteral("requirement");
  if (starts(text, QStringLiteral(R"(^\s*sequenceDiagram)"))) return QStringLiteral("sequence");
  if (starts(text, QStringLiteral(R"(^\s*swimlane-beta\b)"))) return QStringLiteral("swimlane");
  if (flowRenderer != QLatin1String("dagre-d3") &&
      ((starts(text, QStringLiteral(R"(^\s*graph)")) && flowRenderer == QLatin1String("dagre-wrapper")) ||
       starts(text, QStringLiteral(R"(^\s*flowchart)")))) return QStringLiteral("flowchart-v2");
  if (flowRenderer != QLatin1String("dagre-wrapper") && flowRenderer != QLatin1String("elk") &&
      starts(text, QStringLiteral(R"(^\s*graph)"))) return QStringLiteral("flowchart");
  if (starts(text, QStringLiteral(R"(^\s*timeline)"))) return QStringLiteral("timeline");
  if (starts(text, QStringLiteral(R"(^\s*gitGraph)"))) return QStringLiteral("gitGraph");
  if (starts(text, QStringLiteral(R"(^\s*stateDiagram-v2)")) ||
      (starts(text, QStringLiteral(R"(^\s*stateDiagram)")) && stateRenderer == QLatin1String("dagre-wrapper"))) return QStringLiteral("stateDiagram");
  if (stateRenderer != QLatin1String("dagre-wrapper") && starts(text, QStringLiteral(R"(^\s*stateDiagram)"))) return QStringLiteral("state");
  if (starts(text, QStringLiteral(R"(^\s*journey)"))) return QStringLiteral("journey");
  if (starts(text, QStringLiteral(R"(^\s*quadrantChart)"))) return QStringLiteral("quadrantChart");
  if (starts(text, QStringLiteral(R"(^\s*sankey(-beta)?)"))) return QStringLiteral("sankey");
  if (starts(text, QStringLiteral(R"(^\s*packet(-beta)?)"))) return QStringLiteral("packet");
  if (starts(text, QStringLiteral(R"(^\s*xychart(-beta)?)"))) return QStringLiteral("xychart");
  if (starts(text, QStringLiteral(R"(^\s*block(-beta)?)"))) return QStringLiteral("block");
  if (starts(text, QStringLiteral(R"(^\s*eventmodeling)"))) return QStringLiteral("eventmodeling");
  if (starts(text, QStringLiteral(R"(^\s*treeView-beta)"))) return QStringLiteral("treeView");
  if (starts(text, QStringLiteral(R"(^\s*radar-beta)"))) return QStringLiteral("radar");
  if (starts(text, QStringLiteral(R"(^\s*ishikawa(-beta)?\b)"), insensitive)) return QStringLiteral("ishikawa");
  if (starts(text, QStringLiteral(R"(^\s*treemap)"))) return QStringLiteral("treemap");
  if (starts(text, QStringLiteral(R"(^\s*railroad-beta)"), insensitive)) return QStringLiteral("railroad");
  if (starts(text, QStringLiteral(R"(^\s*railroad-ebnf-beta)"), insensitive)) return QStringLiteral("railroadEbnf");
  if (starts(text, QStringLiteral(R"(^\s*railroad-abnf-beta)"), insensitive)) return QStringLiteral("railroadAbnf");
  if (starts(text, QStringLiteral(R"(^\s*railroad-peg-beta)"), insensitive)) return QStringLiteral("railroadPeg");
  if (starts(text, QStringLiteral(R"(^\s*venn-beta)"))) return QStringLiteral("venn");
  if (starts(text, QStringLiteral(R"(^\s*wardley-beta)"), insensitive)) return QStringLiteral("wardley");
  if (starts(text, QStringLiteral(R"(^\s*cynefin-beta(?:[\s:]|$))"))) return QStringLiteral("cynefin");

  throw UnknownDiagramError(QStringLiteral("No diagram type detected matching given configuration for text: %1").arg(text));
}

}  // namespace muffin::mermaid
