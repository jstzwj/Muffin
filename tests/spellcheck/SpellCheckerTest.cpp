#include "spellcheck/SpellChecker.h"

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}
}  // namespace

int main(int argc, char** argv) {
  // Use an INI-format settings store under a throwaway org/app name so the test never
  // touches the real Muffin QSettings (e.g. the Windows registry).
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("SpellCheckerTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QCoreApplication app(argc, argv);

  // The en_US dictionary must be bundled.
  const QStringList langs = muffin::SpellChecker::availableLanguages();
  require(langs.contains(QStringLiteral("en_US")), "en_US dictionary must be bundled");

  auto& sc = muffin::SpellChecker::instance();
  sc.setLanguage(QStringLiteral("en_US"));
  sc.setEnabled(true);

  require(sc.isCorrect(QStringLiteral("hello")), "\"hello\" should be correct");
  require(sc.isCorrect(QStringLiteral("world")), "\"world\" should be correct");
  require(!sc.isCorrect(QStringLiteral("spelllng")), "\"spelllng\" should be misspelled");
  require(!sc.isCorrect(QStringLiteral("recieve")), "\"recieve\" should be misspelled");

  const QStringList sugg = sc.suggestions(QStringLiteral("spelllng"));
  require(sugg.contains(QStringLiteral("spelling")),
          "suggestions for \"spelllng\" should include \"spelling\"");

  // Ignored words are treated as correct for the rest of the session.
  require(!sc.isCorrect(QStringLiteral("xyzqwerty")), "control word should be misspelled");
  sc.ignoreWord(QStringLiteral("xyzqwerty"));
  require(sc.isCorrect(QStringLiteral("xyzqwerty")), "ignored word should be correct");

  // Disabling suppresses all squiggles.
  sc.setEnabled(false);
  require(sc.isCorrect(QStringLiteral("spelllng")), "disabled => everything treated as correct");
  return 0;
}
