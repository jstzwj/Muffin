using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Muffin.Setup;
using WixToolset.Mba.Core;

internal static class UpdateHandoffTest
{
    internal static bool Child(string[] args)
    {
        if (args.Length != 2 || args[0] != "--handoff-child") return false;
        File.WriteAllText(args[1] + ".started", "started");
        var elapsed = Stopwatch.StartNew();
        while (!File.Exists(args[1] + ".exit") && elapsed.Elapsed.TotalSeconds < 30) Thread.Sleep(20);
        return true;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static IBootstrapperCommand Command(string arguments, LaunchAction action = LaunchAction.Install,
                                                Display display = Display.Passive)
    {
        return new BootstrapperCommand(action, display, arguments, 0, ResumeType.None,
                                       IntPtr.Zero, RelationType.None, false, null, null, null);
    }

    private static void RejectCommand(string arguments, LaunchAction action = LaunchAction.Install,
                                      Display display = Display.Passive)
    {
        var rejected = false;
        try { using (UpdateHandoff.FromCommand(Command(arguments, action, display))) { } }
        catch (ArgumentException) { rejected = true; }
        catch (FormatException) { rejected = true; }
        Require(rejected, "invalid update command never becomes an ordinary installation");
    }

    internal static void RunBundle(string bundle)
    {
        var directory = Path.Combine(Path.GetTempPath(), "MuffinBundleCommandTest-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var log = Path.Combine(directory, "bundle.log");
        // Full display is deliberate: even an old BA that misses MuffinUpdate
        // can only show its welcome/maintenance page. Never run a production
        // MSI in passive mode just to test a regression in its restart guard.
        using (var process = Process.Start(new ProcessStartInfo(Path.GetFullPath(bundle))
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            Arguments = "/install /norestart /log \"" + log + "\" MuffinUpdate=invalid",
        }))
        {
            if (!process.WaitForExit(60000))
            {
                process.CloseMainWindow();
                process.WaitForExit(10000);
                throw new InvalidOperationException("Bundle ignored the update command; logs: " + directory);
            }
            var logs = new StringBuilder();
            foreach (var path in Directory.GetFiles(directory, "*.log")) logs.Append(File.ReadAllText(path));
            var text = logs.ToString();
            Require(process.ExitCode == 1602 && text.Contains("Invalid update handoff:") &&
                    !text.Contains("Detect begin") && !text.Contains("Plan begin") && !text.Contains("Apply begin"),
                    "real Burn command is consumed and rejected before detection or installation; logs: " + directory);
        }
        Directory.Delete(directory, true);
        Console.WriteLine("Real setup bundle: update command consumed, invalid mode rejected before Detect/Plan/Apply");
    }

    internal static void Run()
    {
        Require(UpdateHandoff.FromCommand(Command(null)) == null &&
                UpdateHandoff.FromCommand(Command("/norestart OtherVariable=value")) == null,
                "ordinary installation has no editor handoff");
        foreach (var invalid in new[] { "MuffinUpdate=", "MuffinUpdate", "/MuffinUpdate", "/MuffinUpdate=bad",
                                        "--MuffinUpdate=bad", "MuffinUpdate=bad",
                                        "MuffinUpdate=first MuffinUpdate=second", "MuffinUpdate=first muffinupdate=second" })
            RejectCommand(invalid);
        RejectCommand("MuffinUpdate=bad", LaunchAction.Repair);
        RejectCommand("MuffinUpdate=bad", display: Display.Full);
        RejectCommand("MuffinUpdate=bad", display: Display.None);

        var directory = Path.Combine(Path.GetTempPath(), "MuffinHandoffTest-" + Guid.NewGuid().ToString("N") + " 中文路径");
        Directory.CreateDirectory(directory);
        var application = Path.Combine(directory, "Muffin.exe");
        File.Copy(Process.GetCurrentProcess().MainModule.FileName, application);
        try
        {
            foreach (var scenario in new[] { "commit", "cancel", "no-commit", "late-cancel", "retry" })
            {
                var stem = Path.Combine(directory, Guid.NewGuid().ToString("N"));
                var readyDirectory = stem + ".update";
                Directory.CreateDirectory(readyDirectory);
                var ready = Path.Combine(readyDirectory, scenario == "retry" ?
                                         "update-" + Guid.NewGuid().ToString("N") + ".ready" : "update.ready");
                using (var child = Process.Start(new ProcessStartInfo(application)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    Arguments = "--handoff-child \"" + stem + "\"",
                }))
                {
                    var elapsed = Stopwatch.StartNew();
                    while (!File.Exists(stem + ".started") && elapsed.ElapsedMilliseconds < 10000) Thread.Sleep(10);
                    Require(File.Exists(stem + ".started"), "fixture process started");
                    var token = child.Id + ":" + Convert.ToBase64String(Encoding.UTF8.GetBytes(application)) + ":" +
                                Convert.ToBase64String(Encoding.UTF8.GetBytes(ready));
                    // Exercise the same WiX command parser and BA entry as a
                    // real Burn invocation, not just the token decoder.
                    using (var handoff = UpdateHandoff.FromCommand(Command("/norestart \"MuffinUpdate=" + token + "\"")))
                    {
                        Task<bool> previous = null;
                        UpdateHandoff previousHandoff = null;
                        if (scenario == "retry")
                        {
                            var oldReady = Path.Combine(readyDirectory, "update-" + Guid.NewGuid().ToString("N") + ".ready");
                            var oldToken = child.Id + ":" + Convert.ToBase64String(Encoding.UTF8.GetBytes(application)) + ":" +
                                           Convert.ToBase64String(Encoding.UTF8.GetBytes(oldReady));
                            previousHandoff = UpdateHandoff.FromCommand(Command("MuffinUpdate=" + oldToken));
                            // A timed-out attempt can start after the retry.
                            // Its canceled decision must remain independent.
                            previousHandoff.Cancel();
                            previous = Task.Run(() => previousHandoff.WaitForExit());
                        }
                        var task = Task.Run(() => handoff.WaitForExit());
                        elapsed.Restart();
                        while (!File.Exists(ready) && elapsed.ElapsedMilliseconds < 10000) Thread.Sleep(10);
                        Require(File.Exists(ready) && !task.IsCompleted, "updater acknowledges readiness but waits for editor exit");
                        if (scenario == "cancel")
                        {
                            File.WriteAllText(ready + ".cancel", "cancel");
                            Require(task.Wait(10000) && !task.Result && !child.HasExited, "cancel never closes editor or permits installation");
                        }
                        if (scenario == "retry")
                        {
                            Require(previous.Wait(10000) && !previous.Result && !task.IsCompleted,
                                    "late canceled helper cannot acknowledge or cancel the new attempt");
                            previousHandoff.MarkFinished();
                            previousHandoff.Dispose();
                        }
                        if (scenario == "commit" || scenario == "late-cancel" || scenario == "retry")
                            File.WriteAllText(ready + ".decision", "install");
                        if (scenario == "late-cancel")
                            handoff.Cancel();
                        File.WriteAllText(stem + ".exit", "exit");
                        Require(child.WaitForExit(10000), "editor exits normally without forced termination");
                        Require(task.Wait(10000) && task.Result == (scenario == "commit" || scenario == "retry"),
                            "installation needs explicit approval and exit, and cancellation wins over approval");
                    }
                }
            }
            foreach (var invalid in new[] { "", "1:bad:bad", "0:a:b", "1:a:b:c" })
            {
                var rejected = false;
                try { using (UpdateHandoff.Parse(invalid)) { } }
                catch (ArgumentException) { rejected = true; }
                catch (FormatException) { rejected = true; }
                Require(rejected, "malformed handoff rejected");
            }
        }
        finally { Directory.Delete(directory, true); }
        Console.WriteLine("Update handoff: WiX command line, legacy/current markers, retry, wait, cancel and process identity passed");
    }
}
