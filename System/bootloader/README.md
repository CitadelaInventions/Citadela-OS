# Citadela bootloader app

`bootloader.ino` is an ESP32 OTA application. `/System/bootloader.bin` is the
image that the kernel installs in the inactive OTA slot; it is not the ESP32
second-stage bootloader image at flash offset `0x1000`.

The app uses the same 4 MB `default` partition scheme, 256000 baud controller
link, 376 × 288 PAL display mode, and `/evil.txt` boot state as the current
kernel. It uses `CitadelaDisplay`, `CitadelaSerialCommands`, and
`CitadelaStorage`. The stock bitluni compatibility switch only selects the
standard PAL drawing path; this app does not use the project's PAL4x encoder.

Menu options install `/System/kernel.bin`, `/System/WifiEditor.bin`, or
`/System/bootloader.bin` from the SD card as OTA app images. Repair reinstalls
the checked kernel image without erasing data partitions. Esc from the main
menu starts the kernel; Tab opens BLE keyboard selection. USB serial accepts
the same controller commands and digits 1–5 on the main menu.

To build with Arduino CLI and ESP32 core 3.3.x:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=default,FlashSize=4M,PSRAM=disabled --output-dir /tmp/citadela-bootloader-build System/bootloader
```

Copy `bootloader.ino.bin` from the output directory to
`System/bootloader.bin` for the SD card, or use the UART Uploader's Bootloader
package target to transfer both the image and source. On 2026-09-30, the
package was transferred to a connected ESP32, installed as an OTA app by the
kernel, and used to reinstall the kernel from SD. BLE input and the separate
SerialController video handoff were not available to verify on that device.
