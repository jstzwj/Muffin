#include "math/MathFontRegistry.h"

#include <QFontDatabase>
#include <QStringList>

static void initKatexFontsResource() {
  Q_INIT_RESOURCE(katex_fonts);
}

namespace muffin::math {

void MathFontRegistry::ensureLoaded() {
  static bool loaded = false;
  if (loaded) {
    return;
  }
  initKatexFontsResource();
  loaded = true;
  const QStringList fonts{
      QStringLiteral(":/katex/fonts/KaTeX_Main-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Main-Bold.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Main-Italic.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Math-Italic.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_AMS-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Caligraphic-Bold.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Caligraphic-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Fraktur-Bold.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Fraktur-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_SansSerif-Bold.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_SansSerif-Italic.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_SansSerif-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Script-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Size1-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Size2-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Size3-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Size4-Regular.ttf"),
      QStringLiteral(":/katex/fonts/KaTeX_Typewriter-Regular.ttf"),
  };
  for (const QString& font : fonts) {
    QFontDatabase::addApplicationFont(font);
  }
}

}  // namespace muffin::math
