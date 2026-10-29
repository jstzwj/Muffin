#include "app/WindowsIntegration.h"
#include "../TestUtils.h"

#include <QCoreApplication>
#include <QSettings>
#include <QUuid>

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

} // namespace

int main(int argc, char** argv) {
  UserRegistrySandbox sandbox;
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Muffin");
  QCoreApplication::setApplicationName("Muffin");
  runTest("testChoicesMatchAppliedIntegration", testChoicesMatchAppliedIntegration);
  return 0;
}
