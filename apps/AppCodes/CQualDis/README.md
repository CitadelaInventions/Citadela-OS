# CQualDis

CQualDis renders five full raster, strictly black and white scenes through the
Citadela composite DAC. TAB cycles through Mandelbrot contours, a clean UI
type specimen with A–Z, a–z and 0–9, a detailed observatory etching, and an
interactive projected 3D trefoil knot and orbital gyroscope. Arrows pan the
fractal or rotate either 3D model; + and - zoom; R resets; Escape returns to
the kernel. The SerialController mouse cursor is visible in the app. In either
3D scene, click the left mouse button once to turn on orbit control. Move the
mouse to rotate without holding the button; click again to turn it off.
Switching TAB modes turns orbit control off.

The PAL Mono1 Ultra7x mode is 1645 × 288. Its packed framebuffer uses 206
bytes per row, 59,328 bytes total. Pixel 0 is black; pixel 1 is white. The
two DMA scanlines retain 16-bit DAC samples, each 3,976 bytes. The driver also
offers 1410 × 288 (Ultra6x) and 940 × 288 (Super) mono modes. Ultra7x is the
largest one-to-one horizontal mode that fits a complete scanline under the
4,092-byte single DMA descriptor limit. Actual visible detail depends on the
analog display and cable.

The current 240 MHz ESP32 build uses 485,738 of 1,310,720 application flash
bytes (37%) and 78,320 of 327,680 bytes of statically allocated DRAM (23%).
The compiled `.bin` is 485,888 bytes. The display allocates the 59,328-byte
framebuffer and 7,952 bytes for two DMA scanlines at runtime. Rendering either
3D model uses one shared 45,938-byte packed scratch image. With the same
rendering code, the last connected-board check reported 139,884 free heap bytes
after the 3D scratch was allocated; that is a historical measurement, not a
new reading from the renamed image. DMA-capable free heap is part of the free
heap figure and must not be added to it.

That check measured a maximum of 12,833 CPU cycles to prepare a PAL scanline
against a 15,373-cycle line budget, with no over-budget lines in 250,062
observed lines. At 240 MHz, that is about 53.5 µs within a 64.1 µs deadline.
This maximum describes the tightest observed scanline, not average CPU usage.
The 3D scenes completed a view in roughly 66–91 ms; the default fractal and
etching scenes took about 487 ms each. The video signal continues scanning
while a new scene is calculated. Beyond this mode, both the DMA descriptor
limit and the scanline deadline constrain a direct resolution increase.

The 3D scenes use projected geometry and 1-bit ordered dithering instead of
per-pixel ray marching. The knot's immutable samples are cached. Each new
view is rasterized into a packed 45,938-byte canvas, then compared with the
visible interior. Only bytes with changed final pixels are committed; an
unchanged view writes no picture bytes. The cursor restores its saved background
bits before a render or framebuffer dump. Mandelbrot skips points known to lie
in its main cardioid or period-two bulb and mirrors rows when centered on the
real axis. The observatory's static ridge coordinates are cached. Commands
that leave the current view unchanged skip rendering entirely. Frame CRC-32
uses a lookup table while retaining the previous checksum values.

Build from the source-only PAL4x library snapshot and additive mono patch:

```sh
Tools/build-cqualdis.sh
```

This writes `apps/CQualDis.bin`. The kernel lists the app when this binary is
stored at `/apps/CQualDis.bin` on the SD card. The build script stages the
library in a temporary directory and leaves the installed Arduino library
untouched. The original library backup is in
`Recovery/CVBS/pre-1bit-super-resolution-20260930/bitluni_ESP32Lib`; the
pre-optimization app, binary, and patch backup is in
`Recovery/CVBS/pre-mono-boundaries-20261001`.

`MONO INFO` on USB serial reports resolution, stride, heap, maximum measured
scanline render cycles, render budget, and count of lines exceeding that
budget. Each completed scene prints its render time, CRC-32 and white pixel
count. `MONO DUMP` streams the exact packed framebuffer. Its framing is
`MONO DUMP START <width> <height> <stride> <bytes>\n`, then `<bytes>` raw
row-major bytes (most significant bit is the leftmost pixel), followed by
`\nMONO DUMP END\n`. Wait for `MONO FRAME` before requesting a dump.
Both 3D scenes also print `MONO DELTA` with changed byte and pixel counts.

The UI uses a flash-resident 1-bit raster of the open licensed Inter typeface;
the typeface license and generator are in `fonts/`.
