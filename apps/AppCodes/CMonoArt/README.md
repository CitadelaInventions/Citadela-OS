# CMonoArt

CMonoArt draws binary Mandelbrot contours at 940 × 288 through the Citadela
PAL4x video output. Each framebuffer pixel is one bit: 0 is black and 1 is
white. The packed image uses 118 bytes per row, or 33,984 bytes total. The
two existing DMA scanlines still contain 16-bit DAC samples; only the image
buffer is packed. No gray or color values are stored in the image.

940 is the number of active picture samples in the current PAL4x progressive
scanline, and 288 is its number of distinct active rows. This is the maximum
one-to-one raster for that timing. A composite TV may resolve less horizontal
detail than the logical pixel count.

Build from the source-only PAL4x library snapshot and additive mono patch:

```sh
Tools/build-cmonoart.sh
```

This writes `apps/CMonoArt.bin`. The kernel lists that app once its binary is
copied to `/apps/CMonoArt.bin` on the SD card. Use arrow keys to pan, `+` and
`-` to zoom, `R` to reset, and Escape to return to the kernel. `MONO INFO` on
USB serial prints the active resolution, stride, image bytes, and free heap.

The exact unchanged Arduino-library backup is kept locally at
`Recovery/CVBS/pre-1bit-super-resolution-20260930/bitluni_ESP32Lib`.
`vendor/bitluni_ESP32Lib_pal4x` contains the source needed for builds,
without example sketches or duplicate Finder copies. The additive patch is
`Tools/patches/bitluni-mono1.patch`; the build script applies it to a
temporary copy and leaves the installed Arduino library alone.
