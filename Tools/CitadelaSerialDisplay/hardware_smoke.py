"""Opt-in hardware test: reconstruct scenes, compare checksums and test toggle."""

import argparse
from pathlib import Path
import time
from viewer import Connection


def pump(conn, seconds, until_ready=False):
    end = time.monotonic()+seconds
    while time.monotonic() < end:
        while not conn.events.empty():
            kind, value = conn.events.get_nowait()
            print(kind, value.encode("ascii", "backslashreplace").decode(), flush=True)
            if kind == "error":
                raise RuntimeError(value)
        if until_ready and conn.ready:
            return
        time.sleep(.05)
    if until_ready:
        raise RuntimeError(f"No complete screen after {seconds}s; seq={conn.display.sequence}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    conn = Connection(args.port, args.output / "hardware.log")
    conn.thread.start()
    try:
        start = time.monotonic()
        pump(conn, 90, until_ready=True)
        with conn.lock:
            conn.display.image.save(args.output / "home.png")
            print("Initial scene:", round(time.monotonic()-start, 2), "seconds;",
                  conn.display.sequence, "packets;", conn.bytes, "bytes", flush=True)
        conn.send("VDM INPUT config")
        pump(conn, 3)
        with conn.lock:
            conn.display.image.save(args.output / "config.png")
            print("Config glyphs:", "".join(t[2] for t in conn.display.text), flush=True)
            if not conn.display.text:
                raise RuntimeError("No incremental text commands received")
        conn.send("VDM CHECK 34 54 308 194")
        pump(conn, 2)
        with conn.lock:
            if not conn.display.checks or conn.display.checks[-1][-2] != conn.display.checks[-1][-1]:
                raise RuntimeError(f"Configuration differs from actual framebuffer: {conn.display.checks}")
        print("Configuration command rendering matches the actual framebuffer.", flush=True)
        conn.send("VDM INPUT mouse 250 221 1")
        conn.send("VDM INPUT mouse 250 221 0")
        pump(conn, 2)
        if conn.display.enabled or conn.ready:
            raise RuntimeError("Configuration toggle did not disable serial rendering")
        stopped_sequence = conn.display.sequence
        pump(conn, 1)
        if conn.display.sequence != stopped_sequence:
            raise RuntimeError("Drawing packets continued while serial rendering was disabled")
        print("Configuration toggle OFF stops drawing packets.", flush=True)
        conn.send("VDM INPUT mouse 250 221 1")
        conn.send("VDM INPUT mouse 250 221 0")
        pump(conn, 90, until_ready=True)
        print("Configuration toggle ON reconstructs the current scene.", flush=True)
        conn.send("VDM CHECK 246 217 9 9")
        pump(conn, 1)
        if conn.display.checks[-1][-2] != conn.display.checks[-1][-1]:
            raise RuntimeError(f"Cursor overlay differs from actual framebuffer: {conn.display.checks[-1]}")
        conn.ready = False
        resync_start = time.monotonic()
        conn.send("VDM SNAP")
        pump(conn, 90, until_ready=True)
        with conn.lock:
            conn.display.image.save(args.output / "config-snapshot.png")
        print("Cached scene reconstruction:", round(time.monotonic()-resync_start, 2), "seconds", flush=True)
        conn.send("VDM INPUT mouse 100 40 1")
        conn.send("VDM INPUT mouse 100 40 0")
        pump(conn, 3)
        if not conn.display.drag:
            raise RuntimeError("Window title click did not create a semantic drag outline")
        conn.send("VDM INPUT mouse 120 50 0")
        pump(conn, 1)
        x, y, w, h, _ = conn.display.drag
        conn.send(f"VDM CHECK {x} {y} 10 1")
        pump(conn, 1)
        if conn.display.checks[-1][-2] != conn.display.checks[-1][-1]:
            raise RuntimeError("Window outline differs from actual framebuffer")
        conn.send("VDM INPUT mouse 120 50 1")
        conn.send("VDM INPUT mouse 120 50 0")
        pump(conn, 3)
        if conn.display.drag:
            raise RuntimeError("Dropping the window did not remove its semantic outline")
        print("Window dragging uses a calibrated outline and redraws on drop.", flush=True)
        conn.send("VDM INPUT mouse 80 60 0")
        conn.send("VDM INPUT key down")
        conn.send("VDM INPUT key rlsd")
        pump(conn, 4)
        conn.send("VDM INPUT home")
        pump(conn, 5)
        conn.send("VDM CHECK 0 195 300 60")
        pump(conn, 2)
        with conn.lock:
            conn.display.image.save(args.output / "home-return.png")
            print("Final:", conn.display.commands, "commands;", conn.errors, "protocol errors", flush=True)
            if not conn.display.checks or conn.display.checks[-1][-2] != conn.display.checks[-1][-1]:
                raise RuntimeError(f"Wallpaper differs from actual framebuffer: {conn.display.checks}")
        if not conn.ready:
            raise RuntimeError("Display did not recover from a protocol error")
    finally:
        conn.stop.set()
        conn.thread.join(timeout=3)


if __name__ == "__main__":
    main()
