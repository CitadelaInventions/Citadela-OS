"""Citadela desktop display. Connect to the SerialController USB port."""

import argparse
from collections import deque
from pathlib import Path
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk
import serial
from serial.tools import list_ports

from protocol import Display, ProtocolError
from recovery import ScreenSync


def serial_port_names(port_info, platform=None):
    """Return stable, user-facing device names, preferring macOS callout ports."""
    platform = platform or sys.platform
    names = sorted({port.device for port in port_info if port.device})
    if platform == "darwin":
        available = set(names)
        names = [name for name in names
                 if not (name.startswith("/dev/tty.") and
                         "/dev/cu." + name.removeprefix("/dev/tty.") in available)]
        usb_markers = ("usbmodem", "usbserial", "wchusbserial", "slab_usb")
        names.sort(key=lambda name: (not any(marker in name.lower() for marker in usb_markers),
                                     name.casefold()))
    return names


class Connection:
    def __init__(self, port, log_path=None, *, resync_interval=30, stall_timeout=12):
        self.port = port
        self.log_path = log_path
        self.display = Display(Path(__file__).parent / "AssetCache")
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.outgoing = queue.Queue(maxsize=64)
        self.events = queue.Queue(maxsize=256)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.ready = False
        self.connected = False
        self.bytes = 0
        self.errors = 0
        self.motion = None
        self.sync = ScreenSync(time.monotonic(), resync_interval, stall_timeout)

    def event(self, kind, text):
        try:
            self.events.put_nowait((kind, text))
        except queue.Full:
            pass

    def send(self, command):
        if '\n' in command or '\r' in command or len(command) > 110:
            return
        try:
            self.outgoing.put_nowait(command)
        except queue.Full:
            self.event("log", "Input queue full; wait for the current redraw.")

    def run(self):
        port = serial.Serial()
        log = None
        try:
            port.port, port.baudrate = self.port, 115200
            port.timeout, port.write_timeout = 0.05, 1
            port.dtr = port.rts = False
            port.open()
            self.connected = True
            if self.log_path:
                log = open(self.log_path, "w", buffering=1, encoding="ascii", errors="replace")
            self.event("status", f"{self.port}: identifying SerialController...")
            port.write(b"VDM HELLO\n")
            deadline = time.monotonic() + 4
            heartbeat = time.monotonic()
            buffer = bytearray()
            while not self.stop.is_set():
                if time.monotonic() - heartbeat >= 2:
                    port.write(b"VDM HELLO\n")
                    heartbeat = time.monotonic()
                for _ in range(8):
                    try:
                        command = self.outgoing.get_nowait()
                    except queue.Empty:
                        break
                    self.sync.input(command, time.monotonic())
                    if command in ("VDM SNAP", "VDM ON"):
                        self.sync.requested(time.monotonic())
                        self.ready = False
                    port.write((command + "\n").encode("ascii", errors="replace"))
                with self.lock:
                    motion, self.motion = self.motion, None
                if motion and self.ready:
                    self.sync.input(motion, time.monotonic())
                    port.write((motion + "\n").encode("ascii"))
                reason = self.sync.due(time.monotonic())
                if reason:
                    self.ready = False
                    with self.lock:
                        self.motion = None
                    if reason == "stalled":
                        for command in self.sync.release_commands():
                            port.write((command + "\n").encode("ascii"))
                        self.event("input_reset", "")
                        # Abort any ACK wait before requesting a fresh command scene.
                        port.write(b"VDM OFF\n")
                    port.write(b"VDM SNAP\n")
                    self.sync.requested(time.monotonic())
                    self.event("log", f"Automatic resync: {reason}")
                    self.event("status", "Resynchronising screen...")
                chunk = port.read(max(1, min(4096, port.in_waiting)))
                self.bytes += len(chunk)
                buffer.extend(chunk)
                while b"\n" in buffer:
                    raw, _, rest = buffer.partition(b"\n")
                    buffer = bytearray(rest)
                    line = raw.decode("ascii", errors="replace").strip()
                    if not line:
                        continue
                    if log:
                        log.write(line + "\n")
                    if line == "VDM CONTROLLER 1":
                        if deadline:
                            deadline = 0
                            self.sync.requested(time.monotonic())
                            port.write(b"VDM ON\n")
                            self.event("status", "Receiving initial screen...")
                    elif line == "VDM KERNEL 1":
                        raise RuntimeError("This is the kernel port. Select the SerialController port.")
                    elif line.startswith("VDM1 ") or "VDM1 " in line:
                        try:
                            with self.lock:
                                changed = self.display.feed(line)
                                was_ready = self.ready
                                acknowledged = self.display.sequence
                                cached = self.display.asset_reply
                                records = self.display.last_records if changed else []
                                if changed:
                                    self.sync.received(records, time.monotonic())
                                self.ready = self.display.synced and self.display.enabled and not self.sync.pending
                            reply = f"VDM ACK {acknowledged}" + (f" {cached:08X}" if cached else "")
                            port.write((reply+"\n").encode("ascii"))
                            for op, args in records:
                                if op in ("F", "O"):
                                    x, y, w, h, color = args
                                    self.event("log", f"{'Filled rectangle' if op == 'F' else 'Rectangle'} "
                                               f"at ({x}, {y}), size {w} x {h}, colour {color & 0xffffff:06X}")
                                elif op == "BMP":
                                    self.event("log", f"BMP asset {args[0]:08X} at ({args[2]}, {args[3]}), "
                                               f"size {args[4]} x {args[5]}")
                                elif op == "ASSET":
                                    self.event("status", f"{'Using cached' if cached else 'Receiving'} BMP asset "
                                               f"{args[0]:08X} ({args[1]:,} bytes)")
                                elif op == "STATE" and not args[0]:
                                    self.event("status", "Serial Display is OFF in Configuration")
                            if self.ready and not was_ready:
                                self.event("status", "Live")
                        except ProtocolError as exc:
                            self.errors += 1
                            self.ready = False
                            self.sync.damaged = True
                            self.event("log", str(exc))
                            port.write(b"VDM RETRY\n")
                            self.event("status", "Retrying damaged drawing packet...")
                    elif line == "VDM SOURCE_READY":
                        self.ready = False
                        self.sync.requested(time.monotonic())
                        port.write(b"VDM ON\n")
                        self.event("status", "Kernel restarted; receiving screen...")
                    elif line == "VDM DISABLED":
                        self.ready = False
                        with self.lock:
                            self.display.enabled = False
                        self.sync.disabled()
                        self.event("status", "Enable Serial Display in kernel Configuration")
                    else:
                        self.event("log", line)
                if len(buffer) > 2048:
                    buffer.clear()
                    self.event("log", "Discarded an overlong serial log line.")
                if deadline and time.monotonic() > deadline:
                    raise RuntimeError("No display-relay response. Check the port and controller firmware.")
        except (OSError, RuntimeError, serial.SerialException) as exc:
            self.event("error", str(exc))
        finally:
            self.ready = self.connected = False
            if port.is_open:
                try:
                    port.write(b"VDM OFF\n")
                    port.flush()
                except (OSError, serial.SerialException):
                    pass
                port.close()
            if log:
                log.close()
            self.event("closed", "Disconnected")


class Viewer:
    def __init__(self, root, port=None, log_path=None):
        self.root = root
        self.log_path = log_path
        self.connection = None
        self.replay_display = None
        self.image = Image.new("RGB", (376, 285))
        self.photo = None
        self.revision = -1
        self.geometry = (0, 0, 1, 1)
        self.buttons = 0
        self.primary_mouse_mask = 0
        self.pending_motion = None
        self.messages = deque(maxlen=300)
        root.title("Citadela Serial Display")
        root.geometry("1000x820")
        root.minsize(800, 480)
        style = ttk.Style(root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        toolbar = ttk.Frame(root, padding=(10, 8))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="Controller").pack(side="left", padx=(0, 6))
        self.port = tk.StringVar(value=port or "")
        self.ports = ttk.Combobox(toolbar, textvariable=self.port, width=28, state="readonly")
        self.ports.pack(side="left")
        ttk.Button(toolbar, text="Refresh", command=self.refresh_ports).pack(side="left", padx=4)
        self.connect_button = ttk.Button(toolbar, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=4)
        ttk.Button(toolbar, text="Resync", command=self.resync).pack(side="left", padx=4)
        ttk.Button(toolbar, text="Screenshot", command=self.screenshot).pack(side="left", padx=4)
        ttk.Button(toolbar, text="Replay Log", command=self.replay).pack(side="left", padx=4)
        self.scale = tk.StringVar(value="Fit")
        scale = ttk.Combobox(toolbar, textvariable=self.scale, values=("Fit", "1x", "2x", "3x"),
                             state="readonly", width=5)
        scale.pack(side="right")
        scale.bind("<<ComboboxSelected>>", lambda _: self.render())
        self.tabs = ttk.Notebook(root)
        self.tabs.pack(fill="both", expand=True, padx=10)
        self.canvas = tk.Canvas(self.tabs, background="#202224", highlightthickness=0, takefocus=1)
        self.tabs.add(self.canvas, text="Display")
        log_frame = ttk.Frame(self.tabs)
        self.tabs.add(log_frame, text="Serial Log")
        self.log_text = tk.Text(log_frame, background="#f4f5f5", foreground="#232629",
                                wrap="word", font="TkFixedFont", state="disabled")
        self.log_text.pack(fill="both", expand=True, side="left")
        scroll = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scroll.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=scroll.set)
        footer = ttk.Frame(root, padding=(10, 8))
        footer.pack(fill="x")
        self.status = tk.StringVar(value="Disconnected")
        self.stats = tk.StringVar(value="376 x 285")
        ttk.Label(footer, textvariable=self.status).pack(side="left")
        ttk.Label(footer, textvariable=self.stats).pack(side="right")
        self.canvas.bind("<Configure>", lambda _: self.render())
        self.canvas.bind("<Motion>", self.mouse_motion)
        self.canvas.bind("<ButtonPress-1>", lambda e: self.primary_mouse_button(e, True))
        self.canvas.bind("<ButtonRelease-1>", lambda e: self.primary_mouse_button(e, False))
        self.canvas.bind("<ButtonPress-2>", lambda e: self.mouse_button(e, 2, True))
        self.canvas.bind("<ButtonRelease-2>", lambda e: self.mouse_button(e, 2, False))
        self.canvas.bind("<ButtonPress-3>", lambda e: self.mouse_button(e, 2, True))
        self.canvas.bind("<ButtonRelease-3>", lambda e: self.mouse_button(e, 2, False))
        self.canvas.bind("<MouseWheel>", self.wheel)
        self.canvas.bind("<KeyPress>", self.key)
        self.canvas.bind("<KeyRelease>", self.key_release)
        self.canvas.bind("<FocusOut>", self.release_input)
        if sys.platform == "darwin":
            root.bind("<Command-q>", self.close)
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.refresh_ports()
        root.after(33, self.tick)
        if port:
            root.after(300, self.toggle_connection)

    def refresh_ports(self):
        ports = serial_port_names(list_ports.comports())
        self.ports["values"] = ports
        if self.port.get() not in ports and ports:
            self.port.set(ports[0])
        elif not ports:
            self.port.set("")

    def toggle_connection(self):
        if self.connection:
            self.connection.stop.set()
            self.connect_button.configure(state="disabled")
            self.status.set("Disconnecting...")
            return
        if not self.port.get():
            messagebox.showerror("Serial port", "Select the SerialController port.")
            return
        self.replay_display = None
        self.revision = -1
        self.connection = Connection(self.port.get(), self.log_path)
        self.connection.thread.start()
        self.connect_button.configure(text="Disconnect")
        self.ports.configure(state="disabled")
        self.canvas.focus_set()

    def resync(self):
        if self.connection and self.connection.connected:
            self.release_input()
            self.connection.send("VDM INPUT key rlsd")
            self.connection.ready = False
            self.connection.send("VDM SNAP")
            self.status.set("Receiving screen...")

    def screenshot(self):
        path = filedialog.asksaveasfilename(defaultextension=".png",
                                          initialfile="Citadela.png", filetypes=[("PNG", "*.png")])
        if path:
            self.image.save(path)

    def replay(self):
        if self.connection:
            messagebox.showinfo("Replay", "Disconnect before opening a recording.")
            return
        path = filedialog.askopenfilename(filetypes=[("Serial recordings", "*.log *.txt"), ("All files", "*.*")])
        if not path:
            return
        display = Display(Path(__file__).parent / "AssetCache")
        try:
            with open(path, encoding="ascii", errors="replace") as log:
                for line in log:
                    display.feed(line)
        except (OSError, ProtocolError) as exc:
            messagebox.showerror("Replay failed", str(exc))
            return
        self.replay_display = display
        self.image = display.presented.copy()
        self.status.set(f"Replay: {Path(path).name}")
        self.stats.set(f"{self.image.width} x {self.image.height} | {display.commands} commands")
        self.render()

    def render(self):
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        if width < 2 or height < 2:
            return
        scale = min(width/self.image.width, height/self.image.height)
        if self.scale.get() != "Fit":
            scale = min(scale, int(self.scale.get()[0]))
        w, h = max(1, int(self.image.width*scale)), max(1, int(self.image.height*scale))
        x, y = (width-w)//2, (height-h)//2
        self.geometry = (x, y, w/self.image.width, h/self.image.height)
        self.photo = ImageTk.PhotoImage(self.image.resize((w, h), Image.Resampling.NEAREST))
        self.canvas.delete("screen")
        self.canvas.create_image(x, y, anchor="nw", image=self.photo, tags="screen")

    def position(self, event):
        if len(self.geometry) != 4:
            return None
        x, y, sx, sy = self.geometry
        px, py = int((event.x-x)//sx), int((event.y-y)//sy)
        if 0 <= px < self.image.width and 0 <= py < self.image.height:
            return px, py
        return None

    def mouse_motion(self, event):
        pos = self.position(event)
        if pos and self.connection and self.connection.ready:
            self.pending_motion = pos

    def primary_mouse_button(self, event, pressed):
        if pressed:
            # Aqua Tk maps a physical right click differently between releases.
            # Control-click is the standard macOS fallback for a secondary click.
            control_click = sys.platform == "darwin" and bool(event.state & 0x4)
            self.primary_mouse_mask = 2 if control_click else 1
        mask = self.primary_mouse_mask or 1
        self.mouse_button(event, mask, pressed)
        if not pressed:
            self.primary_mouse_mask = 0

    def mouse_button(self, event, mask, pressed):
        self.canvas.focus_set()
        pos = self.position(event)
        if not pos and not self.buttons:
            return
        if not self.connection or not self.connection.ready:
            return
        pos = pos or self.pending_motion or (0, 0)
        self.buttons = self.buttons | mask if pressed else self.buttons & ~mask
        self.pending_motion = None
        with self.connection.lock:
            self.connection.motion = None
        self.connection.send(f"VDM INPUT mouse {pos[0]} {pos[1]} {self.buttons}")

    def key(self, event):
        if not self.connection or not self.connection.ready:
            return "break"
        mapping = {"Up": "up", "Down": "down", "Left": "left", "Right": "right",
                   "Return": "enter", "Escape": "esc", "BackSpace": "backspace",
                   "Prior": "pageup", "Next": "pagedown", "Tab": "Tab", "Delete": "Delete"}
        key = mapping.get(event.keysym)
        if key:
            self.connection.send(f"VDM INPUT key {key}")
        elif event.char and event.char.isprintable() and event.char.isascii():
            key = "Space" if event.char == " " else event.char
            self.connection.send(f"VDM INPUT key {key}")
        return "break"

    def key_release(self, _):
        if self.connection and self.connection.ready:
            self.connection.send("VDM INPUT key rlsd")
        return "break"

    def wheel(self, event):
        if self.connection and self.connection.ready:
            self.connection.send("VDM INPUT key " + ("up" if event.delta > 0 else "down"))

    def release_input(self, _=None):
        if self.connection and self.connection.ready and self.buttons:
            self.connection.send("VDM INPUT mouse 0 0 0")
        self.buttons = 0

    def tick(self):
        conn = self.connection
        if conn:
            changed_log = False
            while not conn.events.empty():
                kind, text = conn.events.get_nowait()
                if kind in ("status", "error", "closed"):
                    self.status.set(text)
                if kind in ("log", "error"):
                    self.messages.append(text)
                    changed_log = True
                if kind == "closed":
                    self.connect_button.configure(text="Connect", state="normal")
                    self.ports.configure(state="readonly")
                    self.connection = None
                if kind == "input_reset":
                    self.buttons = 0
                    self.pending_motion = None
            if changed_log:
                self.log_text.configure(state="normal")
                self.log_text.delete("1.0", "end")
                self.log_text.insert("end", "\n".join(self.messages))
                self.log_text.see("end")
                self.log_text.configure(state="disabled")
            next_image = None
            with conn.lock:
                if self.pending_motion and conn.ready:
                    x, y = self.pending_motion
                    conn.motion = f"VDM INPUT mouse {x} {y} {self.buttons}"
                    self.pending_motion = None
                if conn.display.synced and conn.display.revision != self.revision:
                    self.revision = conn.display.revision
                    next_image = conn.display.presented.copy()
                stats = (f"{conn.display.commands:,} commands | {conn.bytes/1024:.1f} KiB | "
                         f"{conn.errors} errors")
            if next_image is not None:
                self.image = next_image
                self.render()
            self.stats.set(f"{self.image.width} x {self.image.height} | {stats}")
        self.root.after(33, self.tick)

    def close(self, _=None):
        self.release_input()
        if self.connection:
            self.connection.stop.set()
            self.connection.thread.join(timeout=2)
        self.root.destroy()
        return "break"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="SerialController USB port (for example COM4 or /dev/cu.usbmodem1101)")
    parser.add_argument("--record", help="Write a replayable serial command log")
    args = parser.parse_args()
    root = tk.Tk()
    Viewer(root, args.port, args.record)
    root.mainloop()


if __name__ == "__main__":
    main()
