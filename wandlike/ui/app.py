"""Tk front-end: dark, Wand-style list of cheats for the selected game."""
from __future__ import annotations

import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from ..core import updater
from ..core.engine import Engine, fmt_value
from ..version import UPDATE_REPO, __version__
from ..core.plugin import Cheat, Game, load_all
from ..core.settings import Settings

BG, PANEL, ROW, FG, DIM = "#0f1013", "#16181d", "#1b1e24", "#e8eaee", "#7b8190"
ACCENT, ACCENT_DK, OK, WARN = "#1f7a8c", "#0f3d47", "#3ecf8e", "#e8b14a"


class Tooltip:
    def __init__(self, widget: tk.Widget, text: str):
        self.w, self.text, self.tip = widget, text, None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _=None):
        x, y = self.w.winfo_rootx() + 18, self.w.winfo_rooty() + 22
        self.tip = tk.Toplevel(self.w)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry(f"+{x}+{y}")
        tk.Label(self.tip, text=self.text, bg="#2a2e36", fg=FG, wraplength=320, justify="left",
                 padx=8, pady=6, font=("Segoe UI", 9)).pack()

    def hide(self, _=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


class App(tk.Tk):
    def __init__(self, dry_run: bool = False):
        super().__init__()
        self.title("WandLike")
        self.geometry("760x820")
        self.configure(bg=BG)
        self.attributes("-topmost", True)
        self.settings = Settings()
        self.games = load_all()
        self.dry_run = dry_run
        self.engine: Engine | None = None
        self.rows: dict[str, dict] = {}
        self._build_chrome()
        ico = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2])) / "assets" / "app.ico"
        try:
            self.iconbitmap(str(ico))
        except tk.TclError:
            pass
        self.select_game(self.settings.get("game", next(iter(self.games))))
        self.after(1500, self._poll_status)
        self.after(2500, self.check_updates)

    # ------------------------------------------------------------ chrome
    def _build_chrome(self):
        top = tk.Frame(self, bg=PANEL)
        top.pack(fill="x")
        self.game_var = tk.StringVar()
        names = [g.name for g in self.games.values()]
        self.combo = ttk.Combobox(top, textvariable=self.game_var, values=names, state="readonly", width=28)
        self.combo.pack(side="left", padx=12, pady=10)
        self.combo.bind("<<ComboboxSelected>>", lambda _: self.select_game(self._id_from_name(self.game_var.get())))
        self.status = tk.Label(top, text="●  game not running", bg=PANEL, fg=DIM, font=("Segoe UI", 10))
        self.status.pack(side="left", padx=8)
        self.debug_lbl = tk.Label(top, text="", bg=PANEL, fg=DIM, font=("Segoe UI", 9))
        self.debug_lbl.pack(side="right", padx=12)
        if self.dry_run:
            tk.Label(top, text="DRY RUN", bg=WARN, fg="#000", font=("Segoe UI", 9, "bold"), padx=6).pack(side="right")

        self.ver_btn = tk.Label(top, text=f"v{__version__}  ⟳", bg=PANEL, fg=DIM, cursor="hand2", font=("Segoe UI", 9))
        self.ver_btn.pack(side="right", padx=8)
        self.ver_btn.bind("<Button-1>", lambda _e: self.check_updates(manual=True))
        Tooltip(self.ver_btn, "Click to check for updates")
        self.banner = tk.Frame(self, bg=ACCENT_DK)
        self.banner_lbl = tk.Label(self.banner, text="", bg=ACCENT_DK, fg=FG, font=("Segoe UI", 10))
        self.banner_lbl.pack(side="left", padx=12, pady=6)
        self.banner_btn = tk.Button(self.banner, text="Update & restart", bg=ACCENT, fg="white", relief="flat",
                                    padx=12, command=self._install_update)
        self.banner_btn.pack(side="right", padx=12, pady=4)
        self.update_info = None

        self.search = tk.StringVar()
        self.search.trace_add("write", lambda *_: self._filter())
        tk.Entry(self, textvariable=self.search, bg=ROW, fg=FG, insertbackground=FG, relief="flat",
                 font=("Segoe UI", 10)).pack(fill="x", padx=12, pady=(8, 4), ipady=5)

        body = tk.Frame(self, bg=BG)
        body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(body, bg=BG, highlightthickness=0)
        sb = ttk.Scrollbar(body, orient="vertical", command=self.canvas.yview)
        self.inner = tk.Frame(self.canvas, bg=BG)
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self._win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfig(self._win, width=e.width))
        self.canvas.configure(yscrollcommand=sb.set)
        self.canvas.pack(side="left", fill="both", expand=True, padx=(12, 0))
        sb.pack(side="right", fill="y")
        self.canvas.bind_all("<MouseWheel>", lambda e: self.canvas.yview_scroll(-1 * (e.delta // 120), "units"))

        self.log_box = tk.Text(self, height=6, bg=PANEL, fg=DIM, relief="flat", font=("Consolas", 9), state="disabled")
        self.log_box.pack(fill="x", padx=12, pady=8)

    def _id_from_name(self, name: str) -> str:
        return next(g.id for g in self.games.values() if g.name == name)

    def log(self, msg: str):
        def _do():
            self.log_box.configure(state="normal")
            self.log_box.insert("end", msg + "\n")
            self.log_box.see("end")
            self.log_box.configure(state="disabled")
        self.after(0, _do)

    # ------------------------------------------------------------- games
    def select_game(self, gid: str):
        if self.engine:
            self.engine.shutdown()
        game = self.games[gid]
        self.settings.set("game", gid)
        self.game_var.set(game.name)
        self.engine = Engine(game, self.log, self.dry_run)
        self.pins: set[str] = set(self.settings.get(f"pins.{gid}", []))
        for w in self.inner.winfo_children():
            w.destroy()
        self.rows.clear()
        self.game = game
        self.log(game.notes)
        self._render()

    def _render(self):
        for w in self.inner.winfo_children():
            w.destroy()
        self.rows.clear()
        section = None
        for c in self.game.cheats:
            if c.section != section:
                section = c.section
                tk.Label(self.inner, text="  " + section, bg=BG, fg=FG, anchor="w",
                         font=("Segoe UI", 11, "bold")).pack(fill="x", pady=(8, 4))
            self._row(c)
        self._filter()

    def _row(self, c: Cheat):
        row = tk.Frame(self.inner, bg=ROW if c.available else BG)
        row.pack(fill="x", pady=1)
        fg = FG if c.available else DIM
        tk.Label(row, text="⚡" if c.kind != "button" else "▶", bg=row["bg"], fg=DIM, width=3).pack(side="left")
        tk.Label(row, text=c.label, bg=row["bg"], fg=fg, font=("Segoe UI", 10, "bold")).pack(side="left", pady=9)
        tip = c.info + (f"\n\nRequires: {c.requires}" if c.requires else "")
        tip += f"\n\n[{c.verified}]" + (f" {c.unsupported}" if c.unsupported else "")
        i = tk.Label(row, text="ⓘ", bg=row["bg"], fg=DIM, padx=6)
        i.pack(side="left")
        Tooltip(i, tip)

        pin = tk.Label(row, text="📌" if c.id in self.pins else "·", bg=row["bg"], fg=ACCENT if c.id in self.pins else DIM,
                       cursor="hand2", padx=8)
        pin.pack(side="right")
        pin.bind("<Button-1>", lambda _e, cid=c.id: self._toggle_pin(cid))
        ctl = tk.Frame(row, bg=row["bg"])
        ctl.pack(side="right", padx=4)
        state = tk.NORMAL if c.available else tk.DISABLED
        if c.kind == "number":
            self._number_controls(ctl, c, row["bg"])
        elif c.kind == "toggle":
            self._toggle_controls(ctl, c, row["bg"], state)
        else:
            tk.Button(ctl, text="Run", bg=ACCENT, fg="white", relief="flat", padx=14,
                      command=lambda: self.engine.press(c)).pack()
        self.rows[c.id] = {"frame": row, "cheat": c}

    def _number_controls(self, parent, c: Cheat, bg: str):
        saved = self.settings.get(f"val.{self.game.id}.{c.id}", c.default)
        var = tk.StringVar(value=fmt_value(saved))
        mode = {"v": "set"}
        mode_btn = None
        if c.add and c.set:
            mode_btn = tk.Button(parent, text="=", width=2, bg=ROW, fg=FG, relief="flat")

            def flip():
                mode["v"] = "add" if mode["v"] == "set" else "set"
                mode_btn.configure(text="+" if mode["v"] == "add" else "=")
            mode_btn.configure(command=flip)
            mode_btn.pack(side="left", padx=(0, 6))

        def bump(sign):
            try:
                v = float(var.get())
            except ValueError:
                v = c.default
            v = max(c.min, min(c.max, v + sign * c.step))
            var.set(fmt_value(v))

        tk.Button(parent, text="−", width=2, bg=ROW, fg=FG, relief="flat", command=lambda: bump(-1)).pack(side="left")
        tk.Entry(parent, textvariable=var, width=11, justify="center", bg=PANEL, fg=FG, insertbackground=FG,
                 relief="flat").pack(side="left", padx=2, ipady=3)
        tk.Button(parent, text="+", width=2, bg=ROW, fg=FG, relief="flat", command=lambda: bump(1)).pack(side="left")

        def apply():
            try:
                v = max(c.min, min(c.max, float(var.get())))
            except ValueError:
                self.log(f"! {c.label}: not a number")
                return
            var.set(fmt_value(v))
            self.settings.set(f"val.{self.game.id}.{c.id}", v)
            self.engine.apply_number(c, v, mode["v"])
        tk.Button(parent, text="✓", width=3, bg=ACCENT_DK, fg="white", relief="flat", command=apply).pack(side="left", padx=(6, 0))

    def _toggle_controls(self, parent, c: Cheat, bg: str, state):
        on = tk.BooleanVar(value=False)
        off_b = tk.Button(parent, text="Off", width=5, relief="flat", state=state)
        on_b = tk.Button(parent, text="On", width=5, relief="flat", state=state)
        off_b.pack(side="left")
        on_b.pack(side="left")

        def paint():
            off_b.configure(bg=ROW if on.get() else ACCENT_DK, fg=DIM if on.get() else FG)
            on_b.configure(bg=ACCENT if on.get() else ROW, fg="white" if on.get() else DIM)

        def set_to(v):
            if on.get() == v:
                return
            on.set(v)
            paint()
            self.engine.set_toggle(c, v)
        off_b.configure(command=lambda: set_to(False))
        on_b.configure(command=lambda: set_to(True))
        paint()

    def _toggle_pin(self, cid: str):
        self.pins ^= {cid}
        self.settings.set(f"pins.{self.game.id}", sorted(self.pins))
        self._render()

    def _filter(self):
        q = self.search.get().lower().strip()
        pinned = [r for cid, r in self.rows.items() if cid in self.pins]
        for cid, r in self.rows.items():
            show = (not q) or q in r["cheat"].label.lower()
            r["frame"].pack_forget() if not show else r["frame"].pack(fill="x", pady=1)
        # pinned float to the top within the list
        for r in reversed(pinned):
            if r["frame"].winfo_manager():
                r["frame"].pack_forget()
                first = next((w for w in self.inner.pack_slaves()), None)
                r["frame"].pack(fill="x", pady=1, before=first) if first else r["frame"].pack(fill="x", pady=1)

    # ----------------------------------------------------------- updates
    def check_updates(self, manual: bool = False):
        if not UPDATE_REPO:
            if manual:
                self.log("Auto-update is not configured (set UPDATE_REPO in wandlike/version.py).")
            return

        def work():
            try:
                info = updater.check()
            except Exception as e:
                if manual:
                    self.log(f"! update check failed: {e}")
                return
            if info:
                self.after(0, lambda: self._show_update(info))
            elif manual:
                self.log(f"You're up to date (v{__version__}).")
        threading.Thread(target=work, daemon=True).start()

    def _show_update(self, info: dict):
        self.update_info = info
        self.banner_lbl.configure(text=f"Update available: v{info['version']}")
        self.banner.pack(fill="x", after=self.combo.master)

    def _install_update(self):
        info = self.update_info
        self.banner_btn.configure(state="disabled", text="Downloading…")

        def work():
            try:
                path = updater.download(
                    info, lambda f: self.after(0, lambda: self.banner_btn.configure(text=f"Downloading {int(f * 100)}%")))
                updater.apply(path)
            except Exception as e:
                self.log(f"! update failed: {e}")
                self.after(0, lambda: self.banner_btn.configure(state="normal", text="Update & restart"))
                return
            self.after(0, self.destroy)
        threading.Thread(target=work, daemon=True).start()

    # ------------------------------------------------------------ status
    def _poll_status(self):
        running = self.engine.running()
        self.status.configure(text="●  game running" if running else "●  game not running",
                              fg=OK if running else DIM)
        self.after(3000, self._poll_status)
