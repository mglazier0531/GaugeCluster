# Boat GaugeCluster

Animated dashboard prototype for the **Waveshare ESP32-S3-Touch-LCD-7B / ESP32-S3-LCD-7B, 1024 x 600**, ESP32-S3 N16R8.

![Dashboard composition](assets/dashboard-preview.png)

The main reference is `reference/High-Contrast Cyan Automotive Dashboard UI.png`. Its metallic gauge faces, icons, ticks, and bars are retained as a background asset, with independently rendered animated numeric readouts and moving cyan markers. The second image is reserved for future selectable gauges. This first version is a visual demo: all engine readings are simulated, navigation and status icons are reference artwork, touch/settings are not implemented, and no NMEA 2000 traffic is read or transmitted. The fixed cyan bands are part of the artwork; moving cyan dots indicate changing readings. A DEMO label is shown beneath the dashboard.

## Build and flash

Install Python and PlatformIO Core, then run from this folder:

```powershell
pio run
pio run -t upload --upload-port COM9
pio device monitor --port COM9 --baud 115200
```

The pinned PlatformIO Espressif32 platform installs ESP-IDF and the compiler automatically. Firmware uses 16 MB flash, 8 MB octal PSRAM, RGB565, two panel framebuffers, and Waveshare's panel timing with a conservative 20 MHz pixel clock. Only changing readout regions are restored each frame to reduce PSRAM traffic. UART flashing uses the USB-to-UART connector; the USB native connector shares signals with CAN selection on this board. If automatic bootloader entry fails, hold BOOT, press/release RESET, release BOOT, and retry upload.

## Assets and driver provenance

To regenerate assets, install Pillow (`python -m pip install Pillow`) and run `python tools/generate_assets.py`. The generator uses Windows Arial Bold Italic for its rasterized numeric font; generated assets are committed so ordinary firmware builds need no image tools or fonts. Original reference files are preserved unchanged. Layout is fitted proportionally to the screen.

The `src/i2c.*`, `src/io_extension.*`, and `src/rgb_lcd_port.*` hardware drivers come from [Waveshare's official 06_LCD example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B/tree/master/examples/Arduino/examples/06_LCD), upstream commit `c652c902db607f7ffb376257393cfd7657aa6428`. Their license is preserved in `vendor/WAVESHARE-LICENSE`. See [board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B).

`src/main.cpp` separates the `EngineData` model and demo producer from drawing. Future work: replace the demo producer with NMEA 2000 PGN handling, missing/stale-data behavior, actual alarms/navigation, and user-selectable gauges. Do not use the simulated display as an engine instrument.
