#include "mermaid/theme/MermaidStyleResolve.h"

#include <QHash>

#include <utility>

namespace muffin::mermaid::style {
namespace {

// styles2Map (chunk-BNCO5QFQ.mjs:30): insertion-ordered, last-wins. Qt has no
// ordered map, so keep a vector (order) + hash (key -> index).
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

// Split a "key:value" style string on the first ":" (mermaid's rule).
QPair<QString, QString> splitStyle(const QString& style) {
  const int colon = style.indexOf(QLatin1Char(':'));
  if (colon < 0) return {style.trimmed(), QString()};
  return {style.left(colon).trimmed(), style.mid(colon + 1).trimmed()};
}

// getCompiledStyles: for each class name, push that class's declarations.
StyleMap compileClassMap(const QStringList& classNames,
                         const QVector<ClassDef>& classDefs) {
  StyleMap map;
  for (const QString& name : classNames) {
    for (const ClassDef& def : classDefs) {
      if (def.id != name) continue;
      for (const QString& s : def.declarations) {
        const auto [k, v] = splitStyle(s);
        if (!k.isEmpty()) map.insert(k, v);
      }
      break;
    }
  }
  return map;
}

// compileStyles: mermaid prepends the built-in "default" and "node" classes to
// every node (getCompiledStyles(["default","node",...classes])), so
// `classDef default`/`classDef node` apply to all nodes. Inline `style` is
// applied after classDef.
StyleMap compileStyles(const QStringList& classes, const QStringList& inlineStyles,
                       const QVector<ClassDef>& classDefs) {
  QStringList names;
  names.reserve(classes.size() + 2);
  names.append(QStringLiteral("default"));
  names.append(QStringLiteral("node"));
  names.append(classes);
  StyleMap map = compileClassMap(names, classDefs);
  for (const QString& s : inlineStyles) {
    const auto [k, v] = splitStyle(s);
    if (!k.isEmpty()) map.insert(k, v);
  }
  return map;
}

}  // namespace

QStringList compiledClassStyles(const QStringList& classNames,
                                const QVector<ClassDef>& classDefs) {
  const StyleMap map = compileClassMap(classNames, classDefs);
  QStringList out;
  out.reserve(map.entries.size());
  for (const auto& [key, value] : map.entries)
    out.append(key + QLatin1Char(':') + value);
  return out;
}

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

ResolvedNodeStyle resolveNodeStyle(const QStringList& classes, const QStringList& inlineStyles,
                                   const QVector<ClassDef>& classDefs,
                                   const ThemeDefaults& theme) {
  const StyleMap map = compileStyles(classes, inlineStyles, classDefs);

  // styles2String: split into labelStyles/nodeStyles, each `!important`, joined ';'.
  QStringList labelStyles, nodeStyles;
  for (const auto& [key, value] : map.entries) {
    const QString entry = key + QLatin1Char(':') + value + QStringLiteral(" !important");
    if (isLabelStyle(key))
      labelStyles.append(entry);
    else
      nodeStyles.append(entry);
  }

  ResolvedNodeStyle r;
  r.nodeStyles = nodeStyles.join(QLatin1Char(';'));
  r.labelStyles = labelStyles.join(QLatin1Char(';'));

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

ResolvedEdgeStyle resolveEdgeStyle(const QString& pattern, const QStringList& linkStyles,
                                   bool animate, const QString& animation,
                                   const ThemeDefaults& theme) {
  ResolvedEdgeStyle r;
  r.stroke = theme.lineColor;
  r.strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
  r.fill = QStringLiteral("none");
  // Thickness token (normal/dotted/thick/invisible) -> mermaid CSS classes.
  if (pattern == QStringLiteral("dotted")) {
    r.strokeDasharray = QStringLiteral("2");
  } else if (pattern == QStringLiteral("thick")) {
    r.strokeWidth = QStringLiteral("3.5px");
  } else if (pattern == QStringLiteral("invisible")) {
    r.stroke = QStringLiteral("none");
    r.strokeWidth = QStringLiteral("0px");
  }
  // linkStyle overrides (key:value, the auto "fill:none" already appended).
  for (const QString& s : linkStyles) {
    const auto [key, value] = splitStyle(s);
    if (key == QStringLiteral("stroke")) r.stroke = value;
    else if (key == QStringLiteral("stroke-width")) r.strokeWidth = value;
    else if (key == QStringLiteral("stroke-dasharray")) r.strokeDasharray = value;
    else if (key == QStringLiteral("fill")) r.fill = value;
  }
  // Mermaid's edge-animation CSS applies this with !important (wins over all).
  if (animate || !animation.isEmpty()) r.strokeDasharray = QStringLiteral("9,5");
  return r;
}

}  // namespace muffin::mermaid::style
