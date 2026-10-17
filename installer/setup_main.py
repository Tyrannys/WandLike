"""WandLike setup / launcher / uninstaller (one stdlib-only exe, Squirrel-style layout).

    %LOCALAPPDATA%\\WandLike\\
        WandLike.exe        <- this binary; when run from here it launches the newest app-X
        app-1.0.0\\WandLike.exe   <- the real application (one folder per version)
        app.ico

Run from anywhere else it installs. Flags: --silent, --update, --wait-pid N, --no-launch,
--uninstall (installed copy only).
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

APP = "WandLike"
REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\WandLike"
DETACHED = 0x00000008 | 0x00000200
NO_WINDOW = 0x08000000


def install_dir() -> Path:
    override = os.environ.get("WANDLIKE_INSTALL_DIR")
    return Path(override) if override else Path(os.environ["LOCALAPPDATA"]) / APP


def skip_shell() -> bool:
    return bool(os.environ.get("WANDLIKE_NO_SHELL"))


def payload_dir() -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent / "build"))
    return base / "payload"


def parse_version(v: str) -> tuple[int, ...]:
    return tuple(int("".join(c for c in p if c.isdigit()) or 0) for p in v.lstrip("vV").split("."))


def app_dirs(root: Path) -> list[Path]:
    dirs = [d for d in root.glob("app-*") if (d / f"{APP}.exe").exists()]
    return sorted(dirs, key=lambda d: parse_version(d.name[4:]))


def wait_for_pid(pid: int, timeout: float = 40) -> None:
    import ctypes
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if h:
        k.WaitForSingleObject(h, int(timeout * 1000))
        k.CloseHandle(h)


def ps(script: str) -> None:
    subprocess.run(["powershell", "-NoProfile", "-WindowStyle", "Hidden", "-Command", script],
                   creationflags=NO_WINDOW, check=False, timeout=30)


def make_shortcut(lnk: Path, target: Path, icon: Path) -> None:
    lnk.parent.mkdir(parents=True, exist_ok=True)
    q = lambda p: str(p).replace("'", "''")
    ps(f"$s=(New-Object -ComObject WScript.Shell).CreateShortcut('{q(lnk)}');"
       f"$s.TargetPath='{q(target)}';$s.WorkingDirectory='{q(target.parent)}';"
       f"$s.IconLocation='{q(icon)}';$s.Description='{APP}';$s.Save()")


def start_menu_lnk() -> Path:
    return Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs" / f"{APP}.lnk"


def desktop_lnk() -> Path:
    return Path(os.path.expandvars(r"%USERPROFILE%\Desktop")) / f"{APP}.lnk"


def register_uninstall(root: Path, version: str) -> None:
    import winreg
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY) as k:
        exe = root / f"{APP}.exe"
        for name, val in {
            "DisplayName": APP, "DisplayVersion": version, "Publisher": APP,
            "InstallLocation": str(root), "DisplayIcon": str(root / "app.ico"),
            "UninstallString": f'"{exe}" --uninstall',
        }.items():
            winreg.SetValueEx(k, name, 0, winreg.REG_SZ, val)
        winreg.SetValueEx(k, "NoModify", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(k, "NoRepair", 0, winreg.REG_DWORD, 1)


def install(progress=lambda pct, msg: None, update: bool = False) -> tuple[Path, str]:
    root = install_dir()
    payload = payload_dir()
    version = (payload / "version.txt").read_text().strip()
    progress(5, "Preparing…")
    root.mkdir(parents=True, exist_ok=True)

    target = root / f"app-{version}"
    tmp = root / f".installing-{version}"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    progress(15, f"Installing {APP} {version}…")
    shutil.copy2(payload / f"{APP}.exe", tmp / f"{APP}.exe")
    progress(60, "Finishing…")
    shutil.rmtree(target, ignore_errors=True)
    tmp.rename(target)

    shutil.copy2(payload / "app.ico", root / "app.ico")
    launcher, staged = root / f"{APP}.exe", root / f"{APP}.exe.new"
    shutil.copy2(sys.executable, staged)
    try:
        os.replace(staged, launcher)
    except OSError:
        pass  # launcher in use; it is identical across versions in practice, retried next update

    if not skip_shell():
        progress(75, "Creating shortcuts…")
        make_shortcut(start_menu_lnk(), launcher, root / "app.ico")
        if not update and not desktop_lnk().exists():
            make_shortcut(desktop_lnk(), launcher, root / "app.ico")
        register_uninstall(root, version)

    # keep this and the previous version, drop the rest
    for old in app_dirs(root)[:-2]:
        shutil.rmtree(old, ignore_errors=True)
    progress(100, "Done")
    return root, version


def launch_newest(root: Path) -> int:
    dirs = app_dirs(root)
    if not dirs:
        return 1
    exe = dirs[-1] / f"{APP}.exe"
    subprocess.Popen([str(exe)], cwd=str(exe.parent), creationflags=DETACHED, close_fds=True)
    return 0


def uninstall(silent: bool = False) -> int:
    root = install_dir()
    try:
        if silent:
            raise RuntimeError  # skip the confirmation dialog
        import tkinter.messagebox as mb
        import tkinter as tk
        r = tk.Tk()
        r.withdraw()
        ok = mb.askyesno(APP, f"Remove {APP} from this computer?\n\nYour settings in %APPDATA%\\{APP} are kept.")
        r.destroy()
        if not ok:
            return 0
    except Exception:
        pass
    if not skip_shell():
        import winreg
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG_KEY)
        except OSError:
            pass
        for l in (start_menu_lnk(), desktop_lnk()):
            l.unlink(missing_ok=True)
    # this exe lives in root, so delete the folder from a detached helper after we exit
    import tempfile
    bat = Path(tempfile.gettempdir()) / "wandlike-uninstall.cmd"
    bat.write_text(f'@echo off\r\nping -n 4 127.0.0.1 >nul\r\nrmdir /s /q "{root}"\r\ndel "%~f0"\r\n')
    subprocess.Popen(["cmd", "/c", str(bat)], creationflags=DETACHED | NO_WINDOW, close_fds=True)
    return 0


def run_gui_install(args) -> int:
    import threading
    import tkinter as tk
    from tkinter import ttk
    win = tk.Tk()
    win.title(f"{APP} Setup")
    win.geometry("360x120")
    win.resizable(False, False)
    win.configure(bg="#0f1013")
    msg = tk.Label(win, text="Starting…", bg="#0f1013", fg="#e8eaee", font=("Segoe UI", 10))
    msg.pack(pady=(24, 8))
    bar = ttk.Progressbar(win, length=300, maximum=100)
    bar.pack()
    state = {"err": None}

    def work():
        try:
            install(lambda p, m: win.after(0, lambda: (bar.configure(value=p), msg.configure(text=m))), args.update)
        except Exception as e:  # shown to the user, then close
            state["err"] = e
        win.after(0, win.destroy)
    threading.Thread(target=work, daemon=True).start()
    win.mainloop()
    if state["err"]:
        import tkinter.messagebox as mb
        mb.showerror(f"{APP} Setup", f"Installation failed:\n{state['err']}")
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--silent", action="store_true")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--wait-pid", type=int)
    args, _ = ap.parse_known_args()

    root = install_dir()
    running_from_root = Path(sys.executable).resolve().parent == root.resolve()

    if running_from_root:                       # installed launcher mode
        if args.uninstall:
            return uninstall(args.silent)
        return launch_newest(root)

    if args.wait_pid:
        wait_for_pid(args.wait_pid)
    if args.silent:
        install(update=args.update)
        rc = 0
    else:
        rc = run_gui_install(args)
    if rc == 0 and not args.no_launch:
        launch_newest(root)
    return rc


if __name__ == "__main__":
    sys.exit(main())
