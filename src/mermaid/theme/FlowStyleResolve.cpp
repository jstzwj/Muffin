#include "mermaid/theme/FlowStyleResolve.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <utility>

namespace muffin::mermaid::flowstyle {
namespace {

// styles2Map (chunk-BNCO5QFQ.mjs:30): a Map preserving insertion order with
// last-wins values. Qt has no ordered map, so we keep a vector (order) + a
// hash (key -> index) for O(1) update.
struct StyleMap {
  QVector<QPair<QString, QString>> entries;
  QHash<QString, int> index;
  void insert(const QString& key, const QString& value) {
    const auto it = index.constFind(key);
    if (it == index.constEnd()) {
      index.insert(key, entries.size());
      entries.append({key, value});
    } else {
      entries[it.value()].second = value;
    }
  }
};

// Split a "key:value" style string. mermaid splits on the first ":".
QPair<QString, QString> splitStyle(const QString& style) {
  const int colon = style.indexOf(QLatin1Char(':'));
  if (colon < 0) return {style.trimmed(), QString()};
  return {style.left(colon).trimmed(), style.mid(colon + 1).trimmed()};
}

// getCompiledStyles (chunk-YI7H2ERT.mjs:934): for each class name, push the
// class's styles then its textStyles. "default"/"node" built-in classes are
// empty, so only the user's vertex.classes contribute.
StyleMap compileStyles(const flowchart::FlowVertex& vertex,
                       const QVector<flowchart::FlowClass>& classes) {
  StyleMap map;
  for (const QString& name : vertex.classes) {
    for (const flowchart::FlowClass& cls : classes) {
      if (cls.id != name) continue;
      for (const QString& s : cls.styles) { const auto [k, v] = splitStyle(s); if (!k.isEmpty()) map.insert(k, v); }
      for (const QString& s : cls.textStyles) { const auto [k, v] = splitStyle(s); if (!k.isEmpty()) map.insert(k, v); }
      break;
    }
  }
  // inline `style A ...` (cssStyles) — applied after classDef.
  for (const QString& s : vertex.styles) { const auto [k, v] = splitStyle(s); if (!k.isEmpty()) map.insert(k, v); }
  return map;
}

}  // namespace

bool isLabelStyle(const QString& key) {
  static const QStringList keys = {
      QStringLiteral("color"), QStringLiteral("font-size"), QStringLiteral("font-family"),
      QStringLiteral("font-weight"), QStringLiteral("font-style"), QStringLiteral("text-decoration"),
      QStringLiteral("text-align"), QStringLiteral("text-transform"), QStringLiteral("line-height"),
      QStringLiteral("letter-spacing"), QStringLiteral("word-spacing"), QStringLiteral("text-shadow"),
      QStringLiteral("text-overflow"), QStringLiteral("white-space"), QStringLiteral("word-wrap"),
      QStringLiteral("word-break"), QStringLiteral("overflow-wrap"), QStringLiteral("hyphens"),
  };
  return keys.contains(key);
}

ResolvedNodeStyle resolveNodeStyle(const flowchart::FlowVertex& vertex,
                                   const QVector<flowchart::FlowClass>& classes,
                                   const flowtheme::FlowThemeVariables& theme) {
  const StyleMap map = compileStyles(vertex, classes);

  // styles2String (chunk-BNCO5QFQ.mjs:41): split into labelStyles/nodeStyles,
  // each entry suffixed ` !important`, joined with `;`.
  QStringList labelStyles, nodeStyles;
  for (const auto& [key, value] : map.entries) {
    const QString entry = key + QLatin1Char(':') + value + QStringLiteral(" !important");
    if (isLabelStyle(key)) {
      labelStyles.append(entry);
    } else {
      nodeStyles.append(entry);
    }
  }

  ResolvedNodeStyle r;
  r.nodeStyles = nodeStyles.join(QLatin1Char(';'));
  r.labelStyles = labelStyles.join(QLatin1Char(';'));

  // Resolved paint: theme defaults, overridden by the cascade map.
  const auto lookup = [&](const QString& key) -> QString {
    const auto it = map.index.constFind(key);
    return it == map.index.constEnd() ? QString() : map.entries.at(it.value()).second;
  };
  r.fill = lookup(QStringLiteral("fill"));
  if (r.fill.isEmpty()) r.fill = theme.mainBkg;
  r.stroke = lookup(QStringLiteral("stroke"));
  if (r.stroke.isEmpty()) r.stroke = theme.nodeBorder;
  r.strokeWidth = lookup(QStringLiteral("stroke-width"));
  if (r.strokeWidth.isEmpty()) r.strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
  r.color = lookup(QStringLiteral("color"));
  if (r.color.isEmpty()) r.color = theme.nodeTextColor.isEmpty() ? theme.textColor : theme.nodeTextColor;
  r.fontFamily = lookup(QStringLiteral("font-family"));
  if (r.fontFamily.isEmpty()) r.fontFamily = theme.fontFamily;
  r.fontSize = lookup(QStringLiteral("font-size"));
  if (r.fontSize.isEmpty()) r.fontSize = theme.fontSize;
  r.fontWeight = lookup(QStringLiteral("font-weight"));
  return r;
}

ResolvedEdgeStyle resolveEdgeStyle(const flowchart::FlowEdge& edge,
                                   const flowtheme::FlowThemeVariables& theme) {
  ResolvedEdgeStyle r;
  r.stroke = theme.lineColor;
  r.strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
  r.fill = QStringLiteral("none");
  // Thickness (edge.stroke = normal/dotted/thick/invisible) is applied via
  // mermaid CSS classes: edge-pattern-dotted -> stroke-dasharray:2,
  // edge-thickness-thick -> stroke-width:3.5px, invisible -> no stroke.
  if (edge.stroke == QStringLiteral("dotted")) {
    r.strokeDasharray = QStringLiteral("2");
  } else if (edge.stroke == QStringLiteral("thick")) {
    r.strokeWidth = QStringLiteral("3.5px");
  } else if (edge.stroke == QStringLiteral("invisible")) {
    r.stroke = QStringLiteral("none");
    r.strokeWidth = QStringLiteral("0px");
  }
  // linkStyle (edge.style) overrides: key:value strings with the auto
  // "fill:none" already appended by the parser. Last-wins per key.
  for (const QString& s : edge.style) {
    const auto [key, value] = splitStyle(s);
    if (key == QStringLiteral("stroke")) r.stroke = value;
    else if (key == QStringLiteral("stroke-width")) r.strokeWidth = value;
    else if (key == QStringLiteral("stroke-dasharray")) r.strokeDasharray = value;
    else if (key == QStringLiteral("fill")) r.fill = value;
  }
  return r;
}

}  // namespace muffin::mermaid::flowstyle
