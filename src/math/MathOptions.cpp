#include "math/MathOptions.h"

#include <QtGlobal>

namespace muffin::math {
namespace {

constexpr qreal kPointsPerCssPixel = 72.0 / 96.0;

qreal multiplierForStyle(MathStyle style) {
  switch (style.id()) {
    case MathStyle::Display:
    case MathStyle::DisplayCramped:
    case MathStyle::Text:
    case MathStyle::TextCramped:
      return 1.0;
    case MathStyle::Script:
    case MathStyle::ScriptCramped:
      return 0.7;
    case MathStyle::ScriptScript:
    case MathStyle::ScriptScriptCramped:
      return 0.5;
  }
  return 1.0;
}

}  // namespace

MathOptions::MathOptions(MathStyle style, qreal basePointSize, QColor color, MathSettings settings)
    : style_(style), basePointSize_(basePointSize), color_(std::move(color)), settings_(std::move(settings)) {}

MathStyle MathOptions::style() const {
  return style_;
}

qreal MathOptions::basePointSize() const {
  return basePointSize_;
}

qreal MathOptions::sizeMultiplier() const {
  return multiplierForStyle(style_) * sizeScale_;
}

qreal MathOptions::fontPointSize() const {
  return basePointSize_ * sizeMultiplier();
}

QColor MathOptions::color() const {
  return color_;
}

bool MathOptions::phantom() const {
  return phantom_;
}

const MathSettings& MathOptions::settings() const {
  return settings_;
}

MathOptions MathOptions::havingStyle(MathStyle style) const {
  MathOptions copy = *this;
  copy.style_ = style;
  return copy;
}

MathOptions MathOptions::withColor(QColor color) const {
  MathOptions copy = *this;
  copy.color_ = std::move(color);
  return copy;
}

MathOptions MathOptions::withPhantom() const {
  MathOptions copy = *this;
  copy.phantom_ = true;
  return copy;
}

MathOptions MathOptions::havingSizeScale(qreal scale) const {
  MathOptions copy = *this;
  copy.sizeScale_ = qMax<qreal>(0.1, scale);
  return copy;
}

MathOptions MathOptions::sup() const {
  return havingStyle(style_.sup());
}

MathOptions MathOptions::sub() const {
  return havingStyle(style_.sub());
}

MathOptions MathOptions::fracNum() const {
  return havingStyle(style_.fracNum());
}

MathOptions MathOptions::fracDen() const {
  return havingStyle(style_.fracDen());
}

MathOptions MathOptions::cramped() const {
  return havingStyle(style_.cramp());
}

QFont MathOptions::fontForClass(const QString& fontClass) const {
  QString family = QStringLiteral("KaTeX_Main");
  if (fontClass == QStringLiteral("mathnormal")) {
    family = QStringLiteral("KaTeX_Math");
  } else if (fontClass == QStringLiteral("mathit")) {
    family = QStringLiteral("KaTeX_Main");
  } else if (fontClass == QStringLiteral("mathbf")) {
    family = QStringLiteral("KaTeX_Main");
  } else if (fontClass == QStringLiteral("amsrm")) {
    family = QStringLiteral("KaTeX_AMS");
  } else if (fontClass == QStringLiteral("mathcal")) {
    family = QStringLiteral("KaTeX_Caligraphic");
  } else if (fontClass == QStringLiteral("mathfrak")) {
    family = QStringLiteral("KaTeX_Fraktur");
  } else if (fontClass == QStringLiteral("sans")) {
    family = QStringLiteral("KaTeX_SansSerif");
  } else if (fontClass == QStringLiteral("typewriter")) {
    family = QStringLiteral("KaTeX_Typewriter");
  } else if (fontClass == QStringLiteral("script")) {
    family = QStringLiteral("KaTeX_Script");
  } else if (fontClass == QStringLiteral("sqrt") || fontClass == QStringLiteral("size") || fontClass == QStringLiteral("size1")) {
    family = QStringLiteral("KaTeX_Size1");
  } else if (fontClass == QStringLiteral("size2")) {
    family = QStringLiteral("KaTeX_Size2");
  } else if (fontClass == QStringLiteral("size3")) {
    family = QStringLiteral("KaTeX_Size3");
  } else if (fontClass == QStringLiteral("size4")) {
    family = QStringLiteral("KaTeX_Size4");
  }

  QFont font(family);
  font.setPointSizeF(qMax<qreal>(1.0, fontPointSize()) * kPointsPerCssPixel);
  if (fontClass == QStringLiteral("mathbf")) {
    font.setBold(true);
  } else if (fontClass == QStringLiteral("typewriter")) {
    font.setStyleHint(QFont::Monospace);
  }
  return font;
}

}  // namespace muffin::math
