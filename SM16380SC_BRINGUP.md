# SM16380SC bit-banged first-light firmware

This firmware is deliberately synchronous. It uses no display interrupts,
timers, PWM units, or DMA. The CPU uploads a generated test image and then
spends all its time bit-banging GCLK and row addresses.

## Assumed panel

Defaults in `Core/Inc/sm16380sc.h`:

* SM16380SC, original seven-register protocol;
* 64 electrical pixels per serial lane;
* four daisy-chained 16-output chips per RGB lane;
* 32 scan rows (1/32 scan);
* six parallel HUB75 RGB lanes.

Change `SM16380SC_SCAN_ROWS` and `SM16380SC_CHIPS_PER_LANE` if the panel differs.

## NUCLEO-F446RE wiring

| Display Pins | STM32 Pins | Board Pins |
|---|---|---|
| CLK / DCLK | PC4 | CN10 pin 34 |
| LAT / LE | PC5 | CN10 pin 6 |
| R1 | PC6 | CN10 pin 4 |
| G1 | PC7 | CN10 pin 19; Arduino D9 |
| B1 | PC8 | CN10 pin 2 |
| R2 | PC9 | CN10 pin 1 |
| G2 | PC10 | CN7 pin 1 |
| B2 | PC11 | CN7 pin 2 |
| OE / GCLK | PA8 | CN10 pin 23; Arduino D7 |
| A | PB4 | CN10 pin 27; Arduino D5 |
| B | PB5 | CN10 pin 29; Arduino D4 |
| C | PB6 | CN10 pin 17; Arduino D10 |
| D | PB7 | CN7 pin 21 |
| E | PB8 | CN10 pin 3; Arduino D15 |
| GND | GND | CN7 pin 19 or 20; CN10 pin 9 or 20 |

Connector numbering follows ST's NUCLEO-F446RE Morpho connector drawing. View
the board with the component side facing up and confirm the connector pin-1
marking before wiring; odd and even pin columns run on opposite sides of each
Morpho header.

Connect controller ground to panel ground. Power the panel separately. If the
SM16380SC is powered at 5 V, place a 74AHCT245-class buffer between the 3.3 V MCU
and panel because the published guaranteed input-high threshold is 0.7 VDD.

This is the same GPIO assignment used by the downloaded NUCLEO-F446RE SM16380
reference project. The Nucleo silkscreen/header position should still be checked
against the UM1724 board pinout before wiring.

## Test image

The uploaded electrical test pattern contains four 16-output vertical bars:
red, green, blue, and yellow. The second HUB75 RGB triplet is dimmer. A white
diagonal is added to reveal row and output order.

If 16-pixel blocks are horizontally reversed, reverse the chip index in
`SM16380SC_UploadTestImage()`. If pixels inside every block are reversed, change
the OUT mapping in `test_pixel()`.

## Build

Use the generated CMake preset/toolchain after installing Arm GNU Toolchain:

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

The exact preset names can be listed with `cmake --list-presets`.
