# CompositeColorDAC calibration

`CompositeColorDAC` exposes a versioned `CompositeColorCalibration` profile.
The same profile is loaded automatically from ESP32 NVS by every recompiled
application that uses `CompositeColorDAC`.

```cpp
CompositeColorDAC video;

CompositeColorCalibration profile =
    CompositeColorCalibration::defaults(true); // 100/150 ohm divider
profile.brightness = 8;
profile.contrast = 110;
profile.saturation = 95;
profile.blackLevel = 64;
profile.whiteLevel = 205;

video.setCalibration(profile);
video.saveCalibration();
video.init(CompMode::MODEPALColor288Pmid, 25, true);
```

## Picture fields

- `brightness`: luma offset from -128 to 127.
- `contrast`: luma gain in percent, where 100 is neutral.
- `saturation`: chroma gain in percent, where 100 is neutral.
- `hueDegrees`: active-picture hue rotation from -180 to 180 degrees.
- `redGain`, `greenGain`, `blueGain`: per-channel gain in percent.
- `gammaX100`: gamma control, where 100 is neutral.

## Signal fields

- `syncLevel`, `blankingLevel`, `blackLevel`, `whiteLevel`: raw DAC codes.
- `clipMin`, `clipMax`: numeric active-signal clipping limits.
- `burstAmplitude`, `burstPhaseDegrees`: colour-burst controls.
- `activePhaseDegrees`, `activeStartSamples`: active carrier alignment.

Active chroma is normalized to `burstAmplitude`, then limited to the available
DAC headroom before carrier samples are generated. This keeps high saturation
values from clipping into black/white zigzag columns while preserving the
receiver's burst-to-picture colour reference.

`MODEPALColor288Pmid` uses a two-line DMA renderer clocked at four times the
PAL subcarrier (17.734476 MHz). The 376x288 framebuffer stores indexed colours;
the interrupt renderer expands them into phase-locked carrier samples. Thirty-
two dedicated grayscale entries guarantee that neutral pixels have zero
chroma instead of relying on RGB quantization.

Profile version 3 restores the nominal burst amplitudes (`31` with the
documented divider and `11` for a direct DAC connection). Version 2 profiles
are migrated proportionally because reducing burst made the receiver's chroma
AGC amplify luma-edge noise and false colours. All other saved picture and
timing controls are retained.

## Timing fields

The horizontal, vertical, burst-position, and clock fields are bounded offsets
from the selected `ModeComposite`. Active resolution is never changed by a
saved profile. Clock offsets are in parts per million.

On the 4x PAL backend, brightness, contrast, saturation, hue, black, white, and
clipping changes rebuild the palette and affect the existing framebuffer.
Sync, blanking, burst, porch, and clock changes are encoded into DMA line
buffers and require a controlled video release and reinitialization. Use
`CompositeColorDAC::requiresSignalRestart(before, after)` to detect that case.

The Citadela CRGB application provides live controls and stores a readable copy
at `/videoCalibration.conf`. Its USB recovery commands are:

```text
CAL GET
CAL SET <field name> <integer value>
CAL APPLY
CAL SAVE
CAL RESET
CAL UNDO
```

If a timing or sync setting makes the television lose lock, send `CAL RESET`
and then `CAL APPLY` over USB serial.
