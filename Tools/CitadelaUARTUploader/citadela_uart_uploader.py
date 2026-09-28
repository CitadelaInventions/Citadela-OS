from __future__ import annotations

import binascii
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable

import serial
from serial.tools import list_ports

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
except ImportError:
    DND_FILES = None
    TkinterDnD = None

import tkinter as tk
from tkinter import filedialog, messagebox, ttk


APP_NAME_PATTERN = re.compile(r"^[A-Za-z0-9_-]{1,40}$")
BASE_BAUD = 115200
TRANSFER_BAUD = 921600
FQBN = (
    "esp32:esp32:esp32:UploadSpeed=921600,CPUFreq=240,FlashFreq=80,"
    "FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,"
    "PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,"
    "JTAGAdapter=default,ZigbeeMode=default"
)


class UploadError(RuntimeError):
    pass


def find_arduino_cli() -> Path:
    configured = os.environ.get("CITADELA_ARDUINO_CLI")
    candidates = [
        Path(configured) if configured else None,
        Path(r"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"),
        Path(r"C:\Program Files (x86)\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"),
    ]
    on_path = shutil.which("arduino-cli")
    if on_path:
        candidates.insert(0, Path(on_path))
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    raise UploadError(
        "Arduino CLI was not found. Install Arduino IDE 2 or set CITADELA_ARDUINO_CLI."
    )


def available_ports() -> list[tuple[str, str]]:
    ports = []
    for port in list_ports.comports():
        label = f"{port.device} - {port.description or 'Serial port'}"
        ports.append((port.device, label))
    return ports


def safe_staging_copy(source: Path, destination: Path) -> None:
    ignored = {"build", ".git", ".vs", ".vscode", "__pycache__"}
    for child in source.iterdir():
        if child.name in ignored:
            continue
        target = destination / child.name
        if child.is_dir():
            shutil.copytree(child, target, ignore=shutil.ignore_patterns(*ignored))
        else:
            shutil.copy2(child, target)


def compile_sketch(
    ino_path: Path,
    working_root: Path,
    log: Callable[[str], None],
) -> Path:
    cli = find_arduino_cli()
    source_dir = ino_path.parent
    source_stem = ino_path.stem
    sketch_dir = source_dir

    if source_dir.name.casefold() != source_stem.casefold():
        sketch_dir = working_root / source_stem
        sketch_dir.mkdir(parents=True, exist_ok=True)
        safe_staging_copy(source_dir, sketch_dir)
        staged_source = sketch_dir / f"{source_stem}.ino"
        if ino_path.resolve() != staged_source.resolve():
            shutil.copy2(ino_path, staged_source)

    output_dir = working_root / "compiled"
    build_dir = working_root / "build"
    output_dir.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    command = [
        str(cli),
        "compile",
        "--fqbn",
        FQBN,
        "--build-path",
        str(build_dir),
        "--output-dir",
        str(output_dir),
        "--warnings",
        "none",
        "--no-color",
        str(sketch_dir),
    ]
    log(f"Compiling {ino_path.name}")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    assert process.stdout is not None
    for line in process.stdout:
        line = line.rstrip()
        if line:
            log(line)
    return_code = process.wait()
    if return_code != 0:
        raise UploadError(f"Compilation failed with exit code {return_code}.")

    expected = output_dir / f"{sketch_dir.name}.ino.bin"
    if expected.is_file():
        return expected
    binaries = sorted(output_dir.glob("*.ino.bin"), key=lambda item: item.stat().st_size, reverse=True)
    if not binaries:
        raise UploadError("Compilation finished but no sketch .bin was produced.")
    return binaries[0]


class ProtocolClient:
    def __init__(
        self,
        port: str,
        log: Callable[[str], None],
        progress: Callable[[float, str], None],
    ) -> None:
        self.port = port
        self.log = log
        self.progress = progress
        self.serial: serial.Serial | None = None

    def __enter__(self) -> "ProtocolClient":
        connection = serial.Serial()
        connection.port = self.port
        connection.baudrate = BASE_BAUD
        connection.timeout = 0.2
        connection.write_timeout = 10
        connection.dtr = False
        connection.rts = False
        connection.open()
        connection.dtr = False
        connection.rts = False
        self.serial = connection
        time.sleep(0.2)
        connection.reset_input_buffer()
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        if self.serial and self.serial.is_open:
            self.serial.close()

    def send_line(self, line: str) -> None:
        assert self.serial is not None
        self.log(f"> {line}")
        self.serial.write((line + "\n").encode("ascii"))
        self.serial.flush()

    def read_line(self, deadline: float) -> str | None:
        assert self.serial is not None
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            if text:
                self.log(f"< {text}")
                return text
        return None

    def wait_for(self, prefix: str, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(deadline)
            if line is None:
                break
            if line.startswith("CITUART ERROR "):
                raise UploadError(line.removeprefix("CITUART ERROR ").replace("_", " ").title())
            if line.startswith(prefix):
                return line
        raise UploadError(f"Timed out waiting for {prefix.strip()}.")

    def verify_armed(self) -> None:
        self.send_line("CITUART PING")
        line = self.wait_for("CITUART ", 3)
        if line == "CITUART ARMED":
            return
        if line == "CITUART BUSY":
            raise UploadError("The ESP is already receiving another package.")
        raise UploadError("Open Configuration > UART Upload on Citadela first.")

    def begin(self, target: str, app_name: str) -> None:
        begin = "CITUART BEGIN KERNEL" if target == "kernel" else f"CITUART BEGIN APP {app_name}"
        self.send_line(begin)
        self.wait_for("CITUART ACCEPTED", 5)
        baud_line = self.wait_for("CITUART BAUD ", 20)
        try:
            baud = int(baud_line.rsplit(" ", 1)[1])
        except (ValueError, IndexError) as exc:
            raise UploadError(f"Invalid baud response: {baud_line}") from exc
        assert self.serial is not None
        self.serial.baudrate = baud
        time.sleep(0.12)
        self.wait_for("CITUART READY ", 8)
        self.log(f"Transfer link ready at {baud:,} baud")

    def send_file(
        self,
        kind: str,
        path: Path,
        progress_start: float,
        progress_end: float,
        label: str,
    ) -> None:
        assert self.serial is not None
        size = path.stat().st_size
        crc = 0
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                crc = binascii.crc32(chunk, crc)
        crc &= 0xFFFFFFFF

        self.send_line(f"CITUART FILE {kind} {size} {crc:08X}")
        ready = self.wait_for(f"CITUART FILE_READY {kind} ", 8)
        try:
            chunk_size = int(ready.rsplit(" ", 1)[1])
        except (ValueError, IndexError) as exc:
            raise UploadError(f"Invalid chunk response: {ready}") from exc
        if chunk_size < 64 or chunk_size > 4096:
            raise UploadError(f"ESP requested an invalid chunk size ({chunk_size}).")

        sent = 0
        with path.open("rb") as source:
            while sent < size:
                chunk = source.read(chunk_size)
                if not chunk:
                    raise UploadError(f"Unexpected end of {path.name}.")
                self.serial.write(chunk)
                sent += len(chunk)
                ack = self.wait_for(f"CITUART ACK {kind} ", 15)
                try:
                    acknowledged = int(ack.rsplit(" ", 1)[1])
                except (ValueError, IndexError) as exc:
                    raise UploadError(f"Invalid acknowledgement: {ack}") from exc
                if acknowledged != sent:
                    raise UploadError(
                        f"ESP acknowledged {acknowledged} bytes after {sent} were sent."
                    )
                fraction = sent / size
                value = progress_start + (progress_end - progress_start) * fraction
                self.progress(value, f"{label}: {sent:,} / {size:,} bytes")

        self.wait_for(f"CITUART FILE_OK {kind} ", 8)

    def commit(self, target: str, app_name: str) -> None:
        self.send_line("CITUART COMMIT")
        self.wait_for("CITUART COMMITTED ", 20)
        done = self.wait_for("CITUART DONE ", 15)
        expected = "KERNEL" if target == "kernel" else "APP"
        if f"CITUART DONE {expected} " not in done:
            raise UploadError(f"Unexpected completion response: {done}")
        self.progress(100, f"{app_name if target == 'app' else 'Kernel'} uploaded")


def upload_package(
    port: str,
    target: str,
    app_name: str,
    binary_path: Path,
    source_path: Path,
    log: Callable[[str], None],
    progress: Callable[[float, str], None],
) -> None:
    with ProtocolClient(port, log, progress) as client:
        client.verify_armed()
        client.begin(target, app_name)
        try:
            client.send_file("BIN", binary_path, 25, 78, "Binary")
            client.send_file("INO", source_path, 78, 96, "Source")
            client.commit(target, app_name)
        except Exception:
            try:
                client.send_line("CITUART CANCEL")
            except Exception:
                pass
            raise


class UploaderWindow:
    def __init__(self, initial_file: str | None = None) -> None:
        root_class = TkinterDnD.Tk if TkinterDnD else tk.Tk
        self.root = root_class()
        self.root.title("Citadela UART Uploader")
        self.root.geometry("760x570")
        self.root.minsize(680, 500)
        self.root.configure(background="#11171b")

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.source_path = tk.StringVar()
        self.target = tk.StringVar(value="app")
        self.app_name = tk.StringVar()
        self.port = tk.StringVar()
        self.status = tk.StringVar(value="Select an Arduino sketch")
        self.progress_value = tk.DoubleVar(value=0)
        self.port_devices: dict[str, str] = {}
        self.busy = False

        self._configure_style()
        self._build_ui()
        self.refresh_ports()
        self.root.after(80, self._drain_events)

        if initial_file:
            self.select_file(Path(initial_file))

    def _configure_style(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Root.TFrame", background="#11171b")
        style.configure("Panel.TFrame", background="#182229")
        style.configure("Title.TLabel", background="#11171b", foreground="#f4f7f8", font=("Segoe UI", 17, "bold"))
        style.configure("Body.TLabel", background="#182229", foreground="#dce6e9", font=("Segoe UI", 10))
        style.configure("Hint.TLabel", background="#182229", foreground="#8ea9b2", font=("Segoe UI", 9))
        style.configure("Status.TLabel", background="#11171b", foreground="#a9c9d1", font=("Segoe UI", 9))
        style.configure("Accent.TButton", font=("Segoe UI", 10, "bold"), padding=(14, 8))
        style.map("Accent.TButton", background=[("active", "#2d9d92"), ("!disabled", "#257d77")], foreground=[("!disabled", "white")])
        style.configure("TButton", font=("Segoe UI", 9), padding=(10, 6))
        style.configure("TRadiobutton", background="#182229", foreground="#e8eff1", font=("Segoe UI", 10))
        style.map("TRadiobutton", background=[("active", "#182229")])
        style.configure("TEntry", fieldbackground="#0f1519", foreground="#f4f7f8")
        style.configure("TCombobox", fieldbackground="#0f1519", foreground="#f4f7f8")
        style.configure("Horizontal.TProgressbar", troughcolor="#26343b", background="#38b6aa")

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, style="Root.TFrame", padding=20)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text="Citadela UART Uploader", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            outer,
            text="Compile an ESP32 sketch and install its binary and source onto the Citadela SD card.",
            style="Status.TLabel",
        ).pack(anchor="w", pady=(2, 14))

        panel = ttk.Frame(outer, style="Panel.TFrame", padding=16)
        panel.pack(fill="x")
        panel.columnconfigure(1, weight=1)

        ttk.Label(panel, text="Arduino sketch", style="Body.TLabel").grid(row=0, column=0, sticky="w", padx=(0, 12))
        source_entry = ttk.Entry(panel, textvariable=self.source_path, state="readonly")
        source_entry.grid(row=0, column=1, sticky="ew")
        ttk.Button(panel, text="Open .ino", command=self.open_file).grid(row=0, column=2, padx=(10, 0))

        self.drop_label = ttk.Label(
            panel,
            text="Drop an .ino file here" if TkinterDnD else "Open an .ino file to begin",
            style="Hint.TLabel",
            anchor="center",
        )
        self.drop_label.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(10, 16), ipady=8)
        if TkinterDnD and DND_FILES:
            self.drop_label.drop_target_register(DND_FILES)
            self.drop_label.dnd_bind("<<Drop>>", self._drop_file)

        ttk.Label(panel, text="Package type", style="Body.TLabel").grid(row=2, column=0, sticky="w", padx=(0, 12))
        target_frame = ttk.Frame(panel, style="Panel.TFrame")
        target_frame.grid(row=2, column=1, sticky="w")
        ttk.Radiobutton(target_frame, text="Application", variable=self.target, value="app", command=self._target_changed).pack(side="left")
        ttk.Radiobutton(target_frame, text="Kernel", variable=self.target, value="kernel", command=self._target_changed).pack(side="left", padx=(18, 0))

        ttk.Label(panel, text="App name", style="Body.TLabel").grid(row=3, column=0, sticky="w", pady=(12, 0), padx=(0, 12))
        self.name_entry = ttk.Entry(panel, textvariable=self.app_name)
        self.name_entry.grid(row=3, column=1, sticky="ew", pady=(12, 0))
        ttk.Label(panel, text="Letters, digits, _ and -", style="Hint.TLabel").grid(row=3, column=2, sticky="w", padx=(10, 0), pady=(12, 0))

        ttk.Label(panel, text="ESP serial port", style="Body.TLabel").grid(row=4, column=0, sticky="w", pady=(12, 0), padx=(0, 12))
        self.port_combo = ttk.Combobox(panel, textvariable=self.port, state="readonly")
        self.port_combo.grid(row=4, column=1, sticky="ew", pady=(12, 0))
        ttk.Button(panel, text="Refresh", command=self.refresh_ports).grid(row=4, column=2, padx=(10, 0), pady=(12, 0))

        action_bar = ttk.Frame(outer, style="Root.TFrame")
        action_bar.pack(fill="x", pady=(14, 8))
        ttk.Label(action_bar, text="Open Configuration > UART Upload on the ESP before starting.", style="Status.TLabel").pack(side="left")
        self.upload_button = ttk.Button(action_bar, text="Compile & Upload", style="Accent.TButton", command=self.start_upload)
        self.upload_button.pack(side="right")

        ttk.Progressbar(outer, variable=self.progress_value, maximum=100).pack(fill="x", pady=(0, 5))
        ttk.Label(outer, textvariable=self.status, style="Status.TLabel").pack(anchor="w", pady=(0, 8))

        log_frame = ttk.Frame(outer, style="Panel.TFrame")
        log_frame.pack(fill="both", expand=True)
        self.log_text = tk.Text(
            log_frame,
            background="#0c1114",
            foreground="#c7d7dc",
            insertbackground="white",
            relief="flat",
            font=("Cascadia Mono", 9),
            wrap="word",
            padx=10,
            pady=8,
            state="disabled",
        )
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def _drop_file(self, event) -> None:
        paths = self.root.tk.splitlist(event.data)
        if paths:
            self.select_file(Path(paths[0]))

    def open_file(self) -> None:
        selected = filedialog.askopenfilename(
            title="Select an Arduino sketch",
            filetypes=[("Arduino sketches", "*.ino"), ("All files", "*.*")],
        )
        if selected:
            self.select_file(Path(selected))

    def select_file(self, path: Path) -> None:
        path = path.expanduser().resolve()
        if not path.is_file() or path.suffix.casefold() != ".ino":
            messagebox.showerror("Invalid sketch", "Select an Arduino .ino file.")
            return
        self.source_path.set(str(path))
        self.app_name.set(path.stem)
        if path.stem.casefold() == "kernel":
            self.target.set("kernel")
        else:
            self.target.set("app")
        self._target_changed()
        self.status.set(f"Ready to compile {path.name}")

    def _target_changed(self) -> None:
        self.name_entry.configure(state="disabled" if self.target.get() == "kernel" else "normal")

    def refresh_ports(self) -> None:
        ports = available_ports()
        self.port_devices = {label: device for device, label in ports}
        labels = list(self.port_devices)
        self.port_combo.configure(values=labels)
        current = self.port.get()
        if current not in self.port_devices:
            self.port.set(labels[0] if labels else "")

    def _selected_port(self) -> str:
        label = self.port.get()
        return self.port_devices.get(label, label.split(" - ", 1)[0] if label else "")

    def _post(self, kind: str, value: object) -> None:
        self.events.put((kind, value))

    def _log(self, text: str) -> None:
        self._post("log", text)

    def _progress(self, value: float, status: str) -> None:
        self._post("progress", (value, status))

    def _drain_events(self) -> None:
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "log":
                    self.log_text.configure(state="normal")
                    self.log_text.insert("end", str(value) + "\n")
                    self.log_text.see("end")
                    self.log_text.configure(state="disabled")
                elif kind == "progress":
                    progress, status = value
                    self.progress_value.set(float(progress))
                    self.status.set(str(status))
                elif kind == "done":
                    self.busy = False
                    self.upload_button.configure(state="normal")
                    self.status.set(str(value))
                    messagebox.showinfo("Upload complete", str(value))
                elif kind == "error":
                    self.busy = False
                    self.upload_button.configure(state="normal")
                    self.status.set(str(value))
                    messagebox.showerror("Upload failed", str(value))
        except queue.Empty:
            pass
        self.root.after(80, self._drain_events)

    def start_upload(self) -> None:
        if self.busy:
            return
        source_text = self.source_path.get().strip()
        if not source_text:
            messagebox.showerror("No sketch", "Select an Arduino .ino file first.")
            return
        source = Path(source_text)
        port = self._selected_port()
        if not port:
            messagebox.showerror("No serial port", "Connect the kernel ESP and refresh the port list.")
            return
        target = self.target.get()
        app_name = self.app_name.get().strip()
        if target == "app" and not APP_NAME_PATTERN.fullmatch(app_name):
            messagebox.showerror("Invalid app name", "Use 1-40 letters, digits, underscores, or hyphens.")
            return

        self.busy = True
        self.upload_button.configure(state="disabled")
        self.progress_value.set(0)
        self.status.set("Starting compiler")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")
        threading.Thread(
            target=self._worker,
            args=(source, port, target, app_name),
            daemon=True,
        ).start()

    def _worker(self, source: Path, port: str, target: str, app_name: str) -> None:
        try:
            self._progress(2, "Compiling sketch")
            with tempfile.TemporaryDirectory(prefix="citadela-uart-") as temp:
                binary = compile_sketch(source, Path(temp), self._log)
                self._log(f"Binary ready: {binary.stat().st_size:,} bytes")
                self._progress(24, "Connecting to kernel ESP")
                upload_package(port, target, app_name, binary, source, self._log, self._progress)
            target_name = app_name if target == "app" else "Kernel"
            self._post("done", f"{target_name} package was installed on the SD card.")
        except Exception as exc:
            self._log(f"ERROR: {exc}")
            self._post("error", str(exc))

    def run(self) -> None:
        self.root.mainloop()


def main() -> int:
    initial = sys.argv[1] if len(sys.argv) > 1 else None
    UploaderWindow(initial).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
