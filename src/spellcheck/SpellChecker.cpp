#include "spellcheck/SpellChecker.h"

#include <nuspell/dictionary.hxx>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QSettings>

#include <sstream>
#include <utility>

namespace muffin {

namespace {
// Settings keys. The locale is stored as a code string (e.g. "en_US") rather than an
// index, so the bundled set can change without invalidating stored preferences.
constexpr const char* kEnabledKey = "editor/spellCheckEnabled";
constexpr const char* kLanguageKey = "editor/spellCheckLanguage";
constexpr const char* kDefaultLocale = "en_US";
constexpr const char* kResourcePrefix = ":/dicts/";

// Read a (possibly zlib-compressed) Qt resource into a string. Qt decompresses
// compressed resources transparently, so nuspell always receives raw bytes.
bool readResource(const QString& alias, std::string& out) {
  QFile f(alias);
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.isEmpty()) {
    return false;
  }
  out.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
  return true;
}

// Load a Hunspell dictionary for locale from the bundled resources into dict.
bool loadDictFromResources(nuspell::Dictionary& dict, const QString& locale) {
  std::string aff;
  std::string dic;
  if (!readResource(QStringLiteral("%1%2.aff").arg(QString::fromLatin1(kResourcePrefix), locale), aff)) {
    return false;
  }
  if (!readResource(QStringLiteral("%1%2.dic").arg(QString::fromLatin1(kResourcePrefix), locale), dic)) {
    return false;
  }
  std::istringstream affStream(std::move(aff));
  std::istringstream dicStream(std::move(dic));
  try {
    dict.load_aff_dic(affStream, dicStream);
  } catch (const std::exception&) {
    return false;
  }
  return true;
}
}  // namespace

SpellChecker& SpellChecker::instance() {
  static SpellChecker s;
  return s;
}

SpellChecker::~SpellChecker() = default;

SpellChecker::SpellChecker(QObject* parent) : QObject(parent) {
  QSettings settings;
  selectedLocale_ = settings.value(kLanguageKey, QString::fromLatin1(kDefaultLocale)).toString();
  enabled_ = settings.value(kEnabledKey, false).toBool();
}

void SpellChecker::setEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  QSettings().setValue(kEnabledKey, enabled);
  emit enabledChanged(enabled);
}

void SpellChecker::setLanguage(const QString& localeCode) {
  if (!localeCode.isEmpty()) {
    QSettings().setValue(kLanguageKey, localeCode);
  }
  const QString before = language();
  selectedLocale_ = localeCode.isEmpty() ? QString::fromLatin1(kDefaultLocale) : localeCode;
  dictionary_.reset();  // reparse with the new dictionary on next use
  if (language() != before) {
    emit languageChanged(language());
  }
}

void SpellChecker::ensureLoaded() {
  if (dictionary_) {
    return;
  }
  loadDictionary(selectedLocale_.isEmpty() ? QString::fromLatin1(kDefaultLocale) : selectedLocale_);
}

void SpellChecker::loadDictionary(const QString& localeCode) {
  QString target = localeCode.isEmpty() ? QString::fromLatin1(kDefaultLocale) : localeCode;
  auto next = std::make_unique<nuspell::Dictionary>();
  if (!loadDictFromResources(*next, target)) {
    // Fall back to en_US, then give up (no dictionary loaded).
    if (target == QString::fromLatin1(kDefaultLocale) || !loadDictFromResources(*next, kDefaultLocale)) {
      dictionary_.reset();
      loadedLocale_.clear();
      return;
    }
    target = QString::fromLatin1(kDefaultLocale);
  }
  dictionary_ = std::move(next);
  loadedLocale_ = target;
}

QStringList SpellChecker::availableLanguages() {
  QStringList codes;
  QDir dir(QString::fromLatin1(kResourcePrefix));
  const QStringList dicFiles = dir.entryList({QStringLiteral("*.dic")}, QDir::Files);
  for (const QString& file : dicFiles) {
    codes << file.left(file.size() - 4);  // strip ".dic"
  }
  codes.sort();
  return codes;
}

bool SpellChecker::isCorrect(QStringView word) {
  if (!enabled_ || word.isEmpty()) {
    return true;
  }
  ensureLoaded();
  if (!dictionary_) {
    return true;
  }
  const QByteArray utf8 = word.toUtf8();
  if (utf8.isEmpty()) {
    return true;
  }
  const std::string_view sv(utf8.constData(), static_cast<size_t>(utf8.size()));
  if (dictionary_->spell(sv)) {
    return true;
  }
  return isIgnored(word);
}

QStringList SpellChecker::suggestions(QStringView word) {
  if (!enabled_ || word.isEmpty()) {
    return {};
  }
  ensureLoaded();
  if (!dictionary_) {
    return {};
  }
  const QByteArray utf8 = word.toUtf8();
  if (utf8.isEmpty()) {
    return {};
  }
  const std::string_view sv(utf8.constData(), static_cast<size_t>(utf8.size()));
  std::vector<std::string> raw;
  raw.reserve(8);
  dictionary_->suggest(sv, raw);
  QStringList out;
  for (const std::string& s : raw) {
    out << QString::fromStdString(s);
  }
  return out;
}

bool SpellChecker::isIgnored(QStringView word) const {
  if (word.isEmpty()) {
    return false;
  }
  return ignoredWords_.contains(word.toString().toLower());
}

void SpellChecker::ignoreWord(QStringView word) {
  if (word.isEmpty()) {
    return;
  }
  ignoredWords_.insert(word.toString().toLower());
}

}  // namespace muffin
