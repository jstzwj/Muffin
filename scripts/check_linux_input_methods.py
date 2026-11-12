#!/usr/bin/env python3
"""Check a relocated Linux release, then type through Fcitx into both editors.

Run under dbus-run-session -- xvfb-run -a. The daemon comes from the OS; the
Qt input context must come from our bundle. No Conan/Qt library environment is
inherited. The probe is a test tool and is never included in a release archive.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import time


def run(command, env, **kwargs):
    result = subprocess.run(command, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout}")
    return result.stdout


def check_libraries(bundle, env):
    binaries = {p.resolve() for p in bundle.rglob("*.so*") if p.is_file()}
    binaries.update(bundle / name for name in ("Muffin", "muffin-mmdc", "ime-probe"))
    for binary in sorted(binaries):
        if not binary.resolve().is_relative_to(bundle.resolve()):
            raise RuntimeError(f"Library symlink escaped the bundle: {binary}")
        output = run(["ldd", "-r", str(binary)], env)
        if "not found" in output or "undefined symbol:" in output:
            raise RuntimeError(f"Unresolved symbols in {binary.name}:\n{output}")
        # ldd does not quote paths. Match the trailing load address rather
        # than splitting on whitespace: portable directories may have spaces.
        for library in re.findall(r"^\s*libQt6\S+\s+=>\s+(.+?)\s+\(0x[0-9a-fA-F]+\)\s*$",
                                  output, re.MULTILINE):
            if not Path(library).resolve().is_relative_to(bundle.resolve()):
                raise RuntimeError(f"Qt escaped the bundle: {library}")
        rpath = run(["patchelf", "--print-rpath", str(binary)], env).strip()
        if any(path and not path.startswith("$ORIGIN") for path in rpath.split(":")):
            raise RuntimeError(f"Non-relocatable RPATH in {binary.name}: {rpath}")


def wait_for_window(process, env):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Input probe exited before showing a window: {process.returncode}")
        result = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Muffin IME probe$"],
                                env=env, text=True, capture_output=True, timeout=5)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.splitlines()[-1]
        time.sleep(0.1)
    raise RuntimeError("Input probe did not show a window")


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def wait_for_fcitx(daemon, env, daemon_log, timeout=10):
    # State 0 means no focused input context, not a failed daemon. Check the
    # service owner and configured group instead. --check also prevents the
    # readiness query from D-Bus-activating a second daemon during startup.
    deadline = time.monotonic() + timeout
    last_reply = ""
    while daemon.poll() is None:
        group = subprocess.run(["fcitx5-remote", "--check", "-q"], env=env,
                               capture_output=True, text=True, timeout=5)
        if group.returncode == 0 and group.stdout.strip() == "Default":
            return
        last_reply = group.stdout + group.stderr
        if time.monotonic() >= deadline:
            break
        time.sleep(0.1)
    daemon_log.seek(0)
    raise RuntimeError(f"Fcitx service did not become ready (exit={daemon.poll()}):\n"
                       + last_reply + daemon_log.read())


def wait_for_input_context(process, env, timeout=10):
    deadline = time.monotonic() + timeout
    state_text = ""
    engine = ""
    while process.poll() is None:
        state = subprocess.run(["fcitx5-remote", "--check"], env=env,
                               capture_output=True, text=True, timeout=5)
        state_text = state.stdout.strip()
        # Wait for Qt's asynchronous FocusIn/CreateInputContext to finish
        # before activating Pinyin; a controller call with no context is a no-op.
        if state.returncode == 0 and state_text in {"1", "2"}:
            run(["fcitx5-remote", "--check", "-o"], env)
            run(["fcitx5-remote", "--check", "-s", "pinyin"], env)
            engine = run(["fcitx5-remote", "--check", "-n"], env).strip()
            if engine == "pinyin" and run(["fcitx5-remote", "--check"], env).strip() == "2":
                return
        if time.monotonic() >= deadline:
            break
        time.sleep(0.1)
    raise RuntimeError(f"Fcitx input context did not become active "
                       f"(probe exit={process.poll()}, state={state_text!r}, engine={engine!r})")


def check_input(bundle, env, work):
    profile = Path(env["XDG_CONFIG_HOME"]) / "fcitx5/profile"
    profile.parent.mkdir(parents=True)
    profile.write_text("""[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=pinyin
Layout=

[GroupOrder]
0=Default
""", encoding="utf-8")
    with (work / "fcitx.log").open("w+") as daemon_log:
        daemon = subprocess.Popen(["fcitx5", "--replace"], env=env,
                                  stdout=daemon_log, stderr=subprocess.STDOUT)
        try:
            wait_for_fcitx(daemon, env, daemon_log)
            print("Fcitx service ready; no focused editor is required yet", flush=True)
            for mode in ("source", "rendered"):
                report = work / f"{mode}.json"
                with (work / f"{mode}.log").open("w+") as log:
                    probe_env = dict(env, QT_IM_MODULE="fcitx", QT_DEBUG_PLUGINS="1")
                    process = subprocess.Popen([str(bundle / "ime-probe"), mode, str(report)],
                                               cwd=work, env=probe_env,
                                               stdout=log, stderr=subprocess.STDOUT)
                    try:
                        window = wait_for_window(process, env)
                        run(["xdotool", "windowfocus", "--sync", window], env)
                        wait_for_input_context(process, env)
                        run(["xdotool", "type", "--clearmodifiers", "--delay", "150", "nihao"], env)
                        run(["xdotool", "key", "space"], env)
                        if process.wait(timeout=25) != 0:
                            raise RuntimeError(f"{mode} input probe failed: {process.returncode}")
                        result = json.loads(report.read_text(encoding="utf-8"))
                        if not result["passed"] or not result["preedit"] or result["text"] != "prefix你好":
                            raise RuntimeError(f"{mode} input check failed: {result}")
                        print(f"{mode}: preedit + Chinese commit passed (Qt {result['qt']}, {result['platform']})",
                              flush=True)
                    except Exception:
                        log.seek(0)
                        print(log.read()[-20000:])
                        daemon_log.seek(0)
                        print(daemon_log.read()[-10000:])
                        raise
                    finally:
                        stop(process)
        finally:
            stop(daemon)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--dist", type=Path)
    source.add_argument("--archive", type=Path)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("DISPLAY") or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        parser.error("Run under dbus-run-session -- xvfb-run -a")
    with tempfile.TemporaryDirectory(prefix="muffin-ime-") as temporary:
        work = Path(temporary)
        bundle = work / "relocated"
        if args.archive:
            with tarfile.open(args.archive) as archive:
                archive.extractall(bundle, filter="data")
        else:
            shutil.copytree(args.dist, bundle, symlinks=True)
        shutil.copy2(args.probe, bundle / "ime-probe")
        # No build-directory RPATH is allowed to mask a missing deployment.
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(bundle / "ime-probe")], check=True)
        home = work / "home"
        home.mkdir()
        runtime = work / "runtime"
        runtime.mkdir(mode=0o700)
        env = {"PATH": "/usr/bin:/bin", "HOME": str(home), "LANG": "C.UTF-8",
               "DISPLAY": os.environ["DISPLAY"],
               "DBUS_SESSION_BUS_ADDRESS": os.environ["DBUS_SESSION_BUS_ADDRESS"],
               "XDG_RUNTIME_DIR": str(runtime), "XDG_CONFIG_HOME": str(home / ".config"),
               "XDG_DATA_HOME": str(home / ".local/share"), "QT_QPA_PLATFORM": "xcb"}
        if "XAUTHORITY" in os.environ:
            env["XAUTHORITY"] = os.environ["XAUTHORITY"]
        check_libraries(bundle, env)
        print("Relocated binaries: dependencies and RPATH passed", flush=True)
        # Deliberately inject the incompatible desktop plugin path. Startup
        # must isolate it without overwriting the desktop's IM selection.
        env["QT_PLUGIN_PATH"] = "/usr/lib/x86_64-linux-gnu/qt6/plugins"
        for module in ("fcitx", "ibus"):
            run([str(bundle / "ime-probe"), "--load-only"], dict(env, QT_IM_MODULE=module))
            print(f"{module}: bundled input context load passed", flush=True)
        # A missing/broken bundled plugin must not reopen system search paths.
        plugin = bundle / "platforminputcontexts/libfcitx5platforminputcontextplugin.so"
        disabled = plugin.with_suffix(".disabled")
        plugin.rename(disabled)
        try:
            run([str(bundle / "ime-probe"), "--paths-only"], dict(env, QT_IM_MODULE="fcitx"))
        finally:
            disabled.rename(plugin)
        run([str(bundle / "Muffin"), "--version"], env)
        check_input(bundle, env, work)


if __name__ == "__main__":
    main()
