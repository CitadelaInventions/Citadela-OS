# Citadela System Libraries

These headers collect common firmware functions into small Arduino-friendly modules.

- `CitadelaDisplay.h`: composite display wrapper, releasable DMA framebuffer, raw pixel access, black/white polarity filter.
- `CitadelaSerialCommands.h`: SerialController video/loading/fetch handoff commands and line reading.
- `CitadelaStorage.h`: SPIFFS/SD init, file copy, hashes, file comparison.
- `CitadelaUI.h`: rectangles, clickable button hit testing, simple window drawing.
- `CitadelaMouseCursor.h`: reusable saved-pixel mouse cursor with hover/click callbacks.
- `CitadelaWallpaperRenderer.h`: one-call 24-bit BMP full/partial wallpaper renderer using row buffers and run-length fills.
- `CitadelaConfig.h`: SPIFFS config parser and writer helpers.
- `CitadelaBLEHID.h`: BLE HID UUIDs, keyboard/mouse report helpers, wake/report-mode setup.
- `CitadelaOS.h`: convenience include for all common modules.

Common usage is intentionally one-call style:

```cpp
CitaCursor.Begin(videodisplay, SCREEN_WIDTH, SCREEN_HEIGHT, &mouseCursorOutlined);
CitaCursor.SetCallbacks(kernelCursorUpdateHover, kernelCursorActivateClick);
CitaCursor.MoveMouseTo(x, y, buttons);
```

The libraries are header-only so sketches can include them with a relative path from the `System` folder without a separate Arduino library installation.

# Citadela System Planning

The following order shows each step towards a successfull boot of the system.

1)  Power on.
2)  Kernel gets initialised - FrRTOS.
3)  DiskDrum enables DiskBarn using common protocol.
4)  DiskDrum administers power to driver-enabled components.
5)  System initializes storage container.
6)  System loads an image from container that loads initialization data such as configurations and personalizations. 
7)  System splits a partition for the DMA buffer to allocate free-data sectors.
8)  System states to DiskBarn "I2STX" that VD is initialized.
9)  DiskBarn dismisses initial ghost transmission.
10) System dismisses communication with storage container and frees heap.
11) System loads UI and replugs terminal component.

# Citadela System Specifications

CPU_CLOCK: 240MHz
CPU_ARCHITECTURE: Tensilica Xtensia LX6 x32
CPU_CORES: 2

HEAPRAM: 512KB
USABLEHEAP: 320KB
INITHEAP: 260KB

DMAHEAP: 160KB

POWER_VDCB: 5V BRD
POWER_VDCL: 3.3V LGC
POWER_AMP: 1A - 2A

COMMUNICATION: Protocol UART
COM_DEVICE: CR2102
COM_ATTACH: Automatic Injection/ Automatic Flashing/ Automatic Restarting