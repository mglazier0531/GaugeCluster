# Boat GaugeCluster

Animated dashboard prototype for the **Waveshare ESP32-S3-Touch-LCD-7B / ESP32-S3-LCD-7B, 1024 x 600**, ESP32-S3 N16R8.

![Dashboard composition](assets/dashboard-preview.png)

The main reference is `reference/High-Contrast Cyan Automotive Dashboard UI.png`. Its metallic gauge faces and icons are retained as a background asset. Numeric readouts and colored bands are rendered from the same engine values: RPM 0–5000, temperature 100–260 F, oil pressure 0–100 PSI, fuel 0–100%, and voltage 10–16 V. The RPM dial is rebuilt with 0–5 major labels in thousands, minor ticks every 250 RPM, and a redline marker/sector starting at 4800 RPM. The bands fill and drain with the values; unfilled sections are dark and red warning sections brighten only when reached. White ticks and chrome remain visible. Temperature, oil, and voltage use measured tick positions from the artwork, including its nonuniform temperature scale. The generator also saves empty, half, and full band previews in `assets/` for visual verification.

The second reference, `reference/Five-Gauge Marine Performance Dashboard.png`, supplies the additional trim, AFR, intake-air temperature, accelerator position, and fuel-pressure gauge styling and icons. This version uses simulated engine readings; navigation and status icons are reference artwork, and no NMEA 2000 traffic is read or transmitted. Touch opens the display settings through the upper-right gear, or the gauge picker through any gauge. There is no on-screen demo label; the dashboard extends into the reclaimed bottom space. RPM values below 1000 omit the thousands separator, and speed is centered vertically within the top bar using the visible glyph bounds.

## Choose gauges

Tap any of the five gauges, including the large center gauge. The popup shows all ten choices at once. Tap a choice to replace only that position; tap X or outside the popup to cancel. Positions are independent, so the same gauge may appear more than once. Selections are saved to NVS and restored after restart along with the day/night preferences. The original layout is the default, and choosing the original gauge for a position restores its original face.

The available gauges are RPM (0–5000), engine temperature (100–260 F), oil pressure (0–100 PSI), fuel level (0–100%), battery voltage (10–16 V), trim (0–100%), AFR (10–18), intake-air temperature (40–180 F), accelerator position (0–100%), and fuel pressure (0–100 PSI). Every type has a moving band and its own alert rules in every position. The replacement face scales to its position, including the center position, and follows night mode.

Previews: [ten-gauge catalog](assets/gauge-catalog-preview.png), [gauge picker](assets/gauge-picker-preview.png), [alternate layout](assets/selectable-layout-preview.png), and [alternate layout at night](assets/selectable-layout-night-preview.png). The catalog and alternate-layout previews use representative normal readings; the live demo also exercises alarms.

## Gauge alerts

| Gauge | Condition | Appearance |
| --- | --- | --- |
| RPM | Above 4800 RPM | Flash red |
| Engine temperature | Above 200 F | Flash red |
| Oil pressure | Below 15 PSI | Flash red |
| Fuel | Below 20%, but at least 10% | Steady yellow |
| Fuel | Below 10% | Flash red |
| Battery voltage | Below 10 V | Flash red |
| Trim | Above 60%, only while engine RPM is greater than zero | Flash red |
| AFR | Above 16 | Flash red |
| Intake-air temperature | Above 130 F | Flash red |
| Accelerator position | Above 80% | Flash red |
| Fuel pressure | Below 25 PSI | Flash red |

Critical alerts pulse the gauge ring, track, and readout red once per second (500 ms red, 500 ms normal), keeping the number visible throughout. Low fuel is yellow during the day and muted amber at night; critical red remains red in night mode. The entire track highlights even for zero fuel or voltage below the dial minimum. Alerts clear automatically on recovery, without changing backlight settings. Comparisons use the underlying reading and are strict: 4800 RPM / 200 F / 15 PSI / 20% / 10 V do not trigger their respective critical/warning thresholds, and exactly 10% fuel is yellow.

`include/gauge_limits.h` defines limits and the shared severity classifier. `src/gauge_limits_checks.cpp` verifies every threshold boundary, trim's engine-running condition, and all 50 gauge/position choices during compilation. The 84-second demo cycle shows RPM overspeed, high temperature, low oil, yellow fuel, critical fuel, low voltage, and finally the five additional gauge alarms together before recovery. During the first three seconds of each cycle, trim is 75% with RPM zero to demonstrate the trim interlock. All ten readings are simulated and checked even when a gauge is not currently displayed. UART logs report alarm transitions. See [animated alert preview](assets/alerts-preview.gif) and [fuel warning](assets/alerts-fuel-warning.png).

With the demo running and the serial port free, `python tools/check_alarm_runtime.py --port COM9 --seconds 90` observes a full cycle and verifies all ten critical trigger/recovery events, the fuel warning, the trim engine-stop interlock, and absence of panic/watchdog messages. This checks on-device alert transitions; the generated previews allow separate visual review of colors and dial geometry.

## Alerts for gauges not displayed

All ten readings are monitored regardless of the current layout. An alert from any type absent from all five positions opens a **GAUGE ALERTS** popup with the gauge names and live readings. Critical alerts flash red; warnings flash yellow (amber at night). The border, heading, and critical rows pulse once per second while the values remain readable. Multiple hidden alerts share one popup, with critical conditions listed first. All nine possible hidden types fit without scrolling, even when one gauge is duplicated in all five positions.

Tap **ACKNOWLEDGE** to close the popup for the current episodes. A new hidden alert, a recovered alert returning, or a warning escalating to critical opens it again. The popup closes automatically when no unacknowledged hidden condition remains, including when an alerted type is placed on the dashboard. Acknowledgement does not clear the underlying condition or stop a displayed gauge from flashing. It resets at restart. The alert popup takes priority over settings or gauge selection; acknowledging it returns to the previous popup.

`include/hidden_alerts.h` tracks visibility and acknowledgement by gauge type, so duplicate displayed gauges are handled correctly. Compile-time tests cover displayed/hidden filtering, simultaneous alarms, acknowledgement, recovery, escalation, and layout changes. The UART runtime check also verifies that hidden-alert popups open and clear. [Animated popup preview](assets/hidden-alert-preview.gif).

## Check-engine light and ECU messages

The check-engine icon is absent until the monitored engine reports an ECU fault/warning. Gauge-limit alarms do not turn this icon on. Tap the lit icon to open **ENGINE FAULTS**, with a plain-language title and explanation for each reported status. Use PREV/NEXT for multiple faults and X to close. Closing does not clear the ECU fault; the icon stays lit until the ECU sends a clear report. Opening this panel takes priority over other popups and returns to them when closed. Day/night colors and saved display settings still apply.

`include/ecu_faults.h` translates standard engine-status warnings, including high temperature, low oil pressure/level, low fuel pressure, low voltage/coolant, cooling-water flow, water in fuel, charging, boost, speed limit, EGR, throttle sensing, emergency stop, power reduction, maintenance, communication, and shutdown reports. A general check-engine bit without a specific reason displays **ENGINE REPORTED A FAULT / NO SPECIFIC REASON WAS SUPPLIED**. Status flags are not presented as manufacturer trouble codes, and no mechanical cause is guessed. Preheat, secondary-throttle, neutral-start, and reserved indicators do not independently light the icon.

The adapter accepts a complete, reassembled 26-byte PGN 127489 payload through `EcuFaults::receive_127489`; it reads the two status words at bytes 20–23 and currently monitors engine instance zero (shown as Engine 1). Unavailable status words preserve prior reports, malformed packets are rejected, and other engine instances cannot clear this engine's faults. Field layout and flag meanings were checked against the [NMEA2000 engine parser](https://github.com/ttlappalainen/NMEA2000/blob/master/src/N2kMessages.cpp) and [DD206/DD223 definitions](https://github.com/ttlappalainen/NMEA2000/blob/master/src/NMEA2000StdTypes.h).

**Live NMEA 2000 reception remains disabled in this display prototype.** The demo feeds simulated ECU status packets through that adapter, independently of gauge values. The popup labels them **SIMULATED ECU DATA**. In each 84-second cycle: preheat/healthy reports keep the icon off initially; overheating appears at 12–36 seconds; it clears at 36–44 seconds; a general fault appears at 44–56 seconds; three charging/voltage/power-reduction reports appear at 56–80 seconds; and the icon clears again. Exact manufacturer-specific ECU trouble codes require the engine/ECU make, model, year, gateway, and the diagnostic information that gateway actually transmits.

`src/ecu_faults_checks.cpp` checks status decoding, generic fallback, routine-indicator filtering, unavailable/malformed data, multi-fault paging, engine isolation, and clearing during compilation. `python tools/check_ecu_runtime.py --port COM9 --seconds 90` checks the demo's off/on/clear states on the board. Previews: [healthy dashboard](assets/ecu-healthy-preview.png), [overheating explanation](assets/ecu-overheat-preview.png), and [unspecified ECU fault](assets/ecu-unspecified-preview.png).

## Display settings

Tap the upper-right gear to open settings. Drag the backlight slider from 5% to 100%; the hardware brightness changes while dragging. Tap **NIGHT MODE** to switch the entire dashboard, moving bands, readouts, icons, and settings popup between cyan/day and muted amber/night colors. Night mode initially uses 25% backlight; day mode initially uses 100%. Each mode remembers its own brightness. Preferences are saved to NVS on slider release or mode change and restored at startup. Tap **X**, the gear again, or outside the popup to close it.

The GT911 is reset through EXIO1 with GPIO4 selecting its I2C address. Touch is polled at 20 ms on the existing GPIO8/GPIO9 bus, with a queue separating input from rendering. The backlight uses Waveshare's inverted IO-extension PWM (`100 - brightness`); the 5% minimum keeps the controls visible. If touch initialization fails, the dashboard continues and reports the fault on UART.

Generated previews: [day settings](assets/settings-day-preview.png), [night settings](assets/settings-night-preview.png), and [night dashboard](assets/dashboard-night-preview.png).

## Build and flash

Install Python and PlatformIO Core, then run from this folder:

```powershell
pio run
pio run -t upload --upload-port COM9
pio device monitor --port COM9 --baud 115200
```

The pinned PlatformIO Espressif32 platform installs ESP-IDF and the compiler automatically. Firmware uses 16 MB flash, 8 MB octal PSRAM, RGB565, two panel framebuffers, and Waveshare's panel timing with a conservative 20 MHz pixel clock. Only changing readout regions are restored each frame to reduce PSRAM traffic. UART flashing uses the USB-to-UART connector; the USB native connector shares signals with CAN selection on this board. If automatic bootloader entry fails, hold BOOT, press/release RESET, release BOOT, and retry upload.

## Assets and driver provenance

To regenerate assets, install Pillow (`python -m pip install Pillow`) and run `python tools/generate_assets.py`. The generators use Windows Arial Bold Italic and Arial Bold; generated assets are committed so ordinary firmware builds need no image tools or fonts. Original reference files are preserved unchanged. The original logical layout's first 570 rows are expanded to 600 physical rows, extending the bottom status bar to the screen edge. Background, text, restore regions, and band masks use the same vertical mapping.

`tools/selectable_assets.py` extracts the reference icons and generates ten 320-pixel gauge faces and sparse animated tracks. `include/gauge_layout.h` defines the five display positions. Firmware caches the composed day/night backgrounds and scales each selected track into its position when the layout changes. Only values, bands, alert rings, and visible popups are updated during animation. The five gauge IDs are saved as the `gauges` blob in the existing `display` NVS namespace.

The `src/i2c.*`, `src/io_extension.*`, and `src/rgb_lcd_port.*` hardware drivers come from [Waveshare's official 06_LCD example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B/tree/master/examples/Arduino/examples/06_LCD), upstream commit `c652c902db607f7ffb376257393cfd7657aa6428`. Their license is preserved in `vendor/WAVESHARE-LICENSE`. See [board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B).

`src/main.cpp` separates the `EngineData` model and demo producer from drawing. Future work: replace the demo producer with NMEA 2000 PGN handling, real navigation data, and missing/stale-data behavior. Do not use the simulated display as an engine instrument.
