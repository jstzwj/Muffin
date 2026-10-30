#include "app/WindowsIntegration.h"
#include "app/WindowsShellResources.h"
#include "../TestUtils.h"

#include <QCoreApplication>
#include <QList>
#include <QPair>
#include <QSettings>
#include <QUuid>
#include <QTranslator>

using namespace muffin;

namespace {

// Exercise the production registry writer without touching Explorer's real
// registrations, the user's settings, or the protected default-app choice.
class UserRegistrySandbox {
public:
  UserRegistrySandbox() {
    path_ = L"Software\\MuffinIntegrationTest\\" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString();
    require(RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_ALL_ACCESS, &original_) == ERROR_SUCCESS,
            "open original user hive");
    require(RegCreateKeyExW(original_, path_.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &sandbox_, nullptr) ==
                ERROR_SUCCESS,
            "create isolated user hive");
    require(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox_) == ERROR_SUCCESS, "redirect this process's user hive");
  }

  ~UserRegistrySandbox() {
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(sandbox_);
    RegDeleteTreeW(original_, path_.c_str());
    RegCloseKey(original_);
  }

private:
  HKEY original_ = nullptr;
  HKEY sandbox_ = nullptr;
  std::wstring path_;
};

void testChoicesMatchAppliedIntegration() {
  QSettings settings;
  settings.setValue("unrelated/setting", "keep");
  settings.setValue("integration/promptDefaultOnLaunch", 1); // legacy installers may have left this behind
  for (const bool context : {false, true}) {
    for (const bool openWith : {false, true}) {
      WindowsIntegration::applyIntegration(context, openWith);
      settings.sync();
      require(WindowsIntegration::isContextMenuEnabled() == context, "context-menu registration follows preference");
      require(WindowsIntegration::isAssociationEnabled() == openWith, "Open with registration follows preference");
      for (const auto& entry :
           {qMakePair(QStringLiteral("contextMenu"), context), qMakePair(QStringLiteral("openWith"), openWith)}) {
        HKEY key = nullptr;
        require(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Muffin\\Muffin\\integration", 0, KEY_READ, &key) ==
                    ERROR_SUCCESS,
                "installer and app share the same settings key");
        DWORD type = 0, value = 9, size = sizeof(value);
        const auto result = RegQueryValueExW(key, entry.first.toStdWString().c_str(), nullptr, &type,
                                             reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
        require(result == ERROR_SUCCESS && type == REG_DWORD && value == (entry.second ? 1u : 0u),
                "store both enabled and disabled choices as installer-readable DWORDs");
      }
      require(settings.value("unrelated/setting") == QStringLiteral("keep"), "keep unrelated application settings");
    }
  }
  // A subsequent explicit opt-out must replace a previously saved opt-in.
  WindowsIntegration::applyIntegration(true, true);
  WindowsIntegration::applyIntegration(false, false);
  settings.sync();
  require(settings.contains("integration/contextMenu") && settings.value("integration/contextMenu").toInt() == 0 &&
              settings.contains("integration/openWith") && settings.value("integration/openWith").toInt() == 0,
          "turning integration off preserves an explicit choice for the next upgrade");
}

QString registryText(const std::wstring& path, const wchar_t* name = nullptr) {
  DWORD bytes = 0;
  require(RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS,
          "localized registry value exists");
  std::wstring text(bytes / sizeof(wchar_t), L'\0');
  require(RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_REG_SZ, nullptr, text.data(), &bytes) ==
              ERROR_SUCCESS,
          "read localized registry value");
  return QString::fromWCharArray(text.c_str());
}

QString nativeString(UINT id, WORD language) {
  const HMODULE module = GetModuleHandleW(nullptr);
  const auto resource = FindResourceExW(module, RT_STRING, MAKEINTRESOURCEW(id / 16 + 1), language);
  require(resource, "each shell language has a native string table");
  const auto* text = static_cast<const WCHAR*>(LockResource(LoadResource(module, resource)));
  require(text, "load native shell string table");
  for (UINT i = 0; i < id % 16; ++i)
    text += 1 + *text;
  return QString::fromWCharArray(text + 1, *text);
}

void testLocalizedLabelsKeepChoicesAndCommands() {
  const std::wstring classes = L"SOFTWARE\\Classes\\";
  const std::wstring fileVerb = classes + L".md\\shell\\Muffin";
  const std::wstring openVerb = classes + L"Muffin.md\\shell\\open";
  const std::wstring progId = classes + L"Muffin.md";
  WindowsIntegration::applyIntegration(false, false);
  QSettings settings;
  const auto savedChoices = settings.allKeys();
  WindowsIntegration::refreshLocalizedLabels();
  require(!WindowsIntegration::isContextMenuEnabled() && !WindowsIntegration::isAssociationEnabled() &&
              savedChoices == settings.allKeys(),
          "refreshing language cannot re-enable disabled integrations");
  WindowsIntegration::applyIntegration(true, true);
  const auto command = registryText(fileVerb + L"\\command");
  const auto openCommand = registryText(openVerb + L"\\command");
  const auto icon = registryText(progId + L"\\DefaultIcon");
  const auto preferenceKeys = settings.allKeys();
  const QList<QPair<QString, WORD>> languages = {{"en", 0x0409}, {"de", 0x0407},    {"es", 0x0c0a},    {"fr", 0x040c},
                                                 {"it", 0x0410}, {"ja", 0x0411},    {"ko", 0x0412},    {"nl", 0x0413},
                                                 {"pl", 0x0415}, {"pt_BR", 0x0416}, {"ru", 0x0419},    {"tr", 0x041f},
                                                 {"vi", 0x042a}, {"zh_CN", 0x0804}, {"zh_TW", 0x0404}, {"en", 0x0409}};
  for (const auto& [code, language] : languages) {
    QTranslator translator;
    if (code != "en") {
      require(translator.load(":/i18n/muffin_" + code + ".qm"), "load application catalog for shell labels");
      QCoreApplication::installTranslator(&translator);
    }
    WindowsIntegration::refreshLocalizedLabels();
    const auto label = [](const char* source) {
      return QCoreApplication::translate("muffin::WindowsIntegration", source);
    };
    const auto context = label("Open with Muffin");
    for (const auto& extension : WindowsIntegration::markdownExtensions()) {
      const auto path = classes + extension.toStdWString() + L"\\shell\\Muffin";
      require(registryText(path) == context && registryText(path, L"MUIVerb") == context,
              "all file verbs follow the selected app language");
    }
    for (const auto* suffix : {L"Directory\\shell\\Muffin", L"Directory\\Background\\shell\\Muffin"})
      require(registryText(classes + suffix, L"MUIVerb") == context, "folder verbs follow the selected app language");
    require(registryText(progId) == label("Muffin Markdown Document") &&
                registryText(progId, L"FriendlyTypeName") == label("Muffin Markdown Document") &&
                registryText(openVerb, L"MUIVerb") == label("Open"),
            "document type and open verb are localized");
    require(nativeString(MUFFIN_IDS_OPEN_WITH, language) == context &&
                nativeString(MUFFIN_IDS_DOCUMENT_TYPE, language) == label("Muffin Markdown Document") &&
                nativeString(MUFFIN_IDS_OPEN, language) == label("Open"),
            "installer resources reuse the app translations");
    require(registryText(fileVerb + L"\\command") == command && registryText(openVerb + L"\\command") == openCommand &&
                registryText(progId + L"\\DefaultIcon") == icon && preferenceKeys == settings.allKeys(),
            "language changes preserve commands, icon paths and user preferences");
    require(settings.value("integration/contextMenu").toInt() == 1 &&
                settings.value("integration/openWith").toInt() == 1,
            "language changes preserve enabled preference values");
    QCoreApplication::removeTranslator(&translator);
  }
  require(nativeString(MUFFIN_IDS_OPEN_WITH, 0) == QStringLiteral("Open with Muffin"),
          "unsupported OS languages fall back to English");
  WindowsIntegration::applyIntegration(false, true);
  WindowsIntegration::refreshLocalizedLabels();
  require(!WindowsIntegration::isContextMenuEnabled() && WindowsIntegration::isAssociationEnabled(),
          "language refresh preserves independent opt-outs");
  WindowsIntegration::applyIntegration(false, false);
}

} // namespace

int main(int argc, char** argv) {
  UserRegistrySandbox sandbox;
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Muffin");
  QCoreApplication::setApplicationName("Muffin");
  runTest("testChoicesMatchAppliedIntegration", testChoicesMatchAppliedIntegration);
  runTest("testLocalizedLabelsKeepChoicesAndCommands", testLocalizedLabelsKeepChoicesAndCommands);
  return 0;
}
