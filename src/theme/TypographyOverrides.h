#pragma once

#include <QVariantMap>
#include <array>

class QSettings;
namespace muffin {
class CssThemeSheet;

// Zero sizes and empty families inherit the theme. Values are logical CSS px,
// before the independent text-scale and zoom controls. Never stored in Markdown.
struct TypographyOverrides {
  QString bodyFamily, codeFamily;
  int bodySize = 0, codeSize = 0, sourceSize = 0;
  std::array<int, 6> headingSizes{};
  bool operator==(const TypographyOverrides&) const = default;
  bool hasDocumentOverrides() const;
  QVariantMap values() const;
  static TypographyOverrides fromValues(const QVariantMap& values);
  static TypographyOverrides load();
  void save() const;
  CssThemeSheet styleSheet(qreal bodyScale = 1) const;
  QString css(qreal bodyScale = 1) const;
};
}
