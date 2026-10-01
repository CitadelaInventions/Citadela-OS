# CQualDis video player

CQualDis plays prepared, silent colour videos from `/Videos` on the Citadela SD
card. It uses the same 376 × 192 PAL4x composite display mode as CImage. The
browser shows each video's filename and an 80 × 45 preview made from its first
frame. It lists up to 32 MP4 files in the folder.

## Prepare a video

An ordinary H.264 MP4 needs conversion on a computer before Citadela can play
it. Install OpenCV for Python if needed, then run:

```sh
python3 -m pip install opencv-python
python3 Tools/prepare-cqualdis-video.py "/path/to/source.mp4" \
  -o "/path/to/SD-card/Videos/MyVideo.mp4"
```

The command creates `MyVideo.mp4` and `MyVideo.cth` beside it. If converting
elsewhere, copy **both** files to `/Videos` on the SD card. The folder must be
at the card's root. Use `--fps 10` to set a lower maximum frame rate, or
`--force` to replace an existing converted pair. The converter never replaces
the source video. The player derives the displayed title from the MP4 filename;
use a short, descriptive name when choosing the output path.

With CQualDis running and its USB serial port connected, the prepared pair can
also be sent without removing the SD card:

```sh
python3 -m pip install pyserial
python3 Tools/upload-cqualdis-video.py "/path/to/MyVideo.mp4" \
  --port /dev/cu.usbserial-0001
```

The uploader finds `MyVideo.cth` beside the MP4, validates both files, and
transfers them to `/Videos` with CRC-32 and an acknowledgement after every
512-byte chunk. If there is exactly one USB serial port, `--port` can be
omitted. `--name NewTitle` changes the destination basename for both files;
names may use ASCII letters, digits, dots, underscores and hyphens, must begin
with a letter or digit, and may be at most 59 characters before the extension.
The player refuses to overwrite existing files. Remove an old pair from the
SD card or choose a new name before retrying. If an upload is interrupted,
check `/Videos` for a partial pair before uploading again. Press R in the
browser to rescan after a successful transfer. USB uploads accept files up to
2 GiB each; the serial transfer can take a long time for large MJPEG files.

The converter uses OpenCV's FFmpeg backend to read H.264 and write a 376 × 192
letterboxed MJPEG MP4 with one video track, no audio, and at most 12 frames per
second. It checks the result by reopening it, decoding a frame, and verifying
the MP4 codec fields (`stsd` sample entry `mp4v`, `esds` JPEG object type
`0x6c`). If the local OpenCV build cannot produce that format, conversion
fails rather than writing a file the app cannot play.

The `.cth` preview is 3,608 bytes: the four ASCII bytes `CQTH`, little-endian
16-bit width `80`, little-endian 16-bit height `45`, then 3,600 row-major
8-bit palette indices. Indices 0–31 are neutral grays; 32–255 are a 7 × 8 × 4
RGB colour cube. Keep the preview and MP4 filenames identical except for the
extension. A video without its `.cth` file still appears in the browser, but
shows a placeholder instead of a preview.

## Install and use

Build the app with:

```sh
Tools/build-cqualdis.sh
```

The script writes `apps/CQualDis.bin`. Store it on the SD card as
`/apps/CQualDis.bin`, then launch CQualDis from the Citadela OS app list. The
build script stages the PAL4x library in a temporary directory and leaves the
installed Arduino library untouched.

In the browser, Up/Down or Left/Right changes the selected video and Enter
starts playback. The mouse wheel also changes selection. Click a card once to
select it; click the selected card to play it. Press R to rescan `/Videos` after
adding files. Escape returns to the kernel.

During playback, Enter or Space pauses or resumes. Escape stops playback and
returns to the browser. Playback is silent; seeking is not implemented.

## How it runs and its limits

The PAL signal is generated from the driver's indexed-colour framebuffer on
core 1. A task pinned to core 0 reads JPEG samples from the SD card and decodes
them into two 376 × 16 palette stripes. Core 1 compares each completed stripe
with the displayed framebuffer and writes only pixels whose final palette
index changed. A frame with identical pixels makes no picture writes, although
the decoder still reads and compares that frame. The queue between cores holds
only bounded stripes, so the app does not load the whole movie into RAM.

The 376 × 192 framebuffer is 144,384 bytes because each indexed pixel occupies
a 16-bit backing word. The two stripe buffers hold 12,032 bytes together; the
thumbnail buffer is 3,600 bytes. The JPEG decoder reserves 17,884 bytes of
static RAM so it does not need a large contiguous heap block after the display
starts. The decoder task has an 8,192-byte stack; SD, queues, and the display
need additional memory. Current
free-heap and DMA figures print as `CQUALDIS READY` and `CQUALDIS PLAY` on USB
serial. The former Mono1 app's memory and timing measurements do not describe
this player.

On the connected original ESP32, a 30-frame prepared clip played to completion
with 61,076 bytes of free heap during playback. A five-frame identical-image
clip reported `changed=0` for all four repeated frames. These are functional
checks, not a guarantee that every 12 fps clip will keep its nominal timing.

Only a non-fragmented MP4 with complete JPEG frames at exactly 376 × 192 is
supported. The MP4 parser reads sample tables from the card as needed and
rejects H.264 streams, composition-offset tracks, and files of 4 GiB or more.
The converter produces the supported layout. MJPEG files are usually much
larger than their H.264 sources. Twelve frames per second is the converter's
maximum output rate, not a guaranteed playback rate; actual speed depends on
SD reads, JPEG complexity, and display work. The app reports changed-pixel
counts and free heap in `CQUALDIS FRAME` serial messages.
