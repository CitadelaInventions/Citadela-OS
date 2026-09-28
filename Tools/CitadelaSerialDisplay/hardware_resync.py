"""Opt-in COM test of periodic refresh and recovery from an undecodable redraw."""

import argparse
from pathlib import Path
import time

from protocol import ProtocolError
from viewer import Connection


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    conn = Connection(args.port, args.output / "resync.log")
    automatic = []

    def wait(predicate, timeout=90):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            while not conn.events.empty():
                kind, value = conn.events.get_nowait()
                if kind == "error":
                    raise RuntimeError(value)
                if value.startswith("Automatic resync:"):
                    automatic.append(value)
                    print(value, flush=True)
                elif kind == "status" and value == "Live":
                    print("Live", flush=True)
            if predicate():
                return
            time.sleep(.05)
        raise RuntimeError("Timed out waiting for automatic screen recovery")

    conn.thread.start()
    try:
        wait(lambda: conn.ready)
        initial_due = conn.sync.next_periodic
        conn.send("VDM INPUT config")
        wait(lambda: "Configuration" in "".join(t[2] for t in conn.display.text))
        wait(lambda: "Automatic resync: periodic" in automatic, 45)
        wait(lambda: conn.ready and conn.sync.next_periodic > initial_due)
        print("30-second periodic resync reconstructed Configuration using cached assets.", flush=True)

        original_feed = conn.display.feed
        fault = {"armed": True}

        def inject_fault(line):
            if fault["armed"]:
                if line.startswith("VDM1 1 ") and " H " in line:
                    fault["armed"] = False
                else:
                    raise ProtocolError("Injected undecodable redraw for recovery test")
            return original_feed(line)

        with conn.lock:
            conn.display.feed = inject_fault
        conn.send("VDM INPUT utilities")
        wait(lambda: conn.errors > 0, 10)
        failed_at = time.monotonic()
        wait(lambda: "Automatic resync: stalled" in automatic, 20)
        wait(lambda: conn.ready and not fault["armed"], 30)
        with conn.lock:
            conn.display.presented.save(args.output / "utilities-recovered.png")
        print("Stalled Utilities redraw recovered in", round(time.monotonic()-failed_at, 2),
              "seconds; decoder errors were deliberately injected.", flush=True)
        conn.send("VDM INPUT home")
    finally:
        conn.stop.set()
        conn.thread.join(timeout=3)


if __name__ == "__main__":
    main()
