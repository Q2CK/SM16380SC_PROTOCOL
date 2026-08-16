# SM16380SC first-light firmware

Grayscale DCLK/LE/RGB upload is bit-banged, while TIM1_CH1 generates GCLK on
PA8. TIM1 runs continuously at 5 MHz. Its advanced-timer repetition counter is
130, so one update interrupt occurs after every 131 GCLK periods; that handler
advances the 16-state row address on PB4..PB8.

## Assumed panel

Topology configured in `Core/Inc/sm16380sc.h`:

* SM16380SC, original seven-register protocol;
* two daisy-chained 64x32 modules, producing a 128x32 display;
* each module has 24 chips: eight red, eight green and eight blue;
* the six HUB75 RGB lanes divide those into four chips per lane per module;
* eight daisy-chained 16-output chips per RGB lane across both modules;
* 16 scan addresses (1/16 scan), with two physical rows active together;
* six parallel HUB75 RGB lanes.

This produces `16 outputs * 8 chips = 128` horizontal pixels per lane. R1/G1/B1
carry one physical row and R2/G2/B2 carry the second simultaneously active row.

## FMPWM and GCLK count

The first-light profile uses FMPWM mode 3. FMPWM occupies CFG1 bits 14:13 and
reduces the number of external GCLK pulses required per row:

```c
#define SM16380SC_FMPWM_MODE 3u
#define SM16380SC_GCLK_PER_ROW \
    ((1024u >> SM16380SC_FMPWM_MODE) + 3u)
```

| FMPWM | Required GCLK pulses per row |
|---:|---:|
| 0 | 1027 |
| 1 | 515 |
| 2 | 259 |
| 3 | 131 |

The `1024` and extra three clocks come from the working F446RE reference driver.
The public datasheet advertises scan-frequency multiplication but does not expose
the internal formula or value-to-multiplier encoding. Do not change FMPWM without
also changing the GCLK count: CFG1, the internal PWM/SRAM row progression, and
the STM32's A..E transition interval must agree.

For this 1/16-scan topology, approximate refresh is:

```text
refresh_hz = actual_gclk_hz / (SM16380SC_GCLK_PER_ROW * 16)
```

Mode 3 with 131 pulses is the strongest initial setting because it is used by
the known-working reference. A vendor-controller capture can confirm it by
counting GCLK rising edges between consecutive A..E address changes. The fuller
derivation and uncertainty notes are in `../SM16380_PROTOCOL.md`.

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

## Verified serial/upload behavior

GCLK must remain active during configuration and grayscale upload on the tested
panels. Holding it low left the grayscale SRAM at its random cold-start state.
TIM1 therefore starts before configuration and remains active during all serial
DCLK traffic. After the complete payload the driver sends a separate three-DCLK
VSYNC command and rewrites the seven configuration registers, matching the
known reference sequence.

TIM1 uses `PSC=0`, `ARR=35`, `CCR1=18`, and `RCR=130`. With the 180 MHz APB2
timer clock this gives 5 MHz GCLK, approximately 50% duty, one row update at
38.17 kHz, and a complete 16-row scan at approximately 2.39 kHz. PA8 is owned
by TIM1 alternate function; application code must never write its GPIO BSRR.

This continuous-PWM version changes the address from the update ISR without
stopping GCLK. Validate the boundary on a logic analyser. If the first clocks of
each row occur before the address settles, switch to the documented one-pulse
burst design, which stops GCLK low during the address transition.

The verified payload nesting is `[row][OUT0..15][logical chip 0..7][bit15..0]`.
LE is high only on the last bit of chip 7 for each OUT group. Reversing the chip
index produced scrambled 16-pixel rectangles; increasing order produced the
correct image.

## Test image

The active image is a full-screen horizontal rainbow, identical on all 32
physical rows, running red-yellow-green-cyan-blue-magenta-red across 128 pixels.
It contains no intentional white border or horizontal marker lines.

The rainbow and grayscale upload are verified. A brief line flicker at roughly
two-second intervals remains under investigation. It occurs in every tested
FMPWM mode and remained after disabling interrupts, so it must not be documented
as an FMPWM or SysTick diagnosis. Current candidates are GCLK/address boundary
timing, signal integrity, or power/ground behavior. The production timer-based
scanner must be validated with a logic analyser rather than assuming it fixed.

## Build

Use the generated CMake preset/toolchain after installing Arm GNU Toolchain:

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

The exact preset names can be listed with `cmake --list-presets`.
