using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace Muffin.Setup
{
    // The downloaded, verified bundle is also the updater helper. It owns no
    // installed Qt DLLs and does not start MSI until the old process is gone.
    internal sealed class UpdateHandoff : IDisposable
    {
        private readonly Process _parent;
        private readonly string _application;
        private readonly string _ready;
        private readonly object _decisionLock = new object();
        private bool _parentExited;

        private UpdateHandoff(Process parent, string application, string ready)
        {
            _parent = parent;
            _application = application;
            _ready = ready;
        }

        internal static UpdateHandoff Parse(string token)
        {
            var parts = token.Split(':');
            if (parts.Length != 3 || !int.TryParse(parts[0], out var pid) || pid <= 0)
                throw new ArgumentException("Invalid update handoff.");
            var application = Path.GetFullPath(Encoding.UTF8.GetString(Convert.FromBase64String(parts[1])));
            var ready = Path.GetFullPath(Encoding.UTF8.GetString(Convert.FromBase64String(parts[2])));
            if (!string.Equals(Path.GetFileName(application), "Muffin.exe", StringComparison.OrdinalIgnoreCase) ||
                Path.GetFileName(ready) != "update.ready" || !Directory.Exists(Path.GetDirectoryName(ready)))
                throw new ArgumentException("Invalid update handoff paths.");
            var parent = Process.GetProcessById(pid);
            try
            {
                if (!string.Equals(Path.GetFullPath(parent.MainModule.FileName), application, StringComparison.OrdinalIgnoreCase))
                    throw new ArgumentException("Update parent does not match the installed application.");
                return new UpdateHandoff(parent, application, ready);
            }
            catch { parent.Dispose(); throw; }
        }

        internal void Cancel()
        {
            // CreateNew arbitrates with the editor's install decision. If
            // cancellation wins, the editor cannot commit a restart even if
            // it checked the cancel marker just before we wrote it.
            lock (_decisionLock)
            {
                try
                {
                    using (var decision = new FileStream(_ready + ".decision", FileMode.CreateNew, FileAccess.Write, FileShare.Read))
                    {
                        var bytes = Encoding.ASCII.GetBytes("cancel");
                        decision.Write(bytes, 0, bytes.Length);
                    }
                }
                catch (IOException) when (File.Exists(_ready + ".decision")) { }
                File.WriteAllText(_ready + ".cancel", "cancel");
            }
        }

        private string Decision()
        {
            try { return File.ReadAllText(_ready + ".decision"); }
            catch (IOException) { return null; }
        }

        internal bool WaitForExit(Func<bool> canceled = null)
        {
            if (File.Exists(_ready + ".cancel") || (canceled?.Invoke() ?? false))
            {
                Cancel();
                if (Decision() == "cancel")
                    return false;
            }
            File.WriteAllText(_ready, "ready");
            var elapsed = Stopwatch.StartNew();
            while (!_parent.WaitForExit(100))
            {
                if (elapsed.Elapsed.TotalSeconds > 120)
                    return false;
                if (File.Exists(_ready + ".cancel") || (canceled?.Invoke() ?? false))
                {
                    Cancel();
                    // If the editor won the decision, it is already exiting.
                    // Wait for it so Relaunch restores the canceled session.
                    if (Decision() == "cancel")
                        return false;
                }
            }
            _parentExited = true;
            // A process exit is not approval: the editor may have crashed or
            // the handoff may have timed out immediately before it exited.
            if (Decision() != "install" || File.Exists(_ready + ".cancel") || (canceled?.Invoke() ?? false))
                return false;
            // Refuse to update a copy that is still in use by another process.
            // Neither the editor nor the installer force-terminates writers.
            foreach (var process in Process.GetProcessesByName("Muffin"))
            {
                using (process)
                {
                    try
                    {
                        if (string.Equals(Path.GetFullPath(process.MainModule.FileName), _application, StringComparison.OrdinalIgnoreCase))
                            return false;
                    }
                    catch (InvalidOperationException) { }
                    catch (System.ComponentModel.Win32Exception) { return false; }
                }
            }
            return true;
        }

        internal void Relaunch(int status)
        {
            if (!_parentExited || !File.Exists(_application))
                return;
            // BA runs unelevated; Burn elevates its engine, not this launch.
            Process.Start(new ProcessStartInfo(_application)
            {
                UseShellExecute = false,
                WorkingDirectory = Path.GetDirectoryName(_application),
                Arguments = "--update-result " + unchecked((uint)status).ToString(System.Globalization.CultureInfo.InvariantCulture),
            });
        }

        internal void MarkFinished()
        {
            File.WriteAllText(Path.Combine(Path.GetDirectoryName(_ready), "update.finished"), "finished");
        }

        public void Dispose() { _parent.Dispose(); }
    }
}
