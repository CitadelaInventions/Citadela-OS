"""Opt-in hardware test: local rendering must not wait for PC acknowledgements."""

import argparse
from pathlib import Path
import threading
import time
import serial

from viewer import Connection
from protocol import Display


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--kernel-port", default="COM3")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fresh-cache", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    conn = Connection(args.port, args.output / "controller.log")
    if args.fresh_cache:
        conn.display = Display(args.output / "assets")
    usb = serial.Serial()
    usb.port, usb.baudrate, usb.timeout = args.kernel_port, 115200, .05
    usb.dtr = usb.rts = False
    usb.open()
    kernel_log = open(args.output / "kernel.log", "w", encoding="ascii", errors="replace")

    def wait(predicate, seconds=90):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if usb.in_waiting:
                text = usb.read(usb.in_waiting).decode("ascii", errors="replace")
                kernel_log.write(text)
                kernel_log.flush()
                if "transport allocation failed" in text:
                    raise RuntimeError(text)
            while not conn.events.empty():
                kind, value = conn.events.get_nowait()
                if kind == "error":
                    raise RuntimeError(value)
            if predicate():
                return
            time.sleep(.02)
        raise RuntimeError("Hardware test timed out")

    def render_home():
        usb.reset_input_buffer()
        start = time.monotonic()
        usb.write(b"SC home\nSC help\n")
        data = bytearray()
        while time.monotonic() - start < 8:
            data.extend(usb.read(max(1, usb.in_waiting)))
            if b"[SC] commands:" in data:
                elapsed = time.monotonic() - start
                kernel_log.write(data.decode("ascii", errors="replace"))
                kernel_log.flush()
                return elapsed
        raise RuntimeError("Kernel failed to answer after rendering home")

    conn.thread.start()
    try:
        wait(lambda: conn.ready)
        usb.write(b"VDM OFF\n")
        time.sleep(.2)
        baseline = [render_home() for _ in range(3)]
        print("Local home render, capture OFF:", [round(t, 3) for t in baseline], flush=True)
        conn.send("VDM SNAP")
        wait(lambda: conn.sync.pending)
        wait(lambda: conn.ready)

        paused = threading.Event()
        armed = {"value": True}
        original_feed = conn.display.feed

        def delayed_ack(line):
            if armed["value"]:
                armed["value"] = False
                paused.set()
                time.sleep(3)
            return original_feed(line)

        with conn.lock:
            conn.display.feed = delayed_ack
        conn.send("VDM INPUT config")
        if not paused.wait(5):
            raise RuntimeError("No configuration log received for delayed ACK test")
        elapsed = render_home()
        print("Local home render while PC ACK is paused for 3s:", round(elapsed, 3), "s", flush=True)
        if elapsed >= 2:
            raise RuntimeError("Local rendering still appears to wait for the PC")
        time.sleep(3)
        conn.send("VDM CHECK 0 195 300 60")
        wait(lambda: bool(conn.display.checks), 30)
        with conn.lock:
            if conn.display.checks[-1][-2] != conn.display.checks[-1][-1]:
                raise RuntimeError(f"Queued rendering differs from kernel: {conn.display.checks[-1]}")
            conn.display.presented.save(args.output / "home-after-paused-ack.png")
        if conn.errors:
            raise RuntimeError(f"Queued transmission caused {conn.errors} protocol errors")
        print("Input remained responsive; queued home/wallpaper matches the framebuffer; no protocol errors.", flush=True)
    finally:
        conn.stop.set()
        conn.thread.join(timeout=5)
        kernel_log.close()
        usb.close()


if __name__ == "__main__":
    main()
