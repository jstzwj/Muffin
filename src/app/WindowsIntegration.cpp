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

const wchar_t* const kProgId = L"Muffin.md";
const wchar_t* const kAppName = L"Muffin";

QString contextMenuLabel() {
  return QCoreApplication::translate("muffin::WindowsIntegration", "Open with Muffin");
}

QString documentTypeLabel() {
  return QCoreApplication::translate("muffin::WindowsIntegration", "Muffin Markdown Document");
}

QString openLabel() {
  return QCoreApplication::translate("muffin::WindowsIntegration", "Open");
}

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
void writeStringValue(HKEY root, const std::wstring& keyPath, const wchar_t* valueName, const std::wstring& data) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
  RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), bytes);
  RegCloseKey(key);
}

// Open instead of creating: changing a language must not opt users back in.
bool updateExistingString(const std::wstring& keyPath, const wchar_t* name, const std::wstring& text) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    return false;
  DWORD type = 0, bytes = 0;
  bool same = false;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ &&
      bytes == (text.size() + 1) * sizeof(wchar_t)) {
    std::wstring previous(bytes / sizeof(wchar_t), L'\0');
    same = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(previous.data()), &bytes) ==
               ERROR_SUCCESS &&
           previous == text + L'\0';
  }
  const bool changed =
      !same && RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                              static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
  RegCloseKey(key);
  return changed;
}

// Write a zero-length REG_NONE value. OpenWithProgids expects the ProgID as a
// value name with empty REG_NONE data so the app appears in "Open with".
void writeNoneValue(HKEY root, const std::wstring& keyPath, const std::wstring& valueName) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
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
#endif // Q_OS_WIN

} // namespace

namespace muffin {

const QStringList& WindowsIntegration::markdownExtensions() {
  static const QStringList kExtensions = {QStringLiteral(".md"), QStringLiteral(".markdown"), QStringLiteral(".mdx"),
                                          QStringLiteral(".mkd"), QStringLiteral(".mdown")};
  return kExtensions;
}

void WindowsIntegration::applyIntegration(bool contextMenu, bool associate) {
#ifdef Q_OS_WIN
  const std::wstring exe = quotedExePath();
  const std::wstring iconValue = exe + L",0";
  const auto verbLabel = contextMenuLabel().toStdWString();

  // --- Context-menu verbs (files + folder + folder background) ---
  for (const QString& ext : markdownExtensions()) {
    const std::wstring verbPath = classesKey(ext.toStdWString() + L"\\shell\\Muffin");
    if (contextMenu) {
      writeStringValue(HKEY_CURRENT_USER, verbPath, nullptr, verbLabel);
      writeStringValue(HKEY_CURRENT_USER, verbPath, L"MUIVerb", verbLabel);
      writeStringValue(HKEY_CURRENT_USER, verbPath + L'\\' + kIconSubKey, nullptr, iconValue);
      writeStringValue(HKEY_CURRENT_USER, verbPath + L'\\' + kCommandSubKey, nullptr, exe + L" \"%1\"");
    } else {
      // Remove only Muffin's verb subtree; leave the rest of the extension's
      // shell key (other apps' verbs) and the extension key itself intact.
      deleteKeyTree(HKEY_CURRENT_USER, verbPath);
    }
  }

  const std::wstring dirVerb = classesKey(L"Directory\\shell\\Muffin");
  if (contextMenu) {
    writeStringValue(HKEY_CURRENT_USER, dirVerb, nullptr, verbLabel);
    writeStringValue(HKEY_CURRENT_USER, dirVerb, L"MUIVerb", verbLabel);
    writeStringValue(HKEY_CURRENT_USER, dirVerb + L'\\' + kIconSubKey, nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, dirVerb + L'\\' + kCommandSubKey, nullptr, exe + L" --folder \"%1\"");
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, dirVerb);
  }

  // Directory\Background covers the right-click menu on the empty area of an
  // open folder window; it passes the current folder via %V (not %1).
  const std::wstring bgVerb = classesKey(L"Directory\\Background\\shell\\Muffin");
  if (contextMenu) {
    writeStringValue(HKEY_CURRENT_USER, bgVerb, nullptr, verbLabel);
    writeStringValue(HKEY_CURRENT_USER, bgVerb, L"MUIVerb", verbLabel);
    writeStringValue(HKEY_CURRENT_USER, bgVerb + L'\\' + kIconSubKey, nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, bgVerb + L'\\' + kCommandSubKey, nullptr, exe + L" --folder \"%V\"");
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, bgVerb);
  }

  // --- File association (ProgID + OpenWithProgids candidate) ---
  const std::wstring progIdKey = classesKey(kProgId);
  if (associate) {
    const auto description = documentTypeLabel().toStdWString();
    writeStringValue(HKEY_CURRENT_USER, progIdKey, nullptr, description);
    writeStringValue(HKEY_CURRENT_USER, progIdKey, L"FriendlyTypeName", description);
    writeStringValue(HKEY_CURRENT_USER, progIdKey, L"FriendlyAppName", kAppName);
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\DefaultIcon", nullptr, iconValue);
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open", nullptr, openLabel().toStdWString());
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open", L"MUIVerb", openLabel().toStdWString());
    writeStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open\\command", nullptr, exe + L" \"%1\"");
    for (const QString& ext : markdownExtensions()) {
      writeNoneValue(HKEY_CURRENT_USER, classesKey(ext.toStdWString() + L"\\OpenWithProgids"), kProgId);
    }
  } else {
    deleteKeyTree(HKEY_CURRENT_USER, progIdKey);
    for (const QString& ext : markdownExtensions()) {
      deleteValue(HKEY_CURRENT_USER, classesKey(ext.toStdWString() + L"\\OpenWithProgids"), kProgId);
    }
  }

  notifyShellAssocChanged();
  // Shared with the MSI and bootstrapper. Store explicit zeroes as DWORDs:
  // a missing value means an older installation, not an unchecked option.
  QSettings settings;
  settings.setValue(QStringLiteral("integration/contextMenu"), isContextMenuEnabled() ? 1 : 0);
  settings.setValue(QStringLiteral("integration/openWith"), isAssociationEnabled() ? 1 : 0);
  settings.sync();
#else
  Q_UNUSED(contextMenu);
  Q_UNUSED(associate);
#endif
}

void WindowsIntegration::refreshLocalizedLabels() {
#ifdef Q_OS_WIN
  bool changed = false;
  const auto verb = contextMenuLabel().toStdWString();
  const auto updateVerb = [&](const std::wstring& key, const std::wstring& label) {
    changed |= updateExistingString(key, nullptr, label);
    changed |= updateExistingString(key, L"MUIVerb", label);
  };
  for (const auto& extension : markdownExtensions())
    updateVerb(classesKey(extension.toStdWString() + L"\\shell\\Muffin"), verb);
  updateVerb(classesKey(L"Directory\\shell\\Muffin"), verb);
  updateVerb(classesKey(L"Directory\\Background\\shell\\Muffin"), verb);
  const auto progId = classesKey(kProgId);
  const auto description = documentTypeLabel().toStdWString();
  changed |= updateExistingString(progId, nullptr, description);
  changed |= updateExistingString(progId, L"FriendlyTypeName", description);
  updateVerb(progId + L"\\shell\\open", openLabel().toStdWString());
  if (changed)
    notifyShellAssocChanged();
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

} // namespace muffin
