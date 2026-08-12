#pragma once

#include <QString>
#include <QStringList>

namespace muffin {

// Windows shell integration: Explorer context-menu verbs ("Open with Muffin"
// for files and folders) and file-association registration.
//
// Design notes
// ------------
// All writes target HKCU\SOFTWARE\Classes, so they need no elevation and the
// same schema is shared by two writers:
//   1. The MSI installer (WiX registry components under two optional Features,
//      toggled from the feature tree in the install wizard).
//   2. The in-app toggle on the Files preferences page (the methods below).
// Either writer produces the identical registry layout, so toggling in the UI
// after install reconciles cleanly (and the portable build can opt in too).
//
// "Default handler" caveat: Windows 8+ guards the UserChoice registry value
// with a hash, so no application can silently become the *default* for an
// extension. We therefore only register Muffin as an "Open with" candidate;
// openDefaultAppsSettings() launches the system Default Apps page so the user
// can confirm the default in one click. The MSI's File Associations feature
// additionally sets a one-shot launch flag (shouldPromptForDefault()) so the
// first run after an interactive install offers that redirect.
//
// On non-Windows builds every method is a no-op stub so callers (the
// preferences page, main()) compile unchanged.
class WindowsIntegration {
public:
  // Extensions registered as Muffin "Open with" candidates: .md, .markdown,
  // .mdx, .mkd, .mdown.
  static const QStringList& markdownExtensions();

  // Apply the persistent integration set to match the given desired state:
  //   contextMenu — Explorer "Open with Muffin" verbs for files and folders.
  //   associate   — register the Muffin.md ProgID + OpenWithProgids for each
  //                 extension (Muffin then appears in the "Open with" menu).
  // Keys that are turned off are removed; only Muffin-authored values are
  // touched. Explorer is notified to refresh associations immediately.
  static void applyIntegration(bool contextMenu, bool associate);

  // Query the current state from the registry (true if the corresponding
  // Muffin keys exist). Used to initialize the preferences UI from the single
  // source of truth rather than a separate preference mirror.
  static bool isContextMenuEnabled();
  static bool isAssociationEnabled();

  // Open the Windows "Default apps" settings page (ms-settings:defaultapps).
  // This is the only supported way on Windows 8+ to let the user make Muffin
  // the default editor for an extension.
  static void openDefaultAppsSettings();

  // One-shot gate the installer sets when the user keeps the File Associations
  // feature on (signalling "also make it the default"). main() checks this at
  // launch and, if set, calls openDefaultAppsSettings() once and clears it.
  static bool shouldPromptForDefault();
  static void clearPromptForDefault();
};

}  // namespace muffin
