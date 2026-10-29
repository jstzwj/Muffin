#pragma once

#include "theme/CssComputedStyleEngine.h"

#include <memory>

namespace muffin {

// Author sheets stay unchanged. Each document backend composes the same host
// defaults below them, without accumulating copies across viewport changes.
CssThemeSheet documentStyleSheet(const CssThemeSheet& author);

// HTML presentational hints participate below author CSS in every native
// backend, including containers projected over Markdown blocks.
inline void addHtmlPresentationHints(CssElement& element) {
  const QString align = element.attributes.value(QStringLiteral("align")).toLower();
  if (element.tag == QLatin1String("center"))
    element.presentationDeclarations = CssThemeParser::parseDeclarations(QStringLiteral("text-align:center"));
  else if (align == QLatin1String("left") || align == QLatin1String("center") || align == QLatin1String("right") ||
           align == QLatin1String("justify"))
    element.presentationDeclarations = CssThemeParser::parseDeclarations(QStringLiteral("text-align:") + align);
}

// Theme compatibility classes describe the Markdown host, rather than a
// selector-matching exception. Prototypes and live nodes use this same contract.
inline void addDocumentHostClasses(CssElement& element) {
  if (element.tag.size() == 2 && element.tag[0] == QLatin1Char('h') && element.tag[1] >= QLatin1Char('1') &&
      element.tag[1] <= QLatin1Char('6'))
    element.classes << QStringLiteral("md-heading");
  if (element.tag == QStringLiteral("pre"))
    element.classes << QStringLiteral("md-fences");
}

inline QString markdownInlineKind(const QString& tag) {
  if (tag == "strong")
    return QStringLiteral("strong");
  if (tag == "em")
    return QStringLiteral("em");
  if (tag == "code")
    return QStringLiteral("code");
  if (tag == "a")
    return QStringLiteral("link");
  if (tag == "del")
    return QStringLiteral("strike");
  if (tag == "mark")
    return QStringLiteral("highlight");
  if (tag == "sub")
    return QStringLiteral("sub");
  if (tag == "sup")
    return QStringLiteral("sup");
  return {};
}

// The document host is shared by Markdown, HTML and theme previews. Elements
// are owned for the lifetime of a style snapshot; synthetic table sections and
// inline children use the same topology as their HTML counterparts.
struct DocumentStyleHost {
  CssElement html;
  CssElement body;
  CssElement content;
  CssElement write;

  DocumentStyleHost() {
    html.tag = QStringLiteral("html");
    body.tag = QStringLiteral("body");
    body.parent = &html;
    content.tag = QStringLiteral("content");
    content.parent = &body;
    write.tag = QStringLiteral("div");
    write.id = QStringLiteral("write");
    write.classes << QStringLiteral("md-document");
    write.parent = &content;
  }
  DocumentStyleHost(const DocumentStyleHost&) = delete;
  DocumentStyleHost& operator=(const DocumentStyleHost&) = delete;
};

class DocumentStylePrototypes {
public:
  DocumentStyleHost host;

  // Immutable prototypes may cache while the mapper owns them. When borrowing
  // an engine, release cacheIdentities() before this tree is destroyed.
  explicit DocumentStylePrototypes(bool cacheable = false) : cacheable_(cacheable) {
    if (cacheable_) {
      host.html.cacheId = nextCssElementIdentity();
      host.body.cacheId = nextCssElementIdentity();
      host.content.cacheId = nextCssElementIdentity();
      host.write.cacheId = nextCssElementIdentity();
    }
  }

  const CssElement& element(const QString& key) {
    if (key == QStringLiteral("html"))
      return host.html;
    if (key == QStringLiteral("body"))
      return host.body;
    if (key == QStringLiteral("content"))
      return host.content;
    if (key == QStringLiteral("#write"))
      return host.write;
    const auto found = elements_.constFind(key);
    if (found != elements_.constEnd())
      return *found.value();
    auto value = std::make_shared<CssElement>();
    elements_.insert(key, value);
    const qsizetype pseudo = key.indexOf(QStringLiteral("::"));
    if (pseudo >= 0) {
      const auto& parent = element(key.left(pseudo));
      *value = parent;
      value->pseudoElement = key.mid(pseudo + 2);
      value->originatingElement = &parent;
    } else {
      const qsizetype space = key.lastIndexOf(QLatin1Char(' '));
      value->tag = key.mid(space + 1);
      if (value->tag.startsWith(QLatin1Char('.'))) {
        value->classes << value->tag.mid(1);
        value->tag = QStringLiteral("span");
      }
      QString parentKey = space >= 0 ? key.left(space) : QStringLiteral("#write");
      if (key == QStringLiteral("code") || key == QStringLiteral("kbd") || key == QStringLiteral("a") ||
          key == QStringLiteral("mark") || key == QStringLiteral("del") || key == QStringLiteral("em") ||
          key == QStringLiteral("strong"))
        parentKey = QStringLiteral("p");
      if (key == QStringLiteral("li"))
        parentKey = QStringLiteral("ul");
      if (key == QStringLiteral("thead") || key == QStringLiteral("tbody"))
        parentKey = QStringLiteral("table");
      if (key == QStringLiteral("th"))
        parentKey = QStringLiteral("thead tr");
      if (key == QStringLiteral("td"))
        parentKey = QStringLiteral("tbody tr");
      if (key == QStringLiteral("table"))
        parentKey = QStringLiteral(".table-figure");
      if (key == QStringLiteral(".table-figure"))
        value->tag = QStringLiteral("figure");
      if (key == QStringLiteral("pre") || key == QStringLiteral(".md-fences")) {
        value->tag = QStringLiteral("pre");
        value->classes.clear();
      }
      addDocumentHostClasses(*value);
      value->parent = &element(parentKey);
      const QString kind = markdownInlineKind(value->tag);
      if (parentKey == "p" && !kind.isEmpty()) {
        auto wrapper = std::make_shared<CssElement>();
        wrapper->tag = QStringLiteral("span");
        wrapper->attributes.insert(QStringLiteral("md-inline"), kind);
        wrapper->parent = value->parent;
        if (cacheable_)
          wrapper->cacheId = nextCssElementIdentity();
        value->parent = wrapper.get();
        wrappers_.insert(key, std::move(wrapper));
      }
      if (parentKey == "pre" && value->tag == "code")
        value->classes << QStringLiteral("md-fencescode");
    }
    if (cacheable_)
      value->cacheId = nextCssElementIdentity();
    return *value;
  }

  QVector<quint64> cacheIdentities() const {
    QVector<quint64> ids{host.html.cacheId, host.body.cacheId, host.content.cacheId, host.write.cacheId};
    for (const auto& element : elements_)
      ids.append(element->cacheId);
    for (const auto& wrapper : wrappers_)
      ids.append(wrapper->cacheId);
    return ids;
  }

private:
  bool cacheable_ = false;
  QHash<QString, std::shared_ptr<CssElement>> elements_;
  QHash<QString, std::shared_ptr<CssElement>> wrappers_;
};

} // namespace muffin
