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

| HUB75 | STM32F446RE | GPIO |
|---|---|---|
| CLK/DCLK | PC4 | GPIOC bit 4 |
| LAT/LE | PC5 | GPIOC bit 5 |
| R1 | PC6 | GPIOC bit 6 |
| G1 | PC7 | GPIOC bit 7 |
| B1 | PC8 | GPIOC bit 8 |
| R2 | PC9 | GPIOC bit 9 |
| G2 | PC10 | GPIOC bit 10 |
| B2 | PC11 | GPIOC bit 11 |
| OE/GCLK | PA8 | GPIOA bit 8 |
| A | PB4 | GPIOB bit 4 |
| B | PB5 | GPIOB bit 5 |
| C | PB6 | GPIOB bit 6 |
| D | PB7 | GPIOB bit 7 |
| E | PB8 | GPIOB bit 8 |

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
