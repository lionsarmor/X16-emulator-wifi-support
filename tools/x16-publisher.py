#!/usr/bin/env python3
"""
X16 Publisher — a desktop GUI for tools/bundle-x16-app.sh and
android/bundle-app-android.sh.

Point it at a built X16 app's folder, pick which platforms to publish to,
and it produces dedicated, ready-to-run artifacts for each one: a Linux
.sh, a Windows .exe, and/or an Android .apk that boot straight into that
one app automatically. No file picker inside the output itself — each
artifact is built for exactly one app.

Run with: python3 tools/x16-publisher.py
No extra packages needed - just Python 3 with its standard Tkinter GUI.
"""
import json
import os
import queue
import re
import subprocess
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

REPO_ROOT = Path(__file__).resolve().parent.parent
LINUX_WINDOWS_BUNDLER = REPO_ROOT / "tools" / "bundle-x16-app.sh"
ANDROID_BUNDLER = REPO_ROOT / "android" / "bundle-app-android.sh"
CONFIG_PATH = Path.home() / ".config" / "x16-publisher.json"

BG = "#1e1e24"
PANEL = "#282830"
FG = "#e8e8ec"
MUTED = "#9a9aa4"
ACCENT = "#5aa9e6"
ACCENT_DARK = "#3d7cb0"
GOOD = "#5cb85c"
BAD = "#d9534f"
ENTRY_BG = "#33333c"


def load_config():
    try:
        return json.loads(CONFIG_PATH.read_text())
    except Exception:
        return {}


def save_config(data):
    try:
        CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_PATH.write_text(json.dumps(data, indent=2))
    except Exception:
        pass


def slugify_upper(name):
    slug = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-").upper()
    return slug or "APP"


def guess_app_name(folder: Path) -> str:
    # dist/sdcard folders are usually named after the actual project two
    # levels up (e.g. .../DESK-COMMANDER/dist/sdcard) - prefer that if it
    # looks generic, otherwise just use the folder's own name.
    name = folder.name
    if name.lower() in ("sdcard", "dist", "app", "build", "release"):
        for parent in folder.parents:
            if parent.name and parent.name.lower() not in ("dist", "sdcard"):
                name = parent.name
                break
    return name.replace("-", " ").replace("_", " ").strip().upper()


class X16Publisher(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("X16 Publisher")
        self.geometry("760x640")
        self.minsize(680, 560)
        self.configure(bg=BG)

        self.log_queue = queue.Queue()
        self.build_thread = None
        self.last_linux_dir = None
        self.last_android_apk = None

        self._configure_style()
        self._build_ui()
        self._load_saved_state()
        self.after(80, self._drain_log_queue)

    # ------------------------------------------------------------------ UI

    def _configure_style(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=BG, foreground=FG, font=("Sans", 10))
        style.configure("Muted.TLabel", background=BG, foreground=MUTED, font=("Sans", 9))
        style.configure("Header.TLabel", background=BG, foreground=FG, font=("Sans", 13, "bold"))
        style.configure("TCheckbutton", background=BG, foreground=FG, font=("Sans", 10))
        style.map("TCheckbutton", background=[("active", BG)])
        style.configure("TEntry", fieldbackground=ENTRY_BG, foreground=FG, insertcolor=FG, borderwidth=0)
        style.configure("TButton", font=("Sans", 10), padding=6)
        style.configure("Accent.TButton", background=ACCENT, foreground="#0b0b0d")
        style.map("Accent.TButton", background=[("active", ACCENT_DARK)])
        style.configure("TCombobox", fieldbackground=ENTRY_BG, foreground=FG)

    def _build_ui(self):
        pad = {"padx": 16, "pady": 6}

        header = ttk.Label(self, text="X16 Publisher", style="Header.TLabel")
        header.pack(anchor="w", padx=16, pady=(16, 0))
        ttk.Label(
            self,
            text="Turn one X16 app into dedicated, ready-to-run installs for each platform.",
            style="Muted.TLabel",
        ).pack(anchor="w", padx=16, pady=(0, 10))

        form = ttk.Frame(self)
        form.pack(fill="x", **pad)
        form.columnconfigure(1, weight=1)

        # --- App folder ---
        ttk.Label(form, text="App folder (dist/sdcard):").grid(row=0, column=0, sticky="w", pady=4)
        self.folder_var = tk.StringVar()
        folder_entry = ttk.Entry(form, textvariable=self.folder_var)
        folder_entry.grid(row=0, column=1, sticky="ew", padx=(8, 8))
        ttk.Button(form, text="Browse…", command=self._browse_folder).grid(row=0, column=2)

        # --- App name ---
        ttk.Label(form, text="App name:").grid(row=1, column=0, sticky="w", pady=4)
        self.name_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.name_var).grid(row=1, column=1, sticky="ew", padx=(8, 8), columnspan=2)

        # --- PRG ---
        ttk.Label(form, text="Boot program (.PRG):").grid(row=2, column=0, sticky="w", pady=4)
        self.prg_var = tk.StringVar()
        self.prg_combo = ttk.Combobox(form, textvariable=self.prg_var, state="readonly")
        self.prg_combo.grid(row=2, column=1, sticky="ew", padx=(8, 8), columnspan=2)

        # --- Output dir ---
        ttk.Label(form, text="Output folder:").grid(row=3, column=0, sticky="w", pady=4)
        self.out_var = tk.StringVar(value=str(Path.home() / "x16-bundles"))
        ttk.Entry(form, textvariable=self.out_var).grid(row=3, column=1, sticky="ew", padx=(8, 8))
        ttk.Button(form, text="Browse…", command=self._browse_out).grid(row=3, column=2)

        # --- Platforms ---
        plat_frame = ttk.Frame(self)
        plat_frame.pack(fill="x", padx=16, pady=(10, 0))
        ttk.Label(plat_frame, text="Publish to:").pack(side="left", padx=(0, 12))
        self.linux_var = tk.BooleanVar(value=True)
        self.windows_var = tk.BooleanVar(value=True)
        self.android_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(plat_frame, text="Linux (.sh)", variable=self.linux_var).pack(side="left", padx=8)
        ttk.Checkbutton(plat_frame, text="Windows (.exe)", variable=self.windows_var).pack(side="left", padx=8)
        ttk.Checkbutton(plat_frame, text="Android (.apk)", variable=self.android_var).pack(side="left", padx=8)

        # --- Android app id (advanced, only matters for Android) ---
        adv_frame = ttk.Frame(self)
        adv_frame.pack(fill="x", padx=16, pady=(6, 0))
        ttk.Label(adv_frame, text="Android app ID (optional):", style="Muted.TLabel").pack(side="left")
        self.app_id_var = tk.StringVar()
        ttk.Entry(adv_frame, textvariable=self.app_id_var, width=36).pack(side="left", padx=8)
        ttk.Label(adv_frame, text="blank = derived from the app name", style="Muted.TLabel").pack(side="left")

        # --- Publish button + status ---
        action_frame = ttk.Frame(self)
        action_frame.pack(fill="x", padx=16, pady=(14, 6))
        self.publish_btn = ttk.Button(action_frame, text="Publish", style="Accent.TButton", command=self._on_publish)
        self.publish_btn.pack(side="left")
        self.progress = ttk.Progressbar(action_frame, mode="indeterminate", length=160)
        self.progress.pack(side="left", padx=12)
        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(action_frame, textvariable=self.status_var, style="Muted.TLabel").pack(side="left", padx=4)

        # --- Post-build actions ---
        self.post_frame = ttk.Frame(self)
        self.post_frame.pack(fill="x", padx=16)
        self.test_linux_btn = ttk.Button(self.post_frame, text="▶ Test on this machine", command=self._test_linux, state="disabled")
        self.test_linux_btn.pack(side="left", padx=(0, 8))
        self.install_android_btn = ttk.Button(self.post_frame, text="📱 Install to connected device", command=self._install_android, state="disabled")
        self.install_android_btn.pack(side="left")

        # --- Log ---
        log_frame = ttk.Frame(self)
        log_frame.pack(fill="both", expand=True, padx=16, pady=(10, 16))
        ttk.Label(log_frame, text="Output:", style="Muted.TLabel").pack(anchor="w")
        self.log_text = tk.Text(
            log_frame, bg="#111116", fg="#c8c8d0", insertbackground=FG,
            font=("Monospace", 9), wrap="word", state="disabled", borderwidth=0,
        )
        self.log_text.pack(fill="both", expand=True, pady=(4, 0))
        scrollbar = ttk.Scrollbar(self.log_text, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")

        self.folder_var.trace_add("write", lambda *_: self._on_folder_changed())

    # ----------------------------------------------------------- behaviour

    def _load_saved_state(self):
        cfg = load_config()
        if cfg.get("out_dir"):
            self.out_var.set(cfg["out_dir"])
        if cfg.get("last_folder"):
            self.folder_var.set(cfg["last_folder"])
        for key, var in (("linux", self.linux_var), ("windows", self.windows_var), ("android", self.android_var)):
            if key in cfg:
                var.set(cfg[key])

    def _save_state(self):
        save_config({
            "out_dir": self.out_var.get(),
            "last_folder": self.folder_var.get(),
            "linux": self.linux_var.get(),
            "windows": self.windows_var.get(),
            "android": self.android_var.get(),
        })

    def _browse_folder(self):
        path = filedialog.askdirectory(title="Select the app's dist/sdcard folder")
        if path:
            self.folder_var.set(path)

    def _browse_out(self):
        path = filedialog.askdirectory(title="Select output folder")
        if path:
            self.out_var.set(path)

    def _on_folder_changed(self):
        folder = Path(self.folder_var.get()) if self.folder_var.get() else None
        prgs = []
        has_autoboot = False
        if folder and folder.is_dir():
            prgs = sorted(p.name for p in folder.glob("*.PRG"))
            has_autoboot = (folder / "AUTOBOOT.X16").exists()
            if not self.name_var.get():
                self.name_var.set(guess_app_name(folder))

        options = list(prgs)
        if has_autoboot:
            options = ["(auto — AUTOBOOT.X16)"] + options
        self.prg_combo["values"] = options
        if options:
            self.prg_combo.current(0)
        else:
            self.prg_var.set("")

    def _selected_prg(self):
        val = self.prg_var.get()
        if not val or val.startswith("(auto"):
            return ""
        return val

    def _log(self, line):
        self.log_queue.put(line)

    def _drain_log_queue(self):
        try:
            while True:
                line = self.log_queue.get_nowait()
                self.log_text.configure(state="normal")
                self.log_text.insert("end", line + "\n")
                self.log_text.see("end")
                self.log_text.configure(state="disabled")
        except queue.Empty:
            pass
        self.after(80, self._drain_log_queue)

    def _validate(self):
        folder = self.folder_var.get().strip()
        if not folder or not Path(folder).is_dir():
            messagebox.showerror("X16 Publisher", "Pick a valid app folder first.")
            return None
        name = self.name_var.get().strip()
        if not name:
            messagebox.showerror("X16 Publisher", "Give the app a name.")
            return None
        prg = self._selected_prg()
        if not prg and not (Path(folder) / "AUTOBOOT.X16").exists():
            messagebox.showerror("X16 Publisher", "This app has no AUTOBOOT.X16, so pick a .PRG to boot into.")
            return None
        if not (self.linux_var.get() or self.windows_var.get() or self.android_var.get()):
            messagebox.showerror("X16 Publisher", "Pick at least one platform.")
            return None
        out = self.out_var.get().strip() or str(Path.home() / "x16-bundles")
        return {"folder": folder, "name": name, "prg": prg, "out": out}

    def _on_publish(self):
        if self.build_thread and self.build_thread.is_alive():
            return
        params = self._validate()
        if not params:
            return
        self._save_state()
        self.publish_btn.configure(state="disabled")
        self.test_linux_btn.configure(state="disabled")
        self.install_android_btn.configure(state="disabled")
        self.progress.start(12)
        self.status_var.set("Publishing…")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        self.build_thread = threading.Thread(target=self._run_build, args=(params,), daemon=True)
        self.build_thread.start()
        self.after(200, self._poll_build_thread)

    def _poll_build_thread(self):
        if self.build_thread and self.build_thread.is_alive():
            self.after(200, self._poll_build_thread)
            return
        self.progress.stop()
        self.publish_btn.configure(state="normal")

    def _run_subprocess(self, cmd, cwd=None, env=None):
        self._log(f"$ {' '.join(str(c) for c in cmd)}")
        proc = subprocess.Popen(
            cmd, cwd=cwd, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1,
        )
        for line in proc.stdout:
            self._log(line.rstrip("\n"))
        proc.wait()
        return proc.returncode

    def _run_build(self, params):
        ok_count = 0
        fail_count = 0

        platforms = []
        if self.linux_var.get():
            platforms.append("linux")
        if self.windows_var.get():
            platforms.append("windows")
        if platforms:
            self._log(f"\n=== {' + '.join(p.capitalize() for p in platforms)} ===")
            cmd = [
                str(LINUX_WINDOWS_BUNDLER), "--name", params["name"], "--sdcard", params["folder"],
                "--out", params["out"], "--platforms", ",".join(platforms),
            ]
            if params["prg"]:
                cmd += ["--prg", params["prg"]]
            rc = self._run_subprocess(cmd)
            if rc == 0:
                ok_count += 1
                self.last_linux_dir = Path(params["out"]) / slugify_upper(params["name"]) / "linux"
                self.after(0, lambda: self.test_linux_btn.configure(state="normal" if self.linux_var.get() else "disabled"))
            else:
                fail_count += 1
                self._log(f"[FAILED] Linux/Windows bundle exited with code {rc}")

        if self.android_var.get():
            self._log("\n=== Android ===")
            cmd = [str(ANDROID_BUNDLER), "--name", params["name"], "--sdcard", params["folder"], "--out", params["out"]]
            if params["prg"]:
                cmd += ["--prg", params["prg"]]
            if self.app_id_var.get().strip():
                cmd += ["--app-id", self.app_id_var.get().strip()]
            rc = self._run_subprocess(cmd)
            if rc == 0:
                ok_count += 1
                self.last_android_apk = Path(params["out"]) / f"{slugify_upper(params['name'])}.apk"
                self.after(0, self._check_adb_device)
            else:
                fail_count += 1
                self._log(f"[FAILED] Android bundle exited with code {rc}")

        summary = f"Done: {ok_count} succeeded, {fail_count} failed."
        self._log(f"\n{summary}")
        self.after(0, lambda: self.status_var.set(summary))

    def _check_adb_device(self):
        adb = Path.home() / "android-tools" / "sdk" / "platform-tools" / "adb"
        if not adb.exists():
            return
        try:
            out = subprocess.run([str(adb), "devices"], capture_output=True, text=True, timeout=5).stdout
            lines = [l for l in out.splitlines()[1:] if l.strip() and "device" in l]
            if lines:
                self.install_android_btn.configure(state="normal")
        except Exception:
            pass

    def _test_linux(self):
        if not self.last_linux_dir:
            return
        sh_files = list(self.last_linux_dir.glob("*.sh"))
        if not sh_files:
            messagebox.showerror("X16 Publisher", f"No launcher script found in {self.last_linux_dir}")
            return
        self._log(f"\nLaunching {sh_files[0]} …")
        subprocess.Popen([str(sh_files[0])], cwd=str(self.last_linux_dir))

    def _install_android(self):
        if not self.last_android_apk or not self.last_android_apk.exists():
            messagebox.showerror("X16 Publisher", "No Android build to install yet.")
            return
        adb = Path.home() / "android-tools" / "sdk" / "platform-tools" / "adb"
        self._log(f"\n$ adb install -r {self.last_android_apk}")

        def run():
            rc = self._run_subprocess([str(adb), "install", "-r", str(self.last_android_apk)])
            self.after(0, lambda: self.status_var.set("Installed to device." if rc == 0 else "Install failed — see log."))

        threading.Thread(target=run, daemon=True).start()


if __name__ == "__main__":
    app = X16Publisher()
    app.mainloop()
