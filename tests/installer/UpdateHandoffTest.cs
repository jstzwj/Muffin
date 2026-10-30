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

    private static bool IsRejectedBundleLog(int exitCode, string log)
    {
        var rejected = log.IndexOf("e000: Invalid update handoff:", StringComparison.Ordinal);
        var shutdown = log.IndexOf("i500: Shutting down,", StringComparison.Ordinal);
        var detect = log.IndexOf("i100: Detect begin", StringComparison.Ordinal);
        // Burn can run its own detection after i500, inside i502/i599 cleanup
        // (notably on elevated runners). That read-only engine cleanup is not
        // a BA Detect request and must not be mistaken for starting an update.
        return exitCode == 1602 && rejected >= 0 && shutdown > rejected &&
               (detect < 0 || detect > shutdown) &&
               !log.Contains("i200: Plan begin") && !log.Contains("i300: Apply begin");
    }

    internal static void RunBundle(string bundle)
    {
        var directory = Path.Combine(Environment.GetEnvironmentVariable("RUNNER_TEMP") ?? Path.GetTempPath(),
                                     "MuffinBundleCommandTest-" + Guid.NewGuid().ToString("N"));
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
            var rejected = IsRejectedBundleLog(process.ExitCode, text);
            if (!rejected)
            {
                Console.Error.WriteLine("Bundle exit code: {0} (0x{0:X8})", process.ExitCode);
                Console.Error.WriteLine(text);
            }
            Require(rejected, "real Burn command is rejected before BA detection or installation; logs: " + directory);
        }
        Directory.Delete(directory, true);
        Console.WriteLine("Real setup bundle: invalid mode rejected before BA Detect/Plan/Apply; Burn cleanup allowed");
    }

    internal static void Run()
    {
        const string rejection = "[0000:0000]e000: Invalid update handoff: ArgumentException\n";
        const string shutdown = "[0000:0000]i500: Shutting down, exit code: 0x642\n";
        const string detect = "[0000:0000]i100: Detect begin, 1 packages\n";
        const string cleanup = "[0000:0000]i502: Cleanup begin.\n";
        Require(IsRejectedBundleLog(1602, rejection + shutdown), "ordinary rejection is accepted");
        Require(IsRejectedBundleLog(1602, rejection + shutdown + cleanup + detect),
                "Burn's post-shutdown cleanup detection is accepted");
        Require(!IsRejectedBundleLog(1602, detect + rejection + shutdown), "BA detection before rejection is forbidden");
        Require(!IsRejectedBundleLog(1602, rejection + detect + shutdown), "BA detection before shutdown is forbidden");
        Require(!IsRejectedBundleLog(1602, rejection + shutdown + "i200: Plan begin"), "planning is always forbidden");
        Require(!IsRejectedBundleLog(1602, rejection + shutdown + "i300: Apply begin"), "applying is always forbidden");
        Require(!IsRejectedBundleLog(0, rejection + shutdown), "incorrect exit status is rejected");
        Require(!IsRejectedBundleLog(1602, shutdown), "missing handoff validation is rejected");
        Require(!IsRejectedBundleLog(1602, rejection), "missing shutdown boundary is rejected");

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
