# reTerminal Sticky Calculator Demo

A two-page calculator demo for the [reTerminal Sticky](https://www.seeedstudio.com/reTerminal-Sticky-p-5778.html), built with ESP-IDF v5.4.

It pairs a 1972 TI-2500 Datamath-inspired calculator with a modern iPhone-style calculator. Both use the Sticky's 480 x 800 portrait E-Ink display, GT911 touch panel, buttons, buzzer, battery gauge, and Deep Sleep wake flow.

## Features

- TI-2500 Datamath page with CHAIN / CONST behavior and 8-digit display rules
- iPhone Calculator page with expression display, Delete, percentage, sign toggle, thousands separators, and RAM history
- UP or DOWN cycles between the two calculator pages
- Touch input with asynchronous display updates and buzzer feedback
- 1-bit black-and-white E-Ink rendering with periodic full refreshes
- Battery percentage and charging-state logging
- AI short press clears the current calculation and fully refreshes the page
- AI long press (about one second) powers off; long press again wakes the device
- Cold boot shows the calculator wallpaper and enters Deep Sleep until the AI button is held

## Requirements

- reTerminal Sticky
- ESP-IDF v5.4
- USB-C cable

The target is `esp32s3`, with a 32 MB flash configuration and Octal PSRAM enabled.

## Build and flash

Open an ESP-IDF v5.4 terminal in this directory:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the Sticky serial port. Use `Ctrl+]` to leave the monitor.

After flashing, the wallpaper is shown and the device sleeps. Hold the AI button for about one second to start Calculator.

## Controls

| Input | Action |
| --- | --- |
| Touch | Enter calculator keys on the active page |
| UP / DOWN | Cycle between Datamath and iPhone Calculator |
| AI short press | Clear calculation and full-refresh the active page |
| AI long press | Power off or wake the device |
| iPhone History icon | Open recent calculation history |

## Project layout

```text
main/
|-- app/       Calculator state, input routing, display task, history
|-- assets/    Compiled Inter glyphs, TI logo, and power-off wallpaper
|-- board/     Sticky power and shared I2C setup
|-- devices/   E-Ink, touch, buttons, buzzer, battery, charger, sleep
`-- ui/        Canvas plus Datamath, iPhone, and wallpaper views
tools/         Reproducible bitmap/font conversion scripts
components/    BQ27220, GT911, button, and SSD1677 dependencies
```

`assets/INTER-OFL-1.1.txt` contains the Inter font license. The generated Inter bitmap glyphs in `main/assets/` are the only font data used by the firmware; no runtime TTF decoder is included.
