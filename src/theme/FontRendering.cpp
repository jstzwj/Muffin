#include "theme/FontRendering.h"

#include <QtGlobal>

void muffin::font_rendering::configureForScreen(QFont& font) {
#if defined(Q_OS_WIN)
  // Qt 6.11 maps the default preference at DPR 1 to full horizontal hinting
  // and GDI-classic metrics for small text. Vertical hinting selects
  // DirectWrite ClearType Natural instead: vertical grid fitting stays crisp,
  // while horizontal advances remain fractional and layout-device independent.
  font.setHintingPreference(QFont::PreferVerticalHinting);
#else
  Q_UNUSED(font);
#endif
}
