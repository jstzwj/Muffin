using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Interop;
using Muffin.Setup.Ui;

namespace Muffin.Setup
{
    public partial class MainWindow : Window
    {
        private readonly SetupModel _model;
        private bool _closeIssued;

        public MainWindow(SetupModel model)
        {
            _model = model;
            // Surface any UI-thread exception in a file we can read back;
            // otherwise a dead render thread leaves an invisible 0x0 window.
            System.Windows.Threading.Dispatcher.CurrentDispatcher.UnhandledException += (s, e) =>
            {
                try
                {
                    System.IO.File.AppendAllText(
                        System.IO.Path.Combine(System.IO.Path.GetTempPath(), "muffin-ba-ui.log"),
                        e.Exception.ToString() + "\n");
                }
                catch
                {
                }
            };
            InitializeComponent();

            var dark = Win32Interop.SystemPrefersDarkTheme();
            var palette = new ResourceDictionary
            {
                Source = new Uri($"pack://application:,,,/MuffinBootstrapperUI;component/Ui/Palette.{(dark ? "Dark" : "Light")}.xaml"),
            };
            Resources.MergedDictionaries.Add(palette);

            DataContext = _model;

            SourceInitialized += (s, e) =>
                Win32Interop.ApplyModernChrome(new WindowInteropHelper(this).Handle, dark);
        }

        /// <summary>Native HWND handed to Engine.Apply for UAC/Splash suppression.</summary>
        public IntPtr Handle => new WindowInteropHelper(this).Handle;

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            RequestClose();
        }

        /// <summary>
        /// While the engine is applying, a close request cancels the apply and
        /// waits for ApplyComplete; actually closing mid-apply would tear the
        /// BA down under the engine.
        /// </summary>
        public void RequestClose()
        {
            if (_model.IsWorking)
            {
                _closeIssued = true;
                _model.Cancel();
                CloseButton.IsEnabled = false;
                return;
            }
            Close();
        }

        protected override void OnClosing(CancelEventArgs e)
        {
            if (_model.IsWorking && !_closeIssued)
            {
                e.Cancel = true;
                RequestClose();
                return;
            }
            if (_model.IsWorking && _closeIssued)
            {
                // Waiting for ApplyComplete after the cancel above.
                e.Cancel = true;
                return;
            }
            base.OnClosing(e);
        }
    }
}
