#pragma once

#include "parser/MathSpan.h"
#include "theme/Theme.h"
#include <QImage>

namespace Muffin {

class MathImageRenderer {
public:
    explicit MathImageRenderer(const Theme& theme);

    QImage render(const MathSpan& span) const;

private:
    Theme m_theme;
};

} // namespace Muffin
