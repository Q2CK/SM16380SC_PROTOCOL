# SM16380SC RGB LED Driver

Hello, this repository contains working code and documentation for LED matrix screens using the Chinese `SM16380SC` chip.

## Overview

The SM16380 is a constant-current RGB LED driver IC commonly found in "HUB75"-based LED display panels. This repository provides:

- **Protocol specifications** for both SM16380/SC and SM16380SH variants
- **Reference implementations** for STM32 and RP2040 platforms
- **Bring-up guides** and troubleshooting documentation
- **Working examples** to get your panel running quickly

## This repo

### Documentation

- **[SM16380_PROTOCOL.md](SM16380_PROTOCOL.md)** — Complete technical reference
- **[SM16380SC_PROTOCOL_BEGINNER_GUIDE.md](SM16380SC_PROTOCOL_BEGINNER_GUIDE.md)** — Easy introduction
- **[SM16380SC_BRINGUP.md](SM16380SC_BRINGUP.md)** — Step-by-step bring-up
- **[SM16380SC_MINIMAL_EXAMPLE.md](SM16380SC_MINIMAL_EXAMPLE.md)** — Simple working code

### STM32F446RE

The included Nucleo project is a good starting point. It demonstrates:

- Configuration register setup
- Grayscale data upload
- Row scanning with timer-based GCLK

## Display wiring

### Display signals to NUCLEO-F446RE

| Display signal  | STM32 pin      | Nucleo board connector         |
| --------------- | -------------- | ------------------------------ |
| DCLK / CLK      | PC4            | CN10 pin 34                    |
| LE / LAT        | PC5            | CN10 pin 6                     |
| R1              | PC6            | CN10 pin 4                     |
| G1              | PC7            | CN10 pin 19; Arduino D9        |
| B1              | PC8            | CN10 pin 2                     |
| R2              | PC9            | CN10 pin 1                     |
| G2              | PC10           | CN7 pin 1                      |
| B2              | PC11           | CN7 pin 2                      |
| GCLK / HUB75 OE | PA8 / TIM1_CH1 | CN10 pin 23; Arduino D7        |
| A               | PB4            | CN10 pin 27; Arduino D5        |
| B               | PB5            | CN10 pin 29; Arduino D4        |
| C               | PB6            | CN10 pin 17; Arduino D10       |
| D               | PB7            | CN7 pin 21                     |
| E               | PB8            | CN10 pin 3; Arduino D15        |
| GND             | GND            | Common controller/panel ground |

On this SM16380SC panel, the wire conventionally named `OE` carries **GCLK**.
It is not used as the ordinary active-low HUB75 output-enable signal.

### Typical HUB75 2x8 DATA IN socket

The following view is looking directly into the panel's **DATA IN socket**:

```text
          odd column     even column
        +-------------+
Pin  1  | R1       G1 |  Pin  2
Pin  3  | B1      GND |  Pin  4
Pin  5  | R2       G2 |  Pin  6
Pin  7  | B2        E |  Pin  8
Pin  9  | A         B |  Pin 10
Pin 11  | C         D |  Pin 12
Pin 13  | CLK     LAT |  Pin 14
Pin 15  | OE      GND |  Pin 16
        +-------------+
```

For this display:

- `CLK` is SM16380SC `DCLK`.
- `LAT` is SM16380SC `LE`.
- `OE` is SM16380SC `GCLK`.

> **Important:** verify the pin-1 marking on the actual PCB before connecting
> power or signals. Connector diagrams are often drawn from the cable side
> instead of the socket side, which mirrors the pinout. Connect the Nucleo and
> panel grounds, and power the LED panels from an appropriately rated external
> supply rather than through the Nucleo board.

### For RP2040

Use a PIO-based approach for efficient real-time I/O:

- Hardware-driven CLK
- DMA-fed parallel data lines
- ISR only at frame boundaries

## References

- **DMD_STM32**: [GitHub Link](https://github.com/board707/DMD_STM32)
- **Raspberry Pi RGB Matrix**: [GitHub Link](https://github.com/hzeller/rpi-rgb-led-matrix)

## License

This repository compiles reverse-engineered specifications and open-source references. Check individual files for attribution.
