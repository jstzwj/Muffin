#pragma once

#include <QString>

namespace muffin {

class HtmlSanitizer final {
public:
  QString sanitizedPreview(QString html) const;
};

}  // namespace muffin
