using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Muffin.Setup;

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

    internal static void Run()
    {
        var directory = Path.Combine(Path.GetTempPath(), "MuffinHandoffTest-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var application = Path.Combine(directory, "Muffin.exe");
        File.Copy(Process.GetCurrentProcess().MainModule.FileName, application);
        try
        {
            foreach (var scenario in new[] { "commit", "cancel", "no-commit", "late-cancel" })
            {
                var stem = Path.Combine(directory, Guid.NewGuid().ToString("N"));
                var readyDirectory = stem + ".update";
                Directory.CreateDirectory(readyDirectory);
                var ready = Path.Combine(readyDirectory, "update.ready");
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
                    using (var handoff = UpdateHandoff.Parse(token))
                    {
                        var task = Task.Run(() => handoff.WaitForExit());
                        elapsed.Restart();
                        while (!File.Exists(ready) && elapsed.ElapsedMilliseconds < 10000) Thread.Sleep(10);
                        Require(File.Exists(ready) && !task.IsCompleted, "updater acknowledges readiness but waits for editor exit");
                        if (scenario == "cancel")
                        {
                            File.WriteAllText(ready + ".cancel", "cancel");
                            Require(task.Wait(10000) && !task.Result && !child.HasExited, "cancel never closes editor or permits installation");
                        }
                        if (scenario == "commit" || scenario == "late-cancel")
                            File.WriteAllText(ready + ".decision", "install");
                        if (scenario == "late-cancel")
                            handoff.Cancel();
                        File.WriteAllText(stem + ".exit", "exit");
                        Require(child.WaitForExit(10000), "editor exits normally without forced termination");
                        Require(task.Wait(10000) && task.Result == (scenario == "commit"),
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
        Console.WriteLine("Update handoff: wait, cancel, process identity and malformed input passed");
    }
}
