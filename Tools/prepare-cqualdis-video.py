#!/usr/bin/env python3
"""Prepare a silent video and palette thumbnail for CQualDis.

The ESP32 cannot decode ordinary H.264 in its available RAM. This tool uses
OpenCV's FFmpeg backend to decode an input video and write an MJPEG-in-MP4
version at 376x192 and no more than 12 frames/second. Put both output files in
the SD card's /Videos folder. The player can then decode individual JPEG
samples while the display continues scanning its existing framebuffer.

The companion .cth format is intentionally tiny:

    bytes 0..3   ASCII "CQTH"
    bytes 4..5   uint16 little-endian width (80)
    bytes 6..7   uint16 little-endian height (45)
    bytes 8..    width*height row-major palette indices

Its palette is the driver's indexedColor palette: indices 0..31 are neutral
grays; 32..255 form a 7x8x4 RGB cube. A pixel within 6 levels of neutral in
all channels uses a gray entry. The player should pass these indices directly
to the driver's indexed-color framebuffer, rather than treat them as RGB332.

Requires: ``python3 -m pip install opencv-python`` (NumPy is its dependency).
"""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
from typing import BinaryIO, Iterator

try:
    import cv2
    import numpy as np
except ImportError as exc:
    raise SystemExit(
        "OpenCV and NumPy are required. Install with: "
        "python3 -m pip install opencv-python"
    ) from exc


VIDEO_WIDTH = 376
VIDEO_HEIGHT = 192
THUMB_WIDTH = 80
THUMB_HEIGHT = 45
MAX_FPS = 12.0
THUMB_HEADER = struct.Struct("<4sHH")


def letterbox_bgr(frame: np.ndarray, width: int, height: int) -> np.ndarray:
    source_height, source_width = frame.shape[:2]
    if source_width <= 0 or source_height <= 0:
        raise ValueError("Input has an invalid frame size")
    scale = min(width / source_width, height / source_height)
    fitted_width = max(1, min(width, round(source_width * scale)))
    fitted_height = max(1, min(height, round(source_height * scale)))
    interpolation = cv2.INTER_AREA if scale < 1 else cv2.INTER_LINEAR
    fitted = cv2.resize(frame, (fitted_width, fitted_height), interpolation=interpolation)
    result = np.zeros((height, width, 3), dtype=np.uint8)
    x = (width - fitted_width) // 2
    y = (height - fitted_height) // 2
    result[y : y + fitted_height, x : x + fitted_width] = fitted
    return result


def indexed_palette_bytes(frame_bgr: np.ndarray) -> bytes:
    """Quantize a BGR frame exactly like CTBComposite::indexedColor()."""
    rgb = frame_bgr[..., ::-1].astype(np.int32)
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    neutral = rgb.max(axis=2) - rgb.min(axis=2) <= 6
    gray = (19595 * red + 38470 * green + 7471 * blue + 0x8000) >> 16
    gray_index = (gray * 31 + 127) // 255
    ri = (red * 6 + 127) // 255
    gi = (green * 7 + 127) // 255
    bi = (blue * 3 + 127) // 255
    color_index = 32 + ((ri * 8 + gi) * 4 + bi)
    return np.where(neutral, gray_index, color_index).astype(np.uint8).tobytes()


def make_thumbnail(first_frame: np.ndarray) -> bytes:
    preview = letterbox_bgr(first_frame, THUMB_WIDTH, THUMB_HEIGHT)
    return THUMB_HEADER.pack(b"CQTH", THUMB_WIDTH, THUMB_HEIGHT) + indexed_palette_bytes(preview)


def boxes(handle: BinaryIO, start: int, end: int) -> Iterator[tuple[bytes, int, int]]:
    """Walk ISO-BMFF boxes without reading video sample data into memory."""
    offset = start
    while offset < end:
        if end - offset < 8:
            raise ValueError("Truncated MP4 box header")
        handle.seek(offset)
        header = handle.read(8)
        box_size, box_type = struct.unpack(">I4s", header)
        header_size = 8
        if box_size == 1:
            extension = handle.read(8)
            if len(extension) != 8:
                raise ValueError("Truncated extended MP4 box size")
            box_size = struct.unpack(">Q", extension)[0]
            header_size = 16
        elif box_size == 0:
            box_size = end - offset
        if box_size < header_size or box_size > end - offset:
            raise ValueError(f"Invalid MP4 box {box_type!r} at {offset}")
        payload = offset + header_size
        box_end = offset + box_size
        yield box_type, payload, box_end
        offset = box_end


def find_box(handle: BinaryIO, start: int, end: int, box_type: bytes) -> tuple[int, int]:
    for name, payload, box_end in boxes(handle, start, end):
        if name == box_type:
            return payload, box_end
    raise ValueError(f"MP4 is missing {box_type.decode('ascii')} box")


def descriptor(data: bytes, offset: int, end: int) -> tuple[int, int, int, int]:
    if offset >= end:
        raise ValueError("Truncated ES descriptor")
    tag = data[offset]
    offset += 1
    size = 0
    for _ in range(4):
        if offset >= end:
            raise ValueError("Truncated ES descriptor length")
        byte = data[offset]
        offset += 1
        size = (size << 7) | (byte & 0x7F)
        if not byte & 0x80:
            break
    else:
        raise ValueError("Invalid ES descriptor length")
    payload_end = offset + size
    if payload_end > end:
        raise ValueError("ES descriptor exceeds its box")
    return tag, offset, payload_end, payload_end


def esds_object_type(data: bytes) -> int:
    if len(data) < 4:
        raise ValueError("Truncated esds box")
    tag, start, finish, _ = descriptor(data, 4, len(data))
    if tag != 0x03 or finish - start < 3:
        raise ValueError("esds is missing an ES_Descriptor")
    flags = data[start + 2]
    child = start + 3
    if flags & 0x80:  # streamDependenceFlag
        child += 2
    if flags & 0x40:  # URL_Flag
        if child >= finish:
            raise ValueError("Truncated ES URL")
        child += 1 + data[child]
    if flags & 0x20:  # OCRstreamFlag
        child += 2
    if child > finish:
        raise ValueError("Truncated ES_Descriptor")
    while child < finish:
        tag, payload, item_end, child = descriptor(data, child, finish)
        if tag == 0x04:
            if payload >= item_end:
                raise ValueError("Empty DecoderConfigDescriptor")
            return data[payload]
    raise ValueError("esds is missing a DecoderConfigDescriptor")


def validate_mjpeg_mp4(path: Path, expected_frames: int, expected_fps: float) -> None:
    """Reject files a desktop can play but the constrained player cannot."""
    with path.open("rb") as handle:
        file_end = path.stat().st_size
        moov_start, moov_end = find_box(handle, 0, file_end, b"moov")
        video_tracks = 0
        audio_tracks = 0
        for name, trak_start, trak_end in boxes(handle, moov_start, moov_end):
            if name != b"trak":
                continue
            mdia_start, mdia_end = find_box(handle, trak_start, trak_end, b"mdia")
            hdlr_start, hdlr_end = find_box(handle, mdia_start, mdia_end, b"hdlr")
            handle.seek(hdlr_start)
            hdlr = handle.read(min(12, hdlr_end - hdlr_start))
            if len(hdlr) < 12:
                raise ValueError("Truncated MP4 track handler")
            if hdlr[8:12] == b"soun":
                audio_tracks += 1
            if hdlr[8:12] != b"vide":
                continue
            video_tracks += 1
            minf_start, minf_end = find_box(handle, mdia_start, mdia_end, b"minf")
            stbl_start, stbl_end = find_box(handle, minf_start, minf_end, b"stbl")
            stsd_start, stsd_end = find_box(handle, stbl_start, stbl_end, b"stsd")
            handle.seek(stsd_start)
            stsd_header = handle.read(8)
            if len(stsd_header) < 8:
                raise ValueError("Truncated stsd box")
            entry_count = struct.unpack(">I", stsd_header[4:8])[0]
            if entry_count != 1:
                raise ValueError(f"Expected one video codec entry, got {entry_count}")
            entry_start = stsd_start + 8
            entries = boxes(handle, entry_start, stsd_end)
            try:
                entry_type, entry_payload, entry_end = next(entries)
            except StopIteration as exc:
                raise ValueError("stsd has no video codec entry") from exc
            if entry_type != b"mp4v":
                raise ValueError(f"Expected MP4 sample entry mp4v, got {entry_type!r}")
            # ISO VisualSampleEntry has 78 bytes between the box header and
            # its first child. FFmpeg places the esds child after this header.
            child_start = entry_payload + 78
            if child_start > entry_end:
                raise ValueError("Truncated visual sample entry")
            esds_start, esds_end = find_box(handle, child_start, entry_end, b"esds")
            handle.seek(esds_start)
            oti = esds_object_type(handle.read(esds_end - esds_start))
            if oti != 0x6C:  # JPEG per ISO/IEC 14496-1 ObjectTypeIndication
                raise ValueError(f"Expected MJPEG OTI 0x6c, got 0x{oti:02x}")
        if video_tracks != 1:
            raise ValueError(f"Expected one video track, got {video_tracks}")
        if audio_tracks:
            raise ValueError("Converted MP4 unexpectedly contains audio")

    check = cv2.VideoCapture(str(path), cv2.CAP_FFMPEG)
    try:
        if not check.isOpened():
            raise ValueError("OpenCV could not reopen the converted video")
        fourcc = int(check.get(cv2.CAP_PROP_FOURCC))
        actual_fourcc = "".join(chr((fourcc >> (8 * shift)) & 0xFF) for shift in range(4))
        if actual_fourcc != "MJPG":
            raise ValueError(f"Decoded codec is {actual_fourcc!r}, expected MJPG")
        width = round(check.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = round(check.get(cv2.CAP_PROP_FRAME_HEIGHT))
        if (width, height) != (VIDEO_WIDTH, VIDEO_HEIGHT):
            raise ValueError(f"Converted size is {width}x{height}, expected 376x192")
        actual_fps = check.get(cv2.CAP_PROP_FPS)
        if not math.isclose(actual_fps, expected_fps, rel_tol=0.001, abs_tol=0.01):
            raise ValueError(f"Converted frame rate is {actual_fps}, expected {expected_fps}")
        frame_count = round(check.get(cv2.CAP_PROP_FRAME_COUNT))
        if frame_count != expected_frames:
            raise ValueError(f"Converted frame count is {frame_count}, expected {expected_frames}")
        ok, decoded = check.read()
        if not ok or decoded.shape[:2] != (VIDEO_HEIGHT, VIDEO_WIDTH):
            raise ValueError("Could not decode the first converted frame")
    finally:
        check.release()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert H.264 MP4 to CQualDis-compatible silent MJPEG MP4 plus .cth preview"
    )
    parser.add_argument("input", type=Path, help="source MP4 (usually H.264)")
    parser.add_argument(
        "-o", "--output", type=Path,
        help="output MP4; defaults to INPUT-STEM-CQualDis.mp4",
    )
    parser.add_argument(
        "--fps", type=float, default=12.0,
        help="maximum output frames/second, 0 < FPS <= 12 (default: 12)",
    )
    parser.add_argument(
        "--thumbnail", type=Path,
        help="preview path; defaults to the output MP4 path with .cth suffix",
    )
    parser.add_argument(
        "--force", action="store_true",
        help="replace existing output files; never replaces the input",
    )
    return parser.parse_args()


def path_identity(path: Path) -> Path:
    return path.expanduser().resolve(strict=False)


def convert(source: Path, output: Path, thumbnail: Path, fps_limit: float, force: bool) -> tuple[int, float]:
    source, output, thumbnail = map(path_identity, (source, output, thumbnail))
    if not source.is_file():
        raise ValueError(f"Input video does not exist: {source}")
    if source.suffix.lower() != ".mp4":
        raise ValueError("Input must be an MP4 file")
    if output.suffix.lower() != ".mp4" or thumbnail.suffix.lower() != ".cth":
        raise ValueError("Output extensions must be .mp4 and .cth")
    if len({source, output, thumbnail}) != 3:
        raise ValueError("Input, output, and thumbnail must have distinct paths")
    if not math.isfinite(fps_limit) or not 0 < fps_limit <= MAX_FPS:
        raise ValueError("--fps must be greater than 0 and at most 12")
    if not force:
        for path in (output, thumbnail):
            if path.exists():
                raise ValueError(f"Output already exists: {path} (use --force to replace)")
    output.parent.mkdir(parents=True, exist_ok=True)
    thumbnail.parent.mkdir(parents=True, exist_ok=True)

    capture = cv2.VideoCapture(str(source), cv2.CAP_FFMPEG)
    if not capture.isOpened():
        raise ValueError(f"OpenCV/FFmpeg cannot open: {source}")
    temp_video: Path | None = None
    temp_thumbnail: Path | None = None
    writer: cv2.VideoWriter | None = None
    try:
        source_fps = capture.get(cv2.CAP_PROP_FPS)
        if not math.isfinite(source_fps) or source_fps <= 0:
            raise ValueError("Source has no usable frame rate")
        source_frame_count = capture.get(cv2.CAP_PROP_FRAME_COUNT)
        expected_source_frames = (
            round(source_frame_count)
            if math.isfinite(source_frame_count) and source_frame_count > 0 else 0
        )
        output_fps = min(source_fps, fps_limit)
        with tempfile.NamedTemporaryFile(
            prefix=f".{output.stem}-", suffix=".mp4", dir=output.parent, delete=False
        ) as temporary:
            temp_video = Path(temporary.name)

        writer = cv2.VideoWriter(
            str(temp_video), cv2.CAP_FFMPEG,
            cv2.VideoWriter_fourcc(*"MJPG"), output_fps,
            (VIDEO_WIDTH, VIDEO_HEIGHT), True,
        )
        if not writer.isOpened():
            raise ValueError("OpenCV/FFmpeg could not create an MJPEG MP4")

        decoded_count = 0
        written_count = 0
        first_frame: np.ndarray | None = None
        while True:
            ok, frame = capture.read()
            if not ok:
                break
            # Sample at the first input frame at or after each output time.
            # Using the source's reported rate keeps the file's duration close
            # to the original without introducing duplicate frames.
            if decoded_count * output_fps + 1e-7 >= written_count * source_fps:
                fitted = letterbox_bgr(frame, VIDEO_WIDTH, VIDEO_HEIGHT)
                writer.write(fitted)
                if first_frame is None:
                    first_frame = fitted
                written_count += 1
            decoded_count += 1
            if decoded_count % 300 == 0:
                print(f"Decoded {decoded_count} frames; wrote {written_count}", file=sys.stderr)

        writer.release()
        writer = None
        if written_count == 0 or first_frame is None:
            raise ValueError("Input contains no decodable video frames")
        if expected_source_frames > 0 and decoded_count + 2 < expected_source_frames:
            raise ValueError(
                f"Input stopped early after {decoded_count}/{expected_source_frames} frames"
            )
        validate_mjpeg_mp4(temp_video, written_count, output_fps)

        with tempfile.NamedTemporaryFile(
            prefix=f".{thumbnail.stem}-", suffix=".cth", dir=thumbnail.parent,
            delete=False,
        ) as temporary:
            temp_thumbnail = Path(temporary.name)
            temporary.write(make_thumbnail(first_frame))
        if temp_thumbnail.stat().st_size != THUMB_HEADER.size + THUMB_WIDTH * THUMB_HEIGHT:
            raise ValueError("Thumbnail size check failed")
        # Validation completes before either destination is made visible.
        os.replace(temp_video, output)
        temp_video = None
        os.replace(temp_thumbnail, thumbnail)
        temp_thumbnail = None
        return written_count, output_fps
    finally:
        capture.release()
        if writer is not None:
            writer.release()
        for path in (temp_video, temp_thumbnail):
            if path is not None:
                path.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    source = path_identity(args.input)
    output = path_identity(args.output or source.with_name(f"{source.stem}-CQualDis.mp4"))
    thumbnail = path_identity(args.thumbnail or output.with_suffix(".cth"))
    try:
        frames, fps = convert(source, output, thumbnail, args.fps, args.force)
    except (OSError, ValueError) as exc:
        print(f"CQualDis conversion failed: {exc}", file=sys.stderr)
        return 1
    print(f"Video: {output} ({frames} frames, {fps:g} fps, {output.stat().st_size:,} bytes)")
    print(f"Preview: {thumbnail} ({thumbnail.stat().st_size:,} bytes)")
    print("Copy both files into /Videos on the Citadela SD card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
