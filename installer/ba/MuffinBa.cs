using System;
using System.Windows.Threading;
using WixToolset.Mba.Core;

namespace Muffin.Setup
{
    /// <summary>
    /// Muffin's custom bootstrapper application: drives the Burn engine
    /// (Detect → Plan → Apply) behind a modern WPF shell.
    ///
    /// Engine callbacks arrive on a Burn worker thread; everything the model
    /// sees is marshalled onto the UI dispatcher (or run inline when the
    /// display is fully quiet and no window exists).
    /// </summary>
    public class MuffinBa : BootstrapperApplication
    {
        private readonly IBootstrapperCommand _command;
        private MainWindow _window;
        private System.Windows.Interop.HwndSource _hiddenSource;
        private IntPtr _applyHandle;
        private SetupModel _model;
        private Dispatcher _dispatcher;
        private volatile bool _cancelRequested;
        private int _exitCode;

        public MuffinBa(IEngine engine, IBootstrapperCommand command)
            : base(engine)
        {
            _command = command;
        }

        public IEngine Engine => base.engine;

        public IBootstrapperCommand Command => _command;

        /// <summary>True when the run shows a window (Full/Passive display).</summary>
        public bool HasWindow => ShowUi;

        /// <summary>Passive: show progress but take no input. Full: interactive.</summary>
        public bool IsPassive => _command.Display == Display.Passive;

        /// <summary>
        /// Full: interactive window. Passive: progress-only window.
        /// Embedded (uninstall from Add/Remove Programs) and None (quiet):
        /// headless — the engine drives everything and ARP shows its own UI.
        /// </summary>
        private bool ShowUi => _command.Display == Display.Full || IsPassive;

        protected override void Run()
        {
            Engine.Log(LogLevel.Standard, "Muffin BA starting");
            AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            {
                try
                {
                    Engine.Log(LogLevel.Standard, "Muffin BA unhandled exception: " + e.ExceptionObject);
                }
                catch
                {
                }
            };
            UiStrings.Initialize();
            _dispatcher = Dispatcher.CurrentDispatcher;
            _model = new SetupModel(this);

            if (ShowUi)
            {
                _window = new MainWindow(_model);
                _window.Closed += (s, e) => _dispatcher.InvokeShutdown();
                _window.Show();
            }
            else
            {
                // Quiet mode: Burn still needs a valid parent HWND (UAC
                // prompts, MSI internal UI). A message-only window stays
                // invisible while providing one.
                var parameters = new System.Windows.Interop.HwndSourceParameters("MuffinBAHidden")
                {
                    ParentWindow = new IntPtr(-3),  // HWND_MESSAGE
                };
                _hiddenSource = new System.Windows.Interop.HwndSource(parameters);
                _applyHandle = _hiddenSource.Handle;
            }

            Engine.Detect();

            Dispatcher.Run();
            Engine.Quit(_exitCode);
        }

        /// <summary>Close the UI (if any) and let Run() return the stored exit code.</summary>
        public void RequestShutdown()
        {
            Engine.Log(LogLevel.Standard, "Muffin BA request shutdown");
            if (_window != null)
            {
                _window.Dispatcher.BeginInvoke(new Action(_window.Close));
            }
            else
            {
                _dispatcher.BeginInvokeShutdown(System.Windows.Threading.DispatcherPriority.Background);
            }
        }

        /// <summary>
        /// WiX v4 has no Engine.Cancel(); cancellation is expressed by setting
        /// Cancel=true on the next cache/execute Begin event, so remember the
        /// user's intent here and let the handlers act on it.
        /// </summary>
        public void RequestCancel()
        {
            _cancelRequested = true;
        }

        public string TryGetLogPath()
        {
            try
            {
                return Engine.GetVariableString("WixBundleLog");
            }
            catch
            {
                return null;
            }
        }

        // ----- engine event wiring -----

        private void OnUi(Action action)
        {
            if (_window != null)
            {
                _window.Dispatcher.Invoke(action);
            }
            else
            {
                action();
            }
        }

        protected override void OnDetectBegin(DetectBeginEventArgs e)
        {
            OnUi(() => _model.OnDetectBegin(e.RegistrationType != RegistrationType.None));
        }

        protected override void OnDetectRelatedBundle(DetectRelatedBundleEventArgs e)
        {
            // Upgrade = an older related bundle our run replaces ("Update").
            // Otherwise compare versions directly to spot an installed newer
            // bundle: Burn refuses the downgrade at plan time, so surface it
            // immediately instead.
            var older = e.RelationType == RelationType.Upgrade;
            var newer = false;
            if (!older && !string.IsNullOrEmpty(e.Version))
            {
                try
                {
                    newer = Engine.CompareVersions(e.Version, Engine.GetVariableString("WixBundleVersion")) > 0;
                }
                catch
                {
                    // Uncomparable version strings: let plan-time decide.
                }
            }
            OnUi(() => _model.OnDetectRelated(older, newer));
        }

        protected override void OnPlanMsiFeature(PlanMsiFeatureEventArgs e)
        {
            // Feature selection: burn owns ADDLOCAL/REMOVE; the BA answers
            // per-feature queries from the welcome-page checkboxes. Only
            // speak up for installs/updates — repair and uninstall keep the
            // recommended (already-installed) states.
            if (_model.Intent != SetupIntent.Install && _model.Intent != SetupIntent.Update)
            {
                return;
            }

            var wanted = e.FeatureId switch
            {
                "MainFeature" => true,
                "ShellContextMenu" => _model.AddContextMenu,
                "FileAssociations" => _model.AssociateFiles,
                _ => e.RecommendedState == FeatureState.Local,
            };
            e.State = wanted ? FeatureState.Local : FeatureState.Absent;
        }

        protected override void OnDetectComplete(DetectCompleteEventArgs e)
        {
            Engine.Log(LogLevel.Standard, string.Format(
                "Muffin BA detect complete: display={0} action={1} showUi={2}",
                _command.Display, _command.Action, ShowUi));
            OnUi(() => _model.OnDetectComplete(!ShowUi));
        }

        protected override void OnPlanComplete(PlanCompleteEventArgs e)
        {
            if (e.Status != 0)
            {
                OnUi(() => _model.OnPlanFailed(e.Status));
                return;
            }

            var hwnd = _applyHandle;
            if (_window != null)
            {
                hwnd = _window.Dispatcher.Invoke(new Func<IntPtr>(() => _window.Handle));
            }
            Engine.Apply(hwnd);
        }

        protected override void OnApplyBegin(ApplyBeginEventArgs e)
        {
            Engine.Log(LogLevel.Standard, "Muffin BA on apply begin");
            OnUi(() => _model.OnApplyBegin());
        }

        protected override void OnCacheBegin(CacheBeginEventArgs e)
        {
            if (_cancelRequested)
            {
                e.Cancel = true;
            }
            OnUi(() => _model.OnStage("StageCache"));
        }

        protected override void OnExecuteBegin(ExecuteBeginEventArgs e)
        {
            if (_cancelRequested)
            {
                e.Cancel = true;
            }
        }

        protected override void OnExecutePackageBegin(ExecutePackageBeginEventArgs e)
        {
            if (_cancelRequested)
            {
                e.Cancel = true;
            }
            if (e.ShouldExecute)
            {
                OnUi(() => _model.OnExecutePackageBegin(e.PackageId));
            }
        }

        protected override void OnExecuteMsiMessage(ExecuteMsiMessageEventArgs e)
        {
            OnUi(() => _model.OnMsiMessage(e.Message));
        }

        protected override void OnRegisterBegin(RegisterBeginEventArgs e)
        {
            OnUi(() => _model.OnStage("StageRegister"));
        }

        protected override void OnUnregisterBegin(UnregisterBeginEventArgs e)
        {
            OnUi(() => _model.OnStage("StageUnregister"));
        }

        protected override void OnApplyComplete(ApplyCompleteEventArgs e)
        {
            Engine.Log(LogLevel.Standard, string.Format(
                "Muffin BA on apply complete: status=0x{0:X8} thread={1}",
                (uint)e.Status, System.Threading.Thread.CurrentThread.ManagedThreadId));
            _exitCode = e.Status;
            OnUi(() => _model.OnApplyComplete(e.Status));
            Engine.Log(LogLevel.Standard, "Muffin BA apply complete handled");
        }

        protected override void OnShutdown(ShutdownEventArgs e)
        {
            // Engine-initiated shutdown (e.g. /quiet with nothing to do).
            _dispatcher.InvokeShutdown();
        }

        private int _lastLoggedPercent = -100;

        protected override void OnProgress(ProgressEventArgs e)
        {
            // Progress fires constantly; log coarse steps only.
            if (e.OverallPercentage - _lastLoggedPercent >= 25 || e.OverallPercentage >= 100)
            {
                _lastLoggedPercent = e.OverallPercentage;
                Engine.Log(LogLevel.Standard, "Muffin BA progress " + e.OverallPercentage + "%");
            }
            OnUi(() => _model.OnProgress(e.OverallPercentage));
        }
    }
}
