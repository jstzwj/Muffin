using System;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Muffin.Setup;

internal static class IntegrationOptionsTest
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void Check(RegistryKey root, bool installed, bool context, bool openWith)
    {
        var before = root.SubKeyCount;
        var options = IntegrationOptions.Read(root, installed);
        Require(options.ContextMenu == context && options.OpenWith == openWith, "restore expected integration choices");
        Require(root.SubKeyCount == before, "detection must not create settings or registrations");
    }

    private static void TestRegistryChoices()
    {
        var path = @"Software\MuffinInstallerTest\" + Guid.NewGuid().ToString("N");
        try
        {
            using (var root = Registry.CurrentUser.CreateSubKey(path))
            {
                Check(root, false, true, true); // genuinely new installation
                Check(root, true, false, false); // old installation with both options unchecked
                using (var context = root.CreateSubKey(IntegrationOptions.ContextMenuKey))
                {
                    context.SetValue("", "Open with Muffin");
                }
                Check(root, true, true, false);
                Check(root, false, true, false); // portable integration / an older bare MSI
                using (var openWith = root.CreateSubKey(IntegrationOptions.OpenWithKey))
                {
                    openWith.SetValue("", "Muffin Markdown Document");
                }
                Check(root, true, true, true);
                root.DeleteSubKeyTree(IntegrationOptions.ContextMenuKey);
                Check(root, true, false, true);

                using (var settings = root.CreateSubKey(IntegrationOptions.SettingsKey))
                {
                    for (var context = 0; context <= 1; ++context)
                    for (var openWith = 0; openWith <= 1; ++openWith)
                    {
                        settings.SetValue("contextMenu", context, RegistryValueKind.DWord);
                        settings.SetValue("openWith", openWith, RegistryValueKind.DWord);
                        Check(root, true, context != 0, openWith != 0);
                        Check(root, false, context != 0, openWith != 0);
                    }
                    settings.SetValue("contextMenu", 7, RegistryValueKind.DWord);
                    settings.SetValue("openWith", "invalid", RegistryValueKind.String);
                    Check(root, true, false, true); // malformed values use the actual legacy registrations
                    settings.SetValue("contextMenu", 0, RegistryValueKind.DWord);
                    settings.DeleteValue("openWith");
                    Check(root, true, false, true); // independently restore each option
                }
            }
        }
        finally
        {
            Registry.CurrentUser.DeleteSubKeyTree(path, false);
        }
    }

    // Optional package verification runs only against an already-built MSI.
    // Open it ignoring the installed product, cost its features, and evaluate
    // actions without ever executing InstallInitialize or changing the system.
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiOpenPackageEx(string path, uint options, out uint handle);
    [DllImport("msi.dll")]
    private static extern uint MsiSetInternalUI(uint level, IntPtr parent);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiSetProperty(uint handle, string name, string value);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern int MsiEvaluateCondition(uint handle, string condition);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiDoAction(uint handle, string action);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiGetProperty(uint handle, string name, StringBuilder text, ref uint size);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiGetActiveDatabase(uint handle);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiDatabaseOpenView(uint database, string query, out uint view);
    [DllImport("msi.dll")]
    private static extern uint MsiViewExecute(uint view, uint record);
    [DllImport("msi.dll")]
    private static extern uint MsiViewFetch(uint view, out uint record);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    private static extern uint MsiRecordGetString(uint record, uint field, StringBuilder text, ref uint size);
    [DllImport("msi.dll")]
    private static extern uint MsiCloseHandle(uint handle);

    private static string Scalar(uint database, string sql)
    {
        Require(MsiDatabaseOpenView(database, sql, out var view) == 0, "open MSI table query");
        try
        {
            Require(MsiViewExecute(view, 0) == 0, "execute MSI table query");
            Require(MsiViewFetch(view, out var row) == 0, "fetch MSI table row");
            try
            {
                var text = new StringBuilder(4096);
                uint size = (uint)text.Capacity;
                Require(MsiRecordGetString(row, 1, text, ref size) == 0, "read MSI table field");
                return text.ToString();
            }
            finally { MsiCloseHandle(row); }
        }
        finally { MsiCloseHandle(view); }
    }

    private static void TestPackage(string path)
    {
        MsiSetInternalUI(2, IntPtr.Zero); // INSTALLUILEVEL_NONE: test failures must never open MSI dialogs
        Require(MsiOpenPackageEx(path, 1, out var package) == 0, "open MSI without installing");
        try
        {
            var database = MsiGetActiveDatabase(package);
            try
            {
                var initialize = int.Parse(Scalar(database, "SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action`='InstallInitialize'"));
                var removePrevious = int.Parse(Scalar(database, "SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action`='RemoveExistingProducts'"));
                var installFiles = int.Parse(Scalar(database, "SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action`='InstallFiles'"));
                Require(initialize < removePrevious && removePrevious < installFiles,
                    "old product removal participates in rollback before new files are installed");
                var contextCondition = Scalar(database, "SELECT `Condition` FROM `Condition` WHERE `Feature_`='ShellContextMenu'");
                var openCondition = Scalar(database, "SELECT `Condition` FROM `Condition` WHERE `Feature_`='FileAssociations'");
                foreach (var installed in new[] { false, true })
                foreach (var hasContext in new[] { false, true })
                foreach (var hasOpen in new[] { false, true })
                foreach (var savedContext in new[] { "", "#0", "#1", "invalid" })
                foreach (var savedOpen in new[] { "", "#0", "#1", "invalid" })
                {
                    MsiSetProperty(package, "WIX_UPGRADE_DETECTED", installed ? "old-product" : "");
                    MsiSetProperty(package, "Installed", "");
                    MsiSetProperty(package, "MUFFINCONTEXTMENUFOUND", hasContext ? "Open with Muffin" : "");
                    MsiSetProperty(package, "MUFFINOPENWITHFOUND", hasOpen ? "Muffin Markdown Document" : "");
                    MsiSetProperty(package, "MUFFINCONTEXTMENUSAVED", savedContext);
                    MsiSetProperty(package, "MUFFINOPENWITHSAVED", savedOpen);
                    var known = installed || hasContext || hasOpen;
                    bool? context = savedContext == "#0" ? false : savedContext == "#1" ? true : (bool?)null;
                    bool? open = savedOpen == "#0" ? false : savedOpen == "#1" ? true : (bool?)null;
                    Require(MsiEvaluateCondition(package, contextCondition) == (IntegrationOptions.Resolve(context, hasContext, known) ? 0 : 1),
                        "MSI and setup UI agree on context-menu defaults");
                    Require(MsiEvaluateCondition(package, openCondition) == (IntegrationOptions.Resolve(open, hasOpen, known) ? 0 : 1),
                        "MSI and setup UI agree on Open with defaults");
                }
                Require(Scalar(database, "SELECT `Value` FROM `Registry` WHERE `Name`='contextMenu'") == "#[MUFFINCONTEXTMENUSTATE]",
                    "MSI stores context-menu choice as a transactional DWORD");
                Require(Scalar(database, "SELECT `Value` FROM `Registry` WHERE `Name`='openWith'") == "#[MUFFINOPENWITHSTATE]",
                    "MSI stores Open with choice as a transactional DWORD");
                Require(Scalar(database, "SELECT `Feature_Parent` FROM `Feature` WHERE `Feature`='ShellContextMenu'") == "MainFeature",
                    "integration features remain optional children of the required application");
                var settingsComponent = Scalar(database, "SELECT `Component_` FROM `Registry` WHERE `Name`='contextMenu'");
                Require(Scalar(database, "SELECT `Feature_` FROM `FeatureComponents` WHERE `Component_`='" + settingsComponent + "'") == "MainFeature",
                    "unchecked choices are saved even when neither integration feature is installed");
                Console.WriteLine("MSI / setup agreement: 128 choice combinations passed");
            }
            finally { MsiCloseHandle(database); }
        }
        finally { MsiCloseHandle(package); }

        // Explicit selections must override saved defaults in both directions.
        foreach (var context in new[] { false, true })
        foreach (var openWith in new[] { false, true })
        {
            Require(MsiOpenPackageEx(path, 1, out package) == 0, "open package for feature costing");
            try
            {
                MsiSetProperty(package, "MUFFINCONTEXTMENUSAVED", context ? "#0" : "#1");
                MsiSetProperty(package, "MUFFINOPENWITHSAVED", openWith ? "#0" : "#1");
                MsiSetProperty(package, "ADDLOCAL", "MainFeature" + (context ? ",ShellContextMenu" : "") + (openWith ? ",FileAssociations" : ""));
                MsiSetProperty(package, "REMOVE", (context ? "" : "ShellContextMenu") + (!context && !openWith ? "," : "") + (openWith ? "" : "FileAssociations"));
                foreach (var action in new[] { "CostInitialize", "FileCost", "CostFinalize" })
                    Require(MsiDoAction(package, action) == 0, "cost explicit feature choices: " + action);
                var database = MsiGetActiveDatabase(package);
                try
                {
                    foreach (var choice in new[] { Tuple.Create("MUFFINCONTEXTMENUSTATE", context), Tuple.Create("MUFFINOPENWITHSTATE", openWith) })
                    {
                        var action = Scalar(database, "SELECT `Action` FROM `CustomAction` WHERE `Source`='" + choice.Item1 + "'");
                        var condition = Scalar(database, "SELECT `Condition` FROM `InstallExecuteSequence` WHERE `Action`='" + action + "'");
                        var enabled = MsiEvaluateCondition(package, condition);
                        Require(enabled == (choice.Item2 ? 1 : 0), "final feature state overrides previous preference");
                        if (enabled == 1)
                            Require(MsiDoAction(package, action) == 0, "prepare remembered choice without installing");
                        var text = new StringBuilder(8);
                        uint size = (uint)text.Capacity;
                        Require(MsiGetProperty(package, choice.Item1, text, ref size) == 0 && text.ToString() == (choice.Item2 ? "1" : "0"),
                            "transaction will save the final selected feature state");
                    }
                }
                finally { MsiCloseHandle(database); }
            }
            finally { MsiCloseHandle(package); }
        }
        Console.WriteLine("MSI explicit feature selection: all 4 combinations passed without installing");
    }

    public static int Main(string[] args)
    {
        if (UpdateHandoffTest.Child(args)) return 0;
        try
        {
            UpdateHandoffTest.Run();
            TestRegistryChoices();
            if (args.Length > 0) TestPackage(args[0]);
            Console.WriteLine("Installer integration choice tests passed");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
