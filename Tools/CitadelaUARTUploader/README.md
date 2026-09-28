# Citadela UART Uploader

This Windows utility compiles an Arduino `.ino` sketch for the Citadela ESP32 profile and transfers the compiled binary plus the original source through the kernel ESP's USB UART.

## Use

1. On Citadela, open **Configuration > UART Upload**.
2. Run `Launch Citadela UART Uploader.bat`.
3. Open or drop an `.ino` file, select the kernel ESP serial port, and choose **Compile & Upload**.

Application packages are installed as:

- `/apps/<AppName>.bin`
- `/apps/AppCodes/<AppName>/<AppName>.ino`

Kernel packages replace:

- `/System/kernel.bin`
- `/System/kernel/kernel.bin`
- `/System/kernel/kernel.ino`

Every file is written to a temporary path and checked with CRC32 before the final files are replaced. Existing files are restored if a final rename fails.

Install optional drag-and-drop support with:

```powershell
py -3 -m pip install -r requirements.txt
```

The uploader finds Arduino CLI inside Arduino IDE 2 automatically. Set `CITADELA_ARDUINO_CLI` if it is installed elsewhere.
