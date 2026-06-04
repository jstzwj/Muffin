#pragma once

#include "math/MathSettings.h"
#include "math/MathStyle.h"

#include <QColor>
#include <QFont>

namespace muffin::math {

class MathOptions {
public:
  MathOptions() = default;
  MathOptions(MathStyle style, qreal basePointSize, QColor color, MathSettings settings);

  MathStyle style() const;
  qreal basePointSize() const;
  qreal sizeMultiplier() const;
  qreal fontPointSize() const;
  QColor color() const;
  bool phantom() const;
  const MathSettings& settings() const;

  MathOptions havingStyle(MathStyle style) const;
  MathOptions withColor(QColor color) const;
  MathOptions withPhantom() const;
  MathOptions havingSizeScale(qreal scale) const;
  MathOptions sup() const;
  MathOptions sub() const;
  MathOptions fracNum() const;
  MathOptions fracDen() const;
  MathOptions cramped() const;

  QFont fontForClass(const QString& fontClass) const;

private:
  MathStyle style_ = MathStyle::textStyle();
  qreal basePointSize_ = 12.0;
  qreal sizeScale_ = 1.0;
  QColor color_ = Qt::black;
  bool phantom_ = false;
  MathSettings settings_;
};

}  // namespace muffin::math
