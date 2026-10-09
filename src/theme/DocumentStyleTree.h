#pragma once

#include "theme/CssComputedStyleEngine.h"

#include <memory>

namespace muffin {

// Author sheets stay unchanged. Each document backend composes the same host
// defaults below them, without accumulating copies across viewport changes.
CssThemeSheet documentStyleSheet(const CssThemeSheet& author);

// Theme compatibility classes describe the Markdown host, rather than a
// selector-matching exception. Prototypes and live nodes use this same contract.
inline void addDocumentHostClasses(CssElement& element) {
  if (element.tag.size() == 2 && element.tag[0] == QLatin1Char('h') &&
      element.tag[1] >= QLatin1Char('1') && element.tag[1] <= QLatin1Char('6'))
    element.classes << QStringLiteral("md-heading");
  if (element.tag == QStringLiteral("pre")) element.classes << QStringLiteral("md-fences");
}

// The document host is shared by Markdown, HTML and theme previews. Elements
// are owned for the lifetime of a style snapshot; synthetic table sections and
// inline children use the same topology as their HTML counterparts.
struct DocumentStyleHost {
  CssElement html;
  CssElement body;
  CssElement write;

  DocumentStyleHost() {
    html.tag = QStringLiteral("html");
    body.tag = QStringLiteral("body");
    body.parent = &html;
    write.tag = QStringLiteral("div");
    write.id = QStringLiteral("write");
    write.parent = &body;
  }
  DocumentStyleHost(const DocumentStyleHost&) = delete;
  DocumentStyleHost& operator=(const DocumentStyleHost&) = delete;
};

class DocumentStylePrototypes {
 public:
  DocumentStyleHost host;

  const CssElement& element(const QString& key) {
    if (key == QStringLiteral("html")) return host.html;
    if (key == QStringLiteral("body")) return host.body;
    if (key == QStringLiteral("#write")) return host.write;
    const auto found = elements_.constFind(key);
    if (found != elements_.constEnd()) return *found.value();
    auto value = std::make_shared<CssElement>();
    elements_.insert(key, value);
    const qsizetype pseudo = key.indexOf(QStringLiteral("::"));
    if (pseudo >= 0) {
      const auto& parent = element(key.left(pseudo));
      value->tag = parent.tag;
      value->id = parent.id;
      value->classes = parent.classes;
      value->pseudoElement = key.mid(pseudo + 2);
      value->parent = &parent;
    } else {
      const qsizetype space = key.lastIndexOf(QLatin1Char(' '));
      value->tag = key.mid(space + 1);
      if (value->tag.startsWith(QLatin1Char('.'))) {
        value->classes << value->tag.mid(1);
        value->tag = QStringLiteral("span");
      }
      QString parentKey = space >= 0 ? key.left(space) : QStringLiteral("#write");
      if (key == QStringLiteral("code") || key == QStringLiteral("kbd") || key == QStringLiteral("a") || key == QStringLiteral("mark") ||
          key == QStringLiteral("del") || key == QStringLiteral("em"))
        parentKey = QStringLiteral("p");
      if (key == QStringLiteral("li")) parentKey = QStringLiteral("ul");
      if (key == QStringLiteral("thead") || key == QStringLiteral("tbody")) parentKey = QStringLiteral("table");
      if (key == QStringLiteral("th")) parentKey = QStringLiteral("thead tr");
      if (key == QStringLiteral("td")) parentKey = QStringLiteral("tbody tr");
      if (key == QStringLiteral("pre") || key == QStringLiteral(".md-fences")) {
        value->tag = QStringLiteral("pre");
        value->classes.clear();
      }
      addDocumentHostClasses(*value);
      value->parent = &element(parentKey);
    }
    return *value;
  }

 private:
  QHash<QString, std::shared_ptr<CssElement>> elements_;
};

}  // namespace muffin
