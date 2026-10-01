#!/usr/bin/env python3
"""Upload a prepared CQualDis MP4 and its .cth preview over USB serial.

Requirements: pyserial and opencv-python. Convert H.264 source files first with
prepare-cqualdis-video.py. The player must already be running on the Citadela.
The device refuses to overwrite an existing destination file.

Protocol (115200 baud, DTR/RTS off): PING/PONG, PUT filename size CRC32,
PUT_READY 512, raw 512-byte chunks with cumulative ACK, then PUT_DONE.
"""

from __future__ import annotations

import argparse
import binascii
import importlib.util
import math
from pathlib import Path
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit(
        "pyserial is required. Install with: python3 -m pip install pyserial"
    ) from exc


BAUD = 115200
CHUNK_SIZE = 512
MAX_FILE_SIZE = (1 << 31) - 1
NAME_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z", re.ASCII)
THUMB_HEADER = b"CQTH" + (80).to_bytes(2, "little") + (45).to_bytes(2, "little")
THUMB_SIZE = 8 + 80 * 45


class UploadError(Exception):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload prepared CQualDis MJPEG MP4 and matching .cth over USB"
    )
    parser.add_argument("video", type=Path, help="prepared MJPEG MP4")
    parser.add_argument(
        "--port", help="USB serial port; auto-detects if exactly one USB port is present"
    )
    parser.add_argument(
        "--name", help="destination stem in /Videos (default: source MP4 stem)"
    )
    return parser.parse_args()


def choose_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    candidates = [
        port.device for port in list_ports.comports()
        if "usb" in port.device.lower() or "usb" in port.description.lower()
    ]
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise UploadError("No USB serial port found; pass --port explicitly")
    raise UploadError(
        "Several USB serial ports found; pass --port explicitly: "
        + ", ".join(candidates)
    )


def validate_name(stem: str) -> tuple[str, str]:
    # The firmware accepts at most 63 bytes including the extension.
    if not stem or len(stem) + 4 > 63 or not NAME_PATTERN.fullmatch(stem):
        raise UploadError(
            "Destination name must start with a letter or digit, use only "
            "ASCII letters, digits, dot, underscore or hyphen, and be at most "
            "59 characters before .mp4/.cth"
        )
    return stem + ".mp4", stem + ".cth"


def validate_thumbnail(path: Path) -> None:
    if not path.is_file():
        raise UploadError(f"Missing matching preview: {path}")
    if path.stat().st_size != THUMB_SIZE:
        raise UploadError(f"Invalid .cth size: expected {THUMB_SIZE} bytes")
    with path.open("rb") as source:
        if source.read(8) != THUMB_HEADER:
            raise UploadError("Invalid .cth header; expected CQTH, 80 x 45")


def validate_video(path: Path) -> None:
    if not path.is_file() or path.suffix.lower() != ".mp4":
        raise UploadError("Input must be an existing prepared .mp4 file")
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_SIZE:
        raise UploadError("Video is empty or exceeds the USB transfer limit (2 GiB)")

    converter = Path(__file__).with_name("prepare-cqualdis-video.py")
    if not converter.is_file():
        raise UploadError(f"Converter validator is missing: {converter}")
    spec = importlib.util.spec_from_file_location("cqualdis_video_prepare", converter)
    if spec is None or spec.loader is None:
        raise UploadError("Cannot load converter validator")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except SystemExit as exc:
        raise UploadError(str(exc)) from exc
    capture = module.cv2.VideoCapture(str(path), module.cv2.CAP_FFMPEG)
    try:
        if not capture.isOpened():
            raise UploadError("Cannot open prepared MP4")
        frame_count = capture.get(module.cv2.CAP_PROP_FRAME_COUNT)
        fps = capture.get(module.cv2.CAP_PROP_FPS)
        if (not math.isfinite(frame_count) or frame_count < 1 or
                not math.isfinite(fps) or not 0 < fps <= 12.01):
            raise UploadError("Prepared MP4 must have frames and at most 12 fps")
        frames = round(frame_count)
    finally:
        capture.release()
    try:
        module.validate_mjpeg_mp4(path, frames, fps)
    except (OSError, ValueError) as exc:
        raise UploadError(f"Prepared MP4 validation failed: {exc}") from exc


def crc32_file(path: Path) -> tuple[int, int]:
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_SIZE:
        raise UploadError(f"File is empty or too large: {path}")
    crc = 0
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            crc = binascii.crc32(block, crc)
    return size, crc & 0xFFFFFFFF


class Device:
    def __init__(self, port: str) -> None:
        connection = serial.Serial()
        connection.port = port
        connection.baudrate = BAUD
        connection.timeout = 0.25
        connection.write_timeout = 10
        connection.dtr = False
        connection.rts = False
        connection.open()
        connection.dtr = False
        connection.rts = False
        self.connection = connection

    def __enter__(self) -> Device:
        return self

    def __exit__(self, *_args: object) -> None:
        self.connection.close()

    def send_line(self, line: str) -> None:
        packet = (line + "\n").encode("ascii")
        if self.connection.write(packet) != len(packet):
            raise UploadError("Short serial command write")
        self.connection.flush()

    def read_line(self, deadline: float) -> str | None:
        while time.monotonic() < deadline:
            raw = self.connection.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if line:
                if line == "CQUALDIS PUT_ERROR" or line.startswith("CQUALDIS PUT_ERROR "):
                    raise UploadError(line.removeprefix("CQUALDIS PUT_ERROR ") or
                                      "The player refused the upload")
                return line
        return None

    def wait_for(self, prefix: str, seconds: float) -> str:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = self.read_line(deadline)
            if line is None:
                break
            if line.startswith(prefix):
                return line
        raise UploadError(f"Timed out waiting for {prefix.strip()}")

    def ping(self) -> None:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            self.send_line("CQUALDIS PING")
            short_deadline = min(deadline, time.monotonic() + 1)
            while time.monotonic() < short_deadline:
                line = self.read_line(short_deadline)
                if line == "CQUALDIS PONG":
                    return
        raise UploadError("CQualDis did not respond; launch the app and check the USB port")

    def upload(self, source: Path, basename: str) -> None:
        size, crc = crc32_file(source)
        print(f"Uploading {basename}: {size:,} bytes, CRC32 {crc:08X}")
        self.send_line(f"CQUALDIS PUT {basename} {size} {crc:08X}")
        ready = self.wait_for("CQUALDIS PUT_READY ", 10)
        if ready != f"CQUALDIS PUT_READY {CHUNK_SIZE}":
            raise UploadError(f"Unexpected transfer setup: {ready}")
        sent = 0
        last_progress = 0.0
        with source.open("rb") as data:
            while sent < size:
                chunk = data.read(min(CHUNK_SIZE, size - sent))
                if not chunk:
                    raise UploadError("Local file ended during transfer")
                if self.connection.write(chunk) != len(chunk):
                    raise UploadError("Short serial data write")
                self.connection.flush()
                sent += len(chunk)
                ack = self.wait_for("CQUALDIS ACK ", 15)
                if ack != f"CQUALDIS ACK {sent}":
                    raise UploadError(f"Unexpected byte acknowledgement: {ack}")
                now = time.monotonic()
                if sent == size or now - last_progress >= 1.0:
                    print(f"  {sent:,}/{size:,} bytes ({sent * 100 / size:.1f}%)", flush=True)
                    last_progress = now
        done = self.wait_for("CQUALDIS PUT_DONE ", 30)
        expected = f"CQUALDIS PUT_DONE {basename} {size} {crc:08X}"
        if done != expected:
            raise UploadError(f"Unexpected completion response: {done}")
        print(f"Verified {basename}")


def main() -> int:
    args = parse_args()
    video = args.video.expanduser().resolve(strict=False)
    preview = video.with_suffix(".cth")
    try:
        names = validate_name(args.name if args.name is not None else video.stem)
        validate_thumbnail(preview)
        validate_video(video)
        port = choose_port(args.port)
        print(f"CQualDis USB port: {port}")
        with Device(port) as device:
            device.ping()
            # Send the video first. If its name already exists, the device
            # refuses it before any preview changes on the SD card.
            device.upload(video, names[0])
            device.upload(preview, names[1])
        print("Upload complete. Press R in the CQualDis browser to rescan /Videos.")
        return 0
    except (OSError, ValueError, UploadError, serial.SerialException) as exc:
        print(f"CQualDis upload failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
