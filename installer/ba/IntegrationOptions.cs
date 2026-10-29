using System;
using Microsoft.Win32;

namespace Muffin.Setup
{
    /// <summary>
    /// Shared registry contract with WindowsIntegration.cpp and Muffin.wxs.
    /// The MSI writes both DWORD choices transactionally, including zeroes,
    /// only when it installs the selected features successfully. The app
    /// writes the same values when its integration preferences change.
    /// </summary>
    internal sealed class IntegrationOptions
    {
        internal const string SettingsKey = @"Software\Muffin\Muffin\integration";
        internal const string ContextMenuKey = @"SOFTWARE\Classes\Directory\shell\Muffin";
        internal const string OpenWithKey = @"SOFTWARE\Classes\Muffin.md";

        public bool ContextMenu { get; private set; }
        public bool OpenWith { get; private set; }

        internal static IntegrationOptions Read(RegistryKey user, bool installed)
        {
            using (var settings = user.OpenSubKey(SettingsKey))
            using (var context = user.OpenSubKey(ContextMenuKey))
            using (var openWith = user.OpenSubKey(OpenWithKey))
            {
                var hasContext = context != null;
                var hasOpenWith = openWith != null;
                // A legacy install (or portable integration) may have no saved
                // choices. Its absent keys mean off, rather than first-install
                // defaults. Neither detection nor canceling setup writes here.
                var known = installed || hasContext || hasOpenWith;
                return new IntegrationOptions
                {
                    ContextMenu = Resolve(ReadChoice(settings, "contextMenu"), hasContext, known),
                    OpenWith = Resolve(ReadChoice(settings, "openWith"), hasOpenWith, known),
                };
            }
        }

        internal static bool Resolve(bool? saved, bool enabled, bool installed)
        {
            return saved ?? (installed ? enabled : true);
        }

        private static bool? ReadChoice(RegistryKey settings, string name)
        {
            var value = settings?.GetValue(name);
            // Restrict the schema to 0/1. Unexpected data is not permission to
            // turn integration on during an upgrade; use legacy detection.
            if (value is int number && (number == 0 || number == 1))
            {
                return number == 1;
            }
            return null;
        }
    }
}
