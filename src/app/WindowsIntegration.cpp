#include "app/WindowsIntegration.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QSettings>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

namespace {

// The label Explorer shows for the Muffin context-menu verb. The registry
// stores a single (non-localized) string, so this is intentionally English.
const wchar_t* const kVerbLabel = L"Open with Muffin";
const wchar_t* const kProgId = L"Muffin.md";
const wchar_t* const kProgIdDescription = L"Muffin Markdown Document";
const wchar_t* const kAppName = L"Muffin";

// Marker subkey name under the verb; its (Default) value carries the icon
// path so the context-menu entry shows the Muffin icon.
const wchar_t* const kIconSubKey = L"Icon";
const wchar_t* const kCommandSubKey = L"command";

// HKCU\SOFTWARE\Classes is the per-user half of HKCR. Writing here needs no
// elevation and is read by Explorer for the current user.
#ifdef Q_OS_WIN
const wchar_t* const kClassesRoot = L"SOFTWARE\\Classes";
#endif

#ifdef Q_OS_WIN
std::wstring quotedExePath() {
  return L'"' + QCoreApplication::applicationFilePath().toStdWString() + L'"';
}

// Write (or overwrite) a string (REG_SZ) value. A null valueName writes the
// key's (Default) value.
void writeStringValue(HKEY root, const std::wstring& keyPath, const wchar_t* valueName,
                      const std::wstring& data) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
  RegSetValueExW(key, valueName, 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(data.c_str()), bytes);
  RegCloseKey(key);
}

// Write a zero-length REG_NONE value. OpenWithProgids expects the ProgID as a
// value name with empty REG_NONE data so the app appears in "Open with".
void writeNoneValue(HKEY root, const std::wstring& keyPath, const std::wstring& valueName) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(key, valueName.c_str(), 0, REG_NONE, nullptr, 0);
  RegCloseKey(key);
}

void deleteValue(HKEY root, const std::wstring& keyPath, const std::wstring& valueName) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, keyPath.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, valueName.c_str());
    RegCloseKey(key);
  }
}

// Recursively delete a key and all its subkeys.
void deleteKeyTree(HKEY root, const std::wstring& keyPath) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, keyPath.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
    wchar_t subkey[MAX_PATH];
    while (RegEnumKeyW(key, 0, subkey, MAX_PATH) == ERROR_SUCCESS) {
      deleteKeyTree(root, keyPath + L'\\' + subkey);
    }
    RegCloseKey(key);
  }
  RegDeleteKeyW(root, keyPath.c_str());
}

bool keyExists(HKEY root, const std::wstring& keyPath) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, keyPath.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
    RegCloseKey(key);
    return true;
  }
  return false;
}

std::wstring classesKey(const std::wstring& tail) {
  return std::wstring(kClassesRoot) + L'\\' + tail;
}

void notifyShellAssocChanged() {
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif  // Q_OS_WIN

}  // namespace

namespace muffin {

const QStringList& WindowsIntegration::markdownExtensions() {
  static const QStringList kExtensions = {QStringLiteral(".md"), QStringLiteral(".markdown"),
                                          QStringLiteral(".mdx"), QStringLiteral(".mkd"),
                                          QStringLiteral(".mdown")};
  return kExtensions;
}

void WindowsIntegration::applyIntegration(bool contextMenu, bool associate) {
#ifdef Q_OS_WIN
  const std::wstring exe = quotedExePath();
  const std::wstring iconValue = exe + L",0";

  // --- Context-menu verbs (files + folder + folder background) ---
  for (const QString& ext : markdownExtensions()) {
    const std::wstring verbPath = classesKey(ext.toStdWString() + L"\\shell\\Muffin");
    if (contextMenu) {
      writeStringValue(HKEY_CURRENT_USER, verbPath, nullptr, kVerbLabel);
      writeStringValue(HKEY_CURRENT_USER, verbPath + L'\\' + kIconSubKey, nullptr, iconValue);
      writeStringValue(HKEY_CURRENT_USER, verbPath + L'\\' + kCommandSubKey, nullptr,
                       exe + L" \"%1\"");
    } else {
      // Remove only Muffin's verb subtree; leave the rest of the extension's
      // shell key (other apps' verbs) and the extension key itself intact.
      deleteKeyTree(HKEY_CURRENT_USER, verbPath);
    }
  }

  const std::wstring dirVerb = classesKey(L"Directory\\shell\\Muffin");
  if (contextMenu) {
    writeStringValue(HKEY_CURRENT_USER, dirVerb, nullptr, kVerbLabel);
    writeStringValue(HKEY_CURRENT_USER, dirVerb + L'\\' + kIconSubKey, nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, dirVerb + L'\\' + kCommandSubKey, nullptr,
                     exe + L" --folder \"%1\"");
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, dirVerb);
  }

  // Directory\Background covers the right-click menu on the empty area of an
  // open folder window; it passes the current folder via %V (not %1).
  const std::wstring bgVerb = classesKey(L"Directory\\Background\\shell\\Muffin");
  if (contextMenu) {
    writeStringValue(HKEY_CURRENT_USER, bgVerb, nullptr, kVerbLabel);
    writeStringValue(HKEY_CURRENT_USER, bgVerb + L'\\' + kIconSubKey, nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, bgVerb + L'\\' + kCommandSubKey, nullptr,
                     exe + L" --folder \"%V\"");
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, bgVerb);
  }

  // --- File association (ProgID + OpenWithProgids candidate) ---
  const std::wstring progIdKey = classesKey(kProgId);
  if (associate) {
    writeStringValue(HKEY_CURRENT_USER, progIdKey, nullptr, kProgIdDescription);
    writeStringValue(HKEY_CURRENT_USER, progIdKey, L"FriendlyAppName", kAppName);
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\DefaultIcon", nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open", nullptr, L"Open");
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open\\command", nullptr,
                     exe + L" \"%1\"");
    for (const QString& ext : markdownExtensions()) {
      writeNoneValue(HKEY_CURRENT_USER, classesKey(ext.toStdWString() + L"\\OpenWithProgids"),
                     kProgId);
    }
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, progIdKey);
    for (const QString& ext : markdownExtensions()) {
      deleteValue(HKEY_CURRENT_USER, classesKey(ext.toStdWString() + L"\\OpenWithProgids"),
                  kProgId);
    }
  }

  notifyShellAssocChanged();
#else
  Q_UNUSED(contextMenu);
  Q_UNUSED(associate);
#endif
}

bool WindowsIntegration::isContextMenuEnabled() {
#ifdef Q_OS_WIN
  // The folder verb is the canonical marker that the context-menu set was
  // applied (it is always written whenever contextMenu is on).
  return keyExists(HKEY_CURRENT_USER, classesKey(L"Directory\\shell\\Muffin"));
#else
  return false;
#endif
}

bool WindowsIntegration::isAssociationEnabled() {
#ifdef Q_OS_WIN
  return keyExists(HKEY_CURRENT_USER, classesKey(kProgId));
#else
  return false;
#endif
}

void WindowsIntegration::openDefaultAppsSettings() {
#ifdef Q_OS_WIN
  // ms-settings: URIs are resolved by Windows 10+ shell; openUrl launches the
  // Settings app at the Default Apps page.
  QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps")));
#endif
}

bool WindowsIntegration::shouldPromptForDefault() {
#ifdef Q_OS_WIN
  return QSettings().value(QStringLiteral("integration/promptDefaultOnLaunch"), false).toBool();
#else
  return false;
#endif
}

void WindowsIntegration::clearPromptForDefault() {
#ifdef Q_OS_WIN
  QSettings().remove(QStringLiteral("integration/promptDefaultOnLaunch"));
#endif
}

}  // namespace muffin
