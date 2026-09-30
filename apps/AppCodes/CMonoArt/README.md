# CMonoArt

CMonoArt renders four full raster, strictly black and white scenes through the
Citadela composite DAC. TAB cycles through Mandelbrot contours, a clean UI
type specimen with A–Z, a–z and 0–9, a detailed observatory etching, and an
interactive projected 3D trefoil knot. Arrows pan the fractal or rotate the
knot; + and - zoom; R resets; Escape returns to the kernel.

The PAL Mono1 Ultra7x mode is 1645 × 288. Its packed framebuffer uses 206
bytes per row, 59,328 bytes total. Pixel 0 is black; pixel 1 is white. The
two DMA scanlines retain 16-bit DAC samples, each 3,976 bytes. The driver also
offers 1410 × 288 (Ultra6x) and 940 × 288 (Super) mono modes. Ultra7x is the
largest one-to-one horizontal mode that fits a complete scanline under the
4,092-byte single DMA descriptor limit. Actual visible detail depends on the
analog display and cable.

The 3D scene uses projected, depth-sorted geometry and 1-bit ordered dithering
instead of per-pixel ray marching. The Mandelbrot renderer skips points known
to lie in its main cardioid or period-two bulb.

Build from the source-only PAL4x library snapshot and additive mono patch:

```sh
Tools/build-cmonoart.sh
```

This writes `apps/CMonoArt.bin`. The kernel lists the app when this binary is
stored at `/apps/CMonoArt.bin` on the SD card. The build script stages the
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

The UI uses a flash-resident 1-bit raster of the open licensed Inter typeface;
the typeface license and generator are in `fonts/`.
