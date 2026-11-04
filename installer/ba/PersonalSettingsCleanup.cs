using Microsoft.Win32;

namespace Muffin.Setup
{
    // This deliberately has no filesystem deletion. Notes, recovery drafts,
    // installed themes and replacement backups are user data, not preferences.
    internal static class PersonalSettingsCleanup
    {
        internal static bool ShouldClear(bool requested, int status, bool uninstall, bool relatedBundle)
            => requested && uninstall && !relatedBundle && (status == 0 || status == 3010 || status == 1641);

        internal static void Clear(RegistryKey user)
        {
            user.DeleteSubKeyTree(@"Software\Muffin\Muffin", false);
            using (var parent = user.OpenSubKey(@"Software\Muffin"))
            {
                if (parent == null || parent.SubKeyCount != 0 || parent.ValueCount != 0) return;
            }
            user.DeleteSubKey(@"Software\Muffin", false);
        }
    }
}
