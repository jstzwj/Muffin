using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using Microsoft.Win32;
using WixToolset.Mba.Core;

namespace Muffin.Setup
{
    public enum SetupPage
    {
        Detecting,
        Welcome,
        Maintenance,
        UninstallConfirm,
        Progress,
        Success,
        Failed,
    }

    public enum SetupIntent
    {
        Install,
        Update,
        Repair,
        Uninstall,
    }

    /// <summary>
    /// Presentation state machine for the setup UI. The BA marshals every
    /// engine callback onto the UI thread before touching this class, so the
    /// model itself can stay single-threaded.
    /// </summary>
    public class SetupModel : INotifyPropertyChanged
    {
        private readonly MuffinBa _ba;

        public SetupModel(MuffinBa ba)
        {
            _ba = ba;
            InstallCommand = new RelayCommand(_ => BeginIntent(SetupIntent.Install), _ => !IsWorking);
            RepairCommand = new RelayCommand(_ => BeginIntent(SetupIntent.Repair), _ => !IsWorking);
            AskUninstallCommand = new RelayCommand(_ => Page = SetupPage.UninstallConfirm, _ => !IsWorking);
            ConfirmUninstallCommand = new RelayCommand(_ => BeginIntent(SetupIntent.Uninstall), _ => !IsWorking);
            CancelUninstallCommand = new RelayCommand(_ => Page = SetupPage.Maintenance, _ => !IsWorking);
            LaunchCommand = new RelayCommand(_ => LaunchApp());
            CancelCommand = new RelayCommand(_ => Cancel(), _ => IsWorking);
            OpenLogCommand = new RelayCommand(_ => OpenLog());
            CloseCommand = new RelayCommand(_ => _ba.RequestShutdown());
        }

        public ICommand InstallCommand { get; }
        public ICommand RepairCommand { get; }
        public ICommand AskUninstallCommand { get; }
        public ICommand ConfirmUninstallCommand { get; }
        public ICommand CancelUninstallCommand { get; }
        public ICommand LaunchCommand { get; }
        public ICommand CancelCommand { get; }
        public ICommand OpenLogCommand { get; }
        public ICommand CloseCommand { get; }

        // ----- detect results -----

        public bool BundleInstalled { get; private set; }
        public bool IsUpdate { get; private set; }
        public bool NewerVersionInstalled { get; private set; }

        // ----- observable state -----

        private SetupPage _page = SetupPage.Detecting;
        public SetupPage Page
        {
            get => _page;
            private set => SetProperty(ref _page, value);
        }

        private SetupIntent _intent = SetupIntent.Install;
        public SetupIntent Intent
        {
            get => _intent;
            private set => SetProperty(ref _intent, value);
        }

        public string VersionText { get; private set; } = "";

        private int _progressPercent;
        public int ProgressPercent
        {
            get => _progressPercent;
            private set => SetProperty(ref _progressPercent, value);
        }

        private string _progressStage = "";
        public string ProgressStage
        {
            get => _progressStage;
            private set => SetProperty(ref _progressStage, value);
        }

        private string _progressDetail = "";
        public string ProgressDetail
        {
            get => _progressDetail;
            private set => SetProperty(ref _progressDetail, value);
        }

        private string _errorMessage = "";
        public string ErrorMessage
        {
            get => _errorMessage;
            private set => SetProperty(ref _errorMessage, value);
        }

        public string LogFilePath { get; private set; } = "";

        public bool RestartRecommended { get; private set; }

        public bool Uninstalled { get; private set; }

        private bool _addContextMenu = true;
        public bool AddContextMenu
        {
            get => _addContextMenu;
            set => SetProperty(ref _addContextMenu, value);
        }

        private bool _associateFiles = true;
        public bool AssociateFiles
        {
            get => _associateFiles;
            set => SetProperty(ref _associateFiles, value);
        }

        // ----- language selection -----

        /// <summary>"Auto" plus one native name per supported language.</summary>
        public string[] LanguageChoices { get; } =
            new[] { UiStrings.Get("LanguageAuto") }.Concat(UiStrings.LanguageNames).ToArray();

        private int _languageIndex;
        public int LanguageIndex
        {
            get => _languageIndex;
            set
            {
                if (_languageIndex == value)
                {
                    return;
                }
                _languageIndex = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LanguageIndex)));
                UiStrings.SetLanguage(value == 0 ? "auto" : UiStrings.LanguageCodes[value - 1]);
                RefreshStrings();
            }
        }

        /// <summary>Re-notify every localized string after a language switch.</summary>
        private void RefreshStrings()
        {
            RaisePropertyChanged(
                nameof(WindowTitle), nameof(DetectingText), nameof(WelcomeTagline),
                nameof(OptionsLabel), nameof(AddContextMenuLabel), nameof(AssociateFilesLabel),
                nameof(RepairLabel), nameof(UninstallLabel), nameof(InstalledTitle),
                nameof(InstalledSub), nameof(UninstallTitle), nameof(UninstallBody),
                nameof(KeepLabel), nameof(CancelLabel), nameof(SuccessInstalledTitle),
                nameof(SuccessRemovedTitle), nameof(RestartNoteText), nameof(LaunchLabel),
                nameof(CloseLabel), nameof(FailedTitle), nameof(OpenLogLabel),
                nameof(Heading), nameof(PrimaryLabel));
        }

        public bool IsWorking => Page == SetupPage.Detecting || Page == SetupPage.Progress;

        public bool ShowLaunch => Intent != SetupIntent.Uninstall;

        // ----- localized UI strings (single-shot bindings; language is fixed at startup) -----

        public string WindowTitle => UiStrings.Get("WindowTitle");
        public string DetectingText => UiStrings.Get("Detecting");
        public string WelcomeTagline => UiStrings.Get("WelcomeTagline");
        public string OptionsLabel => UiStrings.Get("Options");
        public string AddContextMenuLabel => UiStrings.Get("AddContextMenu");
        public string AssociateFilesLabel => UiStrings.Get("AssociateFiles");
        public string RepairLabel => UiStrings.Get("Repair");
        public string UninstallLabel => UiStrings.Get("Uninstall");
        public string InstalledTitle => UiStrings.Get("InstalledTitle");
        public string InstalledSub => string.Format(UiStrings.Get("InstalledSub"), VersionText);
        public string UninstallTitle => UiStrings.Get("UninstallTitle");
        public string UninstallBody => UiStrings.Get("UninstallBody");
        public string KeepLabel => UiStrings.Get("Keep");
        public string CancelLabel => UiStrings.Get("Cancel");
        public string SuccessInstalledTitle => UiStrings.Get("SuccessInstalled");
        public string SuccessRemovedTitle => UiStrings.Get("SuccessRemoved");
        public string RestartNoteText => UiStrings.Get("RestartNote");
        public string LaunchLabel => UiStrings.Get("Launch");
        public string CloseLabel => UiStrings.Get("Close");
        public string FailedTitle => UiStrings.Get("FailedTitle");
        public string OpenLogLabel => UiStrings.Get("OpenLog");

        public string Heading
        {
            get
            {
                switch (Page)
                {
                    case SetupPage.Detecting: return DetectingText;
                    case SetupPage.Progress:
                        switch (Intent)
                        {
                            case SetupIntent.Update: return UiStrings.Get("Updating");
                            case SetupIntent.Repair: return UiStrings.Get("Repairing");
                            case SetupIntent.Uninstall: return UiStrings.Get("Removing");
                            default: return UiStrings.Get("Installing");
                        }
                    default: return "";
                }
            }
        }

        public string PrimaryLabel => UiStrings.Get(Intent == SetupIntent.Update ? "Update" : "Install");

        // ----- engine callbacks (UI thread) -----

        public void OnDetectBegin(bool registered)
        {
            VersionText = _ba.Engine.GetVariableString("WixBundleVersion") ?? "";
            // Burn reports the padded four-part version (0.6.1.0); trim the
            // filler fourth field so the hero reads 0.6.1.
            if (VersionText.EndsWith(".0"))
            {
                VersionText = VersionText.Substring(0, VersionText.Length - 2);
            }
            BundleInstalled = registered;
            RaisePropertyChanged(nameof(InstalledSub));
        }

        public void OnDetectRelated(bool older, bool newer)
        {
            if (older)
            {
                IsUpdate = true;
            }
            if (newer)
            {
                NewerVersionInstalled = true;
            }
        }

        public void OnDetectComplete(bool quiet)
        {
            _ba.Engine.Log(LogLevel.Standard, string.Format(
                "Muffin BA deciding page: installed={0} isUpdate={1} newer={2} quiet={3}",
                BundleInstalled, IsUpdate, NewerVersionInstalled, quiet));
            if (NewerVersionInstalled && _ba.Command.Action == LaunchAction.Install)
            {
                _errorMessage = UiStrings.Get("NewerInstalled");
                Page = SetupPage.Failed;
                return;
            }

            if (quiet)
            {
                switch (_ba.Command.Action)
                {
                    case LaunchAction.Uninstall:
                        // Nothing registered under this bundle's provider key:
                        // exit cleanly instead of planning an empty uninstall.
                        if (BundleInstalled || IsUpdate)
                        {
                            BeginIntent(SetupIntent.Uninstall);
                        }
                        else
                        {
                            _ba.RequestShutdown();
                        }
                        return;
                    case LaunchAction.Repair: BeginIntent(SetupIntent.Repair); return;
                    default:
                        BeginIntent(BundleInstalled && !IsUpdate ? SetupIntent.Repair : SetupIntent.Install);
                        return;
                }
            }

            switch (_ba.Command.Action)
            {
                case LaunchAction.Uninstall:
                    if (BundleInstalled || IsUpdate)
                    {
                        Page = SetupPage.UninstallConfirm;
                    }
                    else
                    {
                        // Nothing to remove: exit cleanly like wixstdba does.
                        _ba.RequestShutdown();
                    }
                    return;
                case LaunchAction.Repair:
                    Page = SetupPage.Maintenance;
                    return;
                default:
                    if (BundleInstalled && !IsUpdate)
                    {
                        Page = SetupPage.Maintenance;
                    }
                    else
                    {
                        Page = SetupPage.Welcome;
                    }
                    return;
            }
        }

        public void OnApplyBegin()
        {
            Page = SetupPage.Progress;
            ProgressPercent = 0;
            ProgressStage = "";
            ProgressDetail = "";
        }

        public void OnProgress(int overallPercent)
        {
            if (Page != SetupPage.Progress)
            {
                Page = SetupPage.Progress;
            }
            ProgressPercent = overallPercent;
        }

        /// <summary>Coarse phase under the heading: cache / execute / register.</summary>
        public void OnStage(string stageKey)
        {
            ProgressStage = UiStrings.Get(stageKey);
        }

        /// <summary>
        /// Per-package execute step. Related-bundle packages (upgrade
        /// clean-up) get their own stage line; our MSI keeps the intent
        /// heading and shows the localized MSI message stream as detail.
        /// </summary>
        public void OnExecutePackageBegin(string packageId)
        {
            if (packageId != "MuffinMsi")
            {
                // Related-bundle clean-up during an upgrade.
                ProgressStage = UiStrings.Get("StageRemovePrevious");
            }
            ProgressDetail = "Muffin";
        }

        /// <summary>Live MSI progress messages, localized by msiexec itself.</summary>
        public void OnMsiMessage(string message)
        {
            if (!string.IsNullOrEmpty(message) && message.Length <= 200)
            {
                ProgressDetail = message;
            }
        }

        public void OnApplyComplete(int status)
        {
            LogFilePath = _ba.TryGetLogPath();
            if (status == 3010 || status == 1641)
            {
                RestartRecommended = true;
            }
            Uninstalled = Intent == SetupIntent.Uninstall;

            if (status == 0)
            {
                ErrorMessage = "";
                Page = SetupPage.Success;
            }
            else
            {
                ErrorMessage = string.Format(UiStrings.Get("ErrorFmt"), (uint)status);
                Page = SetupPage.Failed;
            }

            // Windowless runs (quiet + Add/Remove Programs' embedded mode)
            // must quit right after apply — there is no window to close.
            if (_ba.IsPassive || !_ba.HasWindow)
            {
                _ba.RequestShutdown();
            }
        }

        public void OnPlanFailed(int status)
        {
            LogFilePath = _ba.TryGetLogPath();
            ErrorMessage = string.Format(UiStrings.Get("PlanErrorFmt"), (uint)status);
            Page = SetupPage.Failed;
        }

        // ----- user actions -----

        private void BeginIntent(SetupIntent intent)
        {
            Intent = intent;
            Page = SetupPage.Progress;
            ProgressPercent = 0;
            ProgressDetail = "";

            var action = intent switch
            {
                SetupIntent.Update => LaunchAction.Install,
                SetupIntent.Repair => LaunchAction.Repair,
                SetupIntent.Uninstall => LaunchAction.Uninstall,
                _ => LaunchAction.Install,
            };
            _ba.Engine.Plan(action);
        }

        public void Cancel()
        {
            if (Page == SetupPage.Progress)
            {
                _ba.RequestCancel();
            }
        }

        private void LaunchApp()
        {
            var launched = false;
            try
            {
                // ARPINSTALLLOCATION is authoritative; fall back to the
                // default per-machine folder for installs made by an older
                // MSI that did not publish it.
                var dir = FindInstallDirectory()
                    ?? System.IO.Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                        "Muffin");
                if (!string.IsNullOrEmpty(dir))
                {
                    var exe = System.IO.Path.Combine(dir, "Muffin.exe");
                    if (System.IO.File.Exists(exe))
                    {
                        Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true });
                        launched = true;
                    }
                }
            }
            catch
            {
                // Launching is best-effort; never fail the setup UI here.
            }

            // The installer window leaves together with the app launch —
            // staying open after "Launch Muffin" reads as a stuck setup.
            if (launched)
            {
                _ba.RequestShutdown();
            }
        }

        private void OpenLog()
        {
            try
            {
                if (!string.IsNullOrEmpty(LogFilePath) && System.IO.File.Exists(LogFilePath))
                {
                    Process.Start(new ProcessStartInfo(LogFilePath) { UseShellExecute = true });
                }
            }
            catch
            {
                // Best-effort as well.
            }
        }

        /// <summary>
        /// The MSI publishes ARPINSTALLLOCATION, so the per-machine uninstall
        /// registration tells us where Muffin.exe lives without needing a
        /// Burn→MSI property round-trip. Entries without a location (older
        /// installs, the bundle's own registration) are skipped.
        /// </summary>
        private static string FindInstallDirectory()
        {
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Default })
            {
                using (var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (var uninstall = key.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
                {
                    if (uninstall == null)
                    {
                        continue;
                    }
                    foreach (var subName in uninstall.GetSubKeyNames())
                    {
                        using (var sub = uninstall.OpenSubKey(subName))
                        {
                            if (sub?.GetValue("DisplayName") as string != "Muffin")
                            {
                                continue;
                            }
                            var location = sub.GetValue("InstallLocation") as string;
                            if (!string.IsNullOrEmpty(location))
                            {
                                return location;
                            }
                        }
                    }
                }
            }
            return null;
        }

        // ----- plumbing -----

        public event PropertyChangedEventHandler PropertyChanged;

        private void SetProperty<T>(ref T field, T value, [CallerMemberName] string name = null)
        {
            if (Equals(field, value))
            {
                return;
            }
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
            // Derived presentation state.
            if (name == nameof(Page))
            {
                RaisePropertyChanged(nameof(IsWorking), nameof(Heading), nameof(ShowLaunch));
            }
            if (name == nameof(Intent))
            {
                RaisePropertyChanged(nameof(PrimaryLabel), nameof(ShowLaunch), nameof(Heading));
            }
        }

        private void RaisePropertyChanged(params string[] names)
        {
            foreach (var name in names)
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
            }
        }
    }
}
