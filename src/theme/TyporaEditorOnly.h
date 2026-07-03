#pragma once

#include <QSet>
#include <QString>

namespace muffin {

// Typora editor chrome / UI constructs Muffin never renders. Community themes stash
// editor-only hacks in their rules (e.g. pixyll `pre.md-meta-block { padding-top:2000px;
// margin-top:-2010px; width:100vw }` to position the front-matter strip in Typora's
// editor). Those hacks must not leak into Muffin's element-style matching, so any rule
// whose selector carries one of these classes is dropped.
//
// Shared by BOTH CSS engines so the denylist stays in lock-step:
//   - the flat semantic mapper (CssThemeMapper) drops the rule at flatten;
//   - the computed-style engine (CssComputedStyleEngine) drops it at selector match.
//
// Functional classes Muffin DOES use — md-fences, md-focus, md-image, md-task-list-item,
// md-heading — are intentionally NOT here. Confirmed: none of these are referenced as
// editor-only chrome anywhere in src/.
inline bool isTyporaEditorOnlyClass(const QString& cls) {
  if (cls.startsWith(QStringLiteral("md-toc")) || cls.startsWith(QStringLiteral("outline-")) ||
      cls.startsWith(QStringLiteral("megamenu")) || cls.startsWith(QStringLiteral("modal-")) ||
      cls.startsWith(QStringLiteral("ty-"))) { return true; }
  static const QSet<QString> exact = {
      QStringLiteral("md-meta"), QStringLiteral("md-meta-block"),
      QStringLiteral("md-comment"), QStringLiteral("md-raw"), QStringLiteral("md-rawblock"),
      QStringLiteral("md-search-hit"), QStringLiteral("md-search-panel"),
      QStringLiteral("md-search-input"), QStringLiteral("md-search-tip"),
      QStringLiteral("md-expand"), QStringLiteral("sidebar-content"), QStringLiteral("footer-item"),
  };
  return exact.contains(cls);
}

}  // namespace muffin
