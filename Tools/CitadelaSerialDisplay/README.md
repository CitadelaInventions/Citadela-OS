# Citadela Serial Display

The kernel records drawing commands on Serial1. The SerialController relays them
to its USB serial port, and this Python app reconstructs the screen. The existing
CVBS output, PAL 4x encoder, resolution, and colour calibration are unchanged.

## Start on macOS

1. Double-click `Launch Citadela Serial Display.command`. On its first run, the
   launcher creates a private `.venv` and installs Pillow and pyserial without
   changing the system Python installation. If macOS opens the file as text,
   run `chmod +x "Launch Citadela Serial Display.command"` once in Terminal.
2. Select the **SerialController** port, not the kernel port. macOS normally names
   USB serial ports `/dev/cu.usbmodem*` or `/dev/cu.usbserial*`; two attached ESPs
   will appear as two similar entries, so disconnecting one briefly is the safest
   way to identify the controller.
3. Enable **Serial Display** in the kernel Configuration menu, then Connect.
   Wait for the scene and its files to finish transferring and the status to say Live.
4. Click the display to focus it. Mouse clicks, movement, arrows, Enter, Escape,
   printable ASCII keys, and key releases go back to the kernel.

Use Resync after a missed update, Screenshot to save a native-resolution PNG,
and the Serial Log tab to inspect controller messages. Close or Disconnect to
stop drawing logs. The Configuration toggle is saved immediately to SPIFFS.
The viewer also resynchronises every 30 seconds while input is idle. A redraw
or requested scene with no valid drawing progress for 12 seconds is restarted
automatically. Active file transfers are allowed to finish, cached assets are
retained, and automatic resync stops when Serial Display is OFF. The previous
completed screen stays visible while a replacement scene is reconstructed.
A controller-side heartbeat also stops logs after ten
seconds without the viewer, including after a viewer crash.

The launcher requires Python 3.9 or newer with Tk. The installer from
[python.org](https://www.python.org/downloads/macos/) includes Tk. No serial-port
driver is needed for USB CDC devices; boards using a separate USB-to-serial chip
may require that chip vendor's macOS driver.

## Start on Windows

Run `Launch Citadela Serial Display.bat`, then select the **SerialController** COM
port. On the tested setup the controller and kernel are COM4 and COM3 respectively;
Windows may assign different ports.

Manual setup or command-line use on either platform:

```sh
python3 -m pip install -r requirements.txt
python3 viewer.py --port /dev/cu.usbmodem1101 --record session.log
python3 -m unittest discover -v
```

Use a `COM` port instead of `/dev/cu.*` in the Windows commands.

Replay Log reconstructs a recorded session without either ESP. Recording is
optional; `--record` includes ordinary controller logs as well as drawing packets,
so keep recordings private if the displayed content is sensitive.

## What Is Mirrored

- Filled rectangles, outlines, lines, circles, scrolling, and text with the exact
  bitmap font supplied by the firmware.
- Original BMP files for wallpapers and icons, followed by commands specifying
  position, dimensions, clipping, colour settings, and transparency thresholds.
- Cursor coordinates and a movable window outline, reconstructed as overlays.
- A command-based scene redraw on Connect or Resync, without a framebuffer scan.
  Commands are presented together at a frame boundary, not line by line.

Files are cached in `AssetCache` using a content hash. The first transfer of a
wallpaper can still take tens of seconds at 115200 baud, but subsequent redraws
and reconnects reuse the file and send only drawing commands. Keep this cache
when replaying recorded logs.

Mirroring is opt-in and adds no framebuffer on either ESP. Drawing calls only
enqueue commands; they never write UART, wait for acknowledgements or transfer
files. Once the local frame is complete, the main loop incrementally sends its
log, hashes assets and transfers file chunks, handling input between packets.
The kernel allocates about 1.5 KiB of small regular-RAM transport allocations,
plus lazy word-packed asset records and a command queue capped at 40 KiB in the
ESP32's otherwise-unused word-addressable IRAM heap. All IRAM access uses aligned
32-bit loads/stores, preserving scarce byte-addressable/DMA memory. Allocation
starts only after video initialisation and leaves at least 12 KiB in the IRAM
pool. The kernel releases all transport memory before a wallpaper fetch
reinitialises video. If the queue fills, a fresh current scene replaces the
backlog. The existing unbuffered UART can take approximately 20 ms to write one
complete packet after rendering; acknowledgement waits are nonblocking.
Clock ticks still render locally every second; when a scene/file transfer is
queued, only the latest clock value is mirrored once the backlog clears.
The controller relay runs
in a separate FreeRTOS task so Bluetooth reconnects do not block it. The Python
app acknowledges each checked packet through the controller; damaged packets are
retried without redrawing the entire screen.

This build instruments the kernel. Existing separately flashed app binaries do
not automatically gain mirroring, and the controller's own loading screen is not
mirrored. A future app can bind `Citadela::SerialDisplay` to its shared display
wrapper and handle the same session commands. PC colours are the framebuffer's
digital palette, not a simulation of analogue CVBS artefacts or monitor settings.

## Protocol

USB: 115200 8N1. Kernel/controller Serial1: existing 256000 8N1 wiring. Connect the
normal kernel TX to controller RX and controller TX to kernel RX, with common GND.
No CVBS cable is used to carry the desktop commands. Do not connect GPIO25 to a
PC serial adapter.

PC control lines:

- `VDM HELLO`: controller replies `VDM CONTROLLER 1`.
- `VDM ON` / `VDM SNAP`: request a new command-based scene if Configuration allows it.
- `VDM OFF`: disable capture.
- `VDM INPUT mouse X Y BUTTONS`: absolute native-screen coordinates, left bit 1,
  right bit 2.
- `VDM INPUT key KEY`: existing kernel serial-control key names; `rlsd` on release.

Packets are printable ASCII:

```text
VDM1 SEQUENCE CRC16 PAYLOAD
```

CRC16 is CCITT-FALSE over PAYLOAD only (initial 0xFFFF, polynomial 0x1021).
Every command ends in `;`, including the last. Drawing and file-transfer payloads
are at most 559 bytes, with file packets carrying up to 384 original
file bytes encoded as Base64. The relay validates CRC, and the desktop returns
`VDM ACK SEQUENCE` (optionally followed by a cached asset ID) to the kernel
through the controller. The desktop checks CRC and sequence numbers, ignores
retransmissions, and sends `VDM RETRY` after corruption. Sequence 1 containing H
begins a fresh session.

| Command | Properties |
| --- | --- |
| H | width height |
| P | palette offset, concatenated 8-digit AABBGGRR entries |
| FILL / RECT | x y width height AABBGGRR (filled / outline rectangle) |
| LINE | x0 y0 x1 y1 AABBGGRR |
| CIRCLE | x y radius filled AABBGGRR |
| GLYPH | x y width height foreground background character hex glyph mask |
| SCROLL | vertical scroll delta, fill colour |
| LUT | table slot, offset, hex bytes for tone/gamma/colour conversion |
| ASSET | content hash, original BMP file size |
| DATA | asset hash, byte offset, Base64 file bytes |
| ASSETEND | asset hash, commit and verify transferred file |
| ASSETREF | path alias, content hash; bind a deferred file after transfer or cache lookup |
| BMP | asset hash or path alias, mode, placement, clipping and colour/alpha settings |
| CURSOR | x y visible outlined, calibrated white and black |
| DRAG | visible x y width height, calibrated outline colour |
| FRAME | present a completed batch of drawing commands |
| STATE | serial display enabled (0 or 1) |
| B | x y, hex palette-index bytes (up to 48 pixels) |
| D | x y, hex count/index pairs for run-length compressed pixels |
| R | x y count palette-index (solid span) |
| E | command-based scene complete |
| CHECK | rectangle bounds and RGB checksum (diagnostic; no pixel transfer) |

All rectangles use width/height extents, not inclusive bottom-right coordinates.
Glyph masks are row-major, most-significant bit first, four pixels per hex digit.
Transparent glyph background pixels leave the existing desktop untouched.
The B/D/R pixel commands remain available for unusual low-level drawing, but
normal desktop wallpapers, icons, cursor movement and window dragging do not
use them. LUT slots 1/2 hold wallpaper tone and icon gamma; slots 3-5 hold the
existing calibrated indexed RGB conversion.

## Firmware And Recovery

Sources: `System/kernel/kernel.ino`, `System/SerialController/SerialController.ino`,
`System/Libraries/CitadelaDisplay.h`, `CitadelaSerialDisplay.h`, and
`CitadelaDisplayRelay.h`.

The pre-change recovery copies are in `Recovery/SerialDisplay/20260912` and
`Recovery/SerialDisplay/20260913-post-render`. Do not
restore a controller image to the kernel ESP. Never erase NVS/SPIFFS as part of a
normal update. A kernel binary stored on the SD also needs updating before an
older app restores it, otherwise its old kernel will not support this protocol.
