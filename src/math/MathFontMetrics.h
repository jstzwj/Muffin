#pragma once

#include <QString>

#include <optional>

namespace muffin::math {

struct CharacterMetrics {
  qreal depth = 0.0;
  qreal height = 0.0;
  qreal italic = 0.0;
  qreal skew = 0.0;
  qreal width = 0.0;
};

struct GlobalFontMetrics {
  qreal cssEmPerMu = 1.0 / 18.0;
  qreal xHeight = 0.431;
  qreal quad = 1.0;
  qreal defaultRuleThickness = 0.04;
  qreal bigOpSpacing1 = 0.111;
  qreal bigOpSpacing2 = 0.166;
  qreal bigOpSpacing3 = 0.200;
  qreal bigOpSpacing4 = 0.600;
  qreal bigOpSpacing5 = 0.100;
  qreal sqrtRuleThickness = 0.04;
  qreal ptPerEm = 10.0;
  qreal arrayRuleWidth = 0.04;
  qreal axisHeight = 0.25;
  qreal num1 = 0.677;
  qreal num2 = 0.394;
  qreal num3 = 0.444;
  qreal denom1 = 0.686;
  qreal denom2 = 0.345;
  qreal sup1 = 0.413;
  qreal sup2 = 0.363;
  qreal sup3 = 0.289;
  qreal sub1 = 0.150;
  qreal sub2 = 0.247;
  qreal supDrop = 0.386;
  qreal subDrop = 0.050;
  qreal delim1 = 2.390;
  qreal delim2 = 1.010;
  qreal doubleRuleSep = 0.2;
  qreal fboxsep = 0.3;
  qreal fboxrule = 0.04;
};

class MathFontMetrics {
public:
  static std::optional<CharacterMetrics> characterMetrics(const QString& fontName, const QString& character);
  static GlobalFontMetrics globalMetrics(int styleSize);
  static bool loaded();

private:
  static void ensureLoaded();
};

}  // namespace muffin::math
