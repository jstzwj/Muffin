#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <memory>

// nuspell wraps its public types in a versioned inline namespace (v5); forward-declare
// with that same inline namespace so the name resolves to the real type, not a phantom.
namespace nuspell {
inline namespace v5 {
class Dictionary;
}
}

namespace muffin {

// Process-wide spell checker built on nuspell. Loads the Hunspell dictionary for the
// configured locale from the bundled ":/dicts/" resources (falling back to en_US) and
// answers fast spell/suggest queries used by both the source-mode QSyntaxHighlighter
// and the rendered-mode squiggle overlay. GUI-thread only.
class SpellChecker : public QObject {
  Q_OBJECT

 public:
  static SpellChecker& instance();

  // Defined out-of-line (in the .cpp) so the nuspell::Dictionary held by the
  // unique_ptr is a complete type at the point of destruction.
  ~SpellChecker();

  bool isEnabled() const { return enabled_; }
  // Persists editor/spellCheckEnabled and emits enabledChanged. The dictionary stays
  // loaded regardless so toggling on is instant.
  void setEnabled(bool enabled);

  // The selected (and, once loaded, effective) locale code, e.g. "en_US". Used by the
  // Preferences dropdown without forcing a dictionary load.
  QString language() const { return dictionary_ ? loadedLocale_ : selectedLocale_; }
  // Loads the dictionary for localeCode (e.g. "fr"), persisting editor/spellCheckLanguage.
  // Falls back to en_US if the requested locale isn't bundled. Emits languageChanged
  // when the effectively-selected locale changes.
  void setLanguage(const QString& localeCode);

  // Locale codes of bundled dictionaries (sorted), for the Preferences dropdown.
  static QStringList availableLanguages();

  // True if the word is spelled correctly OR checking is disabled OR no dictionary is
  // loaded OR the word is in the session ignore list. Callers can therefore invoke this
  // unconditionally to decide whether to draw a squiggle.
  bool isCorrect(QStringView word);
  // Suggested corrections for a misspelled word (empty when disabled / no dict).
  QStringList suggestions(QStringView word);

  bool isIgnored(QStringView word) const;
  void ignoreWord(QStringView word);

 signals:
  void enabledChanged(bool enabled);
  void languageChanged(const QString& localeCode);

 private:
  SpellChecker(QObject* parent = nullptr);
  void loadDictionary(const QString& localeCode);
  // Lazily parse the selected dictionary on first use, so a disabled checker adds no
  // startup cost (dictionary parsing is ~hundreds of ms).
  void ensureLoaded();

  std::unique_ptr<nuspell::Dictionary> dictionary_;
  QString selectedLocale_;
  QString loadedLocale_;
  QSet<QString> ignoredWords_;  // lowercased, for case-insensitive ignore
  bool enabled_ = false;
};

}  // namespace muffin
