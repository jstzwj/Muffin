#pragma once

#include "theme/CssThemeParser.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace muffin {

struct CssElement {
  QString tag;
  QString id;
  QStringList classes;
  QString pseudoElement;  // "", "before", "after", "selection", "marker"
  bool nthEven = false;
  const CssElement* parent = nullptr;
};

struct CssElementState {
  bool hover = false;
  bool focus = false;
  bool active = false;
  bool visited = false;
  bool mdFocus = false;
};

class CssComputedStyle {
public:
  QString rawValue(const QString& property) const;
  QString resolvedValue(const QString& property) const;
  bool hasProperty(const QString& property) const;

  const QHash<QString, QString>& customProperties() const { return customProperties_; }

  QHash<QString, QString> properties_;
  QHash<QString, QString> customProperties_;
};

class CssComputedStyleEngine {
public:
  explicit CssComputedStyleEngine(const CssThemeSheet& sheet);

  CssComputedStyle styleFor(const CssElement& element) const;
  CssComputedStyle styleFor(const CssElement& element, const CssElementState& state) const;

private:
  const CssThemeSheet& sheet_;
};

}  // namespace muffin
