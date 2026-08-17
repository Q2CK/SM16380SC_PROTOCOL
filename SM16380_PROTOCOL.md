# SM16380 / SM16380SH reverse-engineering notes and implementation plan

Status: source-derived working specification, 2026-08-15. This is not an
official datasheet. Statements marked **confirmed** are implemented by at least
one working open-source driver. Statements marked **inferred** should be checked
with a logic analyser on the target panel.

## Executive summary

SM16380 is an SRAM/S-PWM constant-current RGB LED sink driver used behind a
HUB75-like connector. It is not a conventional shift-register panel. The host
uploads a full grayscale image into driver memory and then supplies a continuous
grayscale clock (`GCLK`, normally carried on the HUB75 `OE` wire). The chip
generates per-channel PWM internally. Row selection remains external.

The usable bus is:

| HUB75 name             | Protocol role                                            |
| ---------------------- | -------------------------------------------------------- |
| `R1/G1/B1`, `R2/G2/B2` | parallel serial-data lanes                               |
| `CLK`                  | data clock (`DCLK`) and internal PWM timing clock        |
| `LAT` / `STB`          | latch and in-band command qualifier                      |
| `OE`                   | grayscale clock (`GCLK`) / row-transition pulse          |
| `A..E`                 | binary row address, or panel-specific serial mux control |
| GND                    | mandatory common reference                               |

The original **SM16380/SM16380SC** and **SM16380SH** must be treated as separate protocol
profiles:

- Original SM16380/SM16380SC: seven 16-bit configuration registers; register selection is
  encoded with LAT-high tails of 4, 8, 16, 12, 6, 15 and 11 clocks.
- SM16380SH: address/value configuration words (normally addresses `0x02` through
  `0x1f`) and a longer unlock/pre-activation sequence with guard words.

Do not select the profile from the seller's panel description. Read the marking
on the sink IC. If it says `SM16380SH`, use the SH profile. If it says
`SM16380` or `SM16380SC`, start with the original profile from the Nucleo
project. The public SM16380SC datasheet confirms the architecture but omits the
command/register table, so SC compatibility with the seven-register sequence is
a strong source-based conclusion that still requires a target-panel capture.

## Evidence and source snapshot

The workspace contains:

- `Nucleo-F446RE_test/Core/Src/main.c`: original SM16380 implementation, including
  named bitfields for all seven configuration words. Downloaded from the Drive
  archive linked by the upstream issue.
- `DMD_STM32/DMD_SPWM_Driver.h`: STM32 SM16380SH driver.
- `DMD_STM32/DMD_SPWM_Driver_RP.h`: RP2040 PIO/DMA SM16380SH driver; especially
  useful because bus operations are separated into small functions.
- `DMD_STM32/dmd_spwm.pio` and generated PIO headers: exact RP2040 wire engine.
- `rpi-rgb-led-matrix_pwm_experiment/lib/spwm`: Raspberry Pi experimental SPWM
  engine and a large captured SM16380SH profile catalogue.
- `rpi-rgb-led-matrix`: upstream baseline; issue 1866 is discussion/resources,
  not an SM16380 implementation in the main branch.

Pinned revisions at analysis time:

- DMD_STM32 `642153489df9422e063a6ef18e6578088a3dbd14` (GPL-3.0).
- rpi-rgb-led-matrix_pwm_experiment
  `5da13cb3b0d38d66bb5201f03704b5bef9e4446b` (GPL-2.0).

The Nucleo archive has no obvious top-level licence. Treat it as reference-only
unless permission/provenance is clarified. A clean implementation from this
protocol description is preferable for a permissively licensed product.

## Electrical cautions

Protocol success does not prove electrical safety.

1. Power the panel from a supply sized for its worst-case LED current. Do not
   power it through an MCU board.
2. Join controller and panel grounds before applying logic signals.
3. Confirm logic thresholds. Many panels accept 3.3 V; some require a 74AHCT245
   or equivalent 3.3-to-5 V buffer. A slow bidirectional level shifter is not
   suitable for a multi-MHz clock.
4. Begin with low global-current configuration and a black framebuffer. The
   Nucleo sample defaults CFG1 current gain to maximum (`0x3f`), which is a poor
   first hardware test.
5. Keep ribbon cables short, buffer CLK/LAT/OE, provide ground interleaving, and
   check overshoot at the far end of a chain.

## Wire encoding

### Shift edge and bit order

**Confirmed:** each pixel/configuration unit is 16 clocks and is sent MSB first.
The Nucleo implementation iterates bit 15 down to bit 0. RGB register values are
shifted simultaneously on physical R, G and B lanes. The RP2040 implementation
uses the same `0x8000 >> bit` order.

The source writes data/LAT, delays, then raises CLK. Therefore data and LAT must
be stable before the active/rising CLK edge. Keep them stable through the edge.
Exact minimum setup/hold time is unknown; start at 1 MHz and measure before
increasing speed.

### Commands are LAT-high pulse counts

**Confirmed:** a command is encoded by holding LAT high for a precise count of
CLK rising edges, then taking LAT low. During ordinary data, LAT stays low. A
data packet is committed by making LAT high only for the final `N` clocks.

Generic primitive:

```text
shift_packet(words_on_parallel_rgb_lanes, lat_tail_clocks):
    for every bit, MSB first:
        set RGB lanes
        LAT = 1 only if this bit is among the final lat_tail_clocks
        CLK = 0; wait setup
        CLK = 1; wait high
        CLK = 0; wait hold
    LAT = 0
```

A command-only pulse is the same mechanism with zero on all RGB lanes and LAT
high for all command clocks.

```text
command(n):
    LAT = 0
    repeat n times:
        RGB = 0
        LAT = 1
        pulse CLK
    LAT = 0
```

Useful decoded commands:

| LAT-high clocks | Meaning                                                          | Confidence                                 |
| --------------: | ---------------------------------------------------------------- | ------------------------------------------ |
|               3 | VSYNC / frame synchronisation                                    | confirmed, both families                   |
|              14 | pre-activation/unlock                                            | confirmed sequence; semantic name inferred |
|              12 | SM16380 original CFG4; also OE-side data-group action in SH code | profile-dependent                          |
|               4 | original SM16380 CFG1 write                                      | confirmed                                  |
|               8 | original SM16380 CFG2 write                                      | confirmed                                  |
|              16 | original SM16380 CFG3 write                                      | confirmed                                  |
|               6 | original SM16380 CFG5 write                                      | confirmed                                  |
|              15 | original SM16380 CFG6 write                                      | confirmed                                  |
|              11 | original SM16380 CFG7 write; SH pre-activation stage             | profile-dependent                          |
|               5 | SM16380SH config-word latch tail                                 | confirmed                                  |

Counts cannot be interpreted without a chip profile. For example, 11 selects
CFG7 on original SM16380 but participates in the SH activation sequence.

## Original SM16380 protocol

### Where SM16380SC fits

The chip marked **SM16380SC belongs on this original-SM16380 path, not the
SM16380SH path**. The manufacturer's SM16380SC V1.1 datasheet confirms:

- 16 constant-current outputs and native 16-bit grayscale;
- 32 Kbit internal SRAM holding one complete frame;
- 1 through 64 scan support;
- 2/4/8/16 row-scan frequency multiplication;
- 6-bit, 64-step software current gain;
- separate rising-edge DCLK data shifting, GCLK grayscale timing and LE command
  qualification;
- QSOP24 pinout: SDI 2, DCLK 3, LE 4, outputs 5..20, GCLK 21, SDO 22,
  R-EXT 23 and VDD 24.

Those features correspond exactly to the original profile's CFG1 scan/FM-PWM/
current fields and its seven functional configuration words. They differ
materially from the address/value and guard-word protocol seen in SM16380SH.
No local open-source source names `SM16380SC` explicitly; the Nucleo code calls
the profile simply `SM16380`. Therefore this classification is high confidence,
not a claim of manufacturer-confirmed command-level equivalence.

SC-specific electrical limits from the public datasheet are useful for the
backend:

| Supply | Maximum DCLK | Maximum GCLK | Maximum specified output current |
| ------ | -----------: | -----------: | -------------------------------: |
| 5.0 V  |       30 MHz |       33 MHz |                    20 mA/channel |
| 3.3 V  |       25 MHz |       25 MHz |                    15 mA/channel |

These are IC test limits, not safe initial panel settings. Cable, buffers and PCB
routing will normally set a lower reliable bus ceiling. Continue to start around
1 MHz and use conservative current gain. The datasheet specifies VIH >= 0.7 VDD;
when the chip is powered at 5 V, a 3.3 V MCU high level is below the guaranteed
3.5 V threshold, so a 74AHCT-family buffer is recommended.

### Configuration register format

The Nucleo code defines seven 16-bit words. C bitfields are implementation
defined, but on the STM32/GCC target the first declared field occupies the least
significant bits. The masks below make that assumption explicit.

#### CFG1, selected with a 4-clock LAT tail

| Bits  |     Mask | Meaning                          |
| ----- | -------: | -------------------------------- |
| 5:0   | `0x003f` | current gain, 0..63              |
| 6     | `0x0040` | reserved                         |
| 12:7  | `0x1f80` | scan count minus one, 0..63      |
| 14:13 | `0x6000` | FM-PWM mode, 0..3                |
| 15    | `0x8000` | unknown/reserved; sample sets it |

Construct it without bitfields:

```c
uint16_t cfg1 = 0x8000u
              | (current_gain & 0x3fu)
              | ((scan_rows - 1u) & 0x3fu) << 7
              | (fm_pwm & 0x03u) << 13;
```

The sample uses gain 63, `scan_rows = panel height / multiplex factor`, and
`fm_pwm = 3`.

#### CFG2, selected with an 8-clock LAT tail

| Bits | Meaning                        |
| ---- | ------------------------------ |
| 1:0  | low-grayscale uniformity level |
| 5:2  | ghost time                     |
| 6    | reserved                       |
| 7    | remove-dead-pixel enable       |
| 15:8 | reserved/unknown               |

The sample sends `0x0001` to all colours.

#### CFG3, selected with a 16-clock LAT tail

| Bits  | Meaning               |
| ----- | --------------------- |
| 0     | energy-saving mode    |
| 1     | energy-saving disable |
| 4:2   | reserved              |
| 7:5   | first-scan dark level |
| 9:8   | reserved              |
| 12:10 | ghost level           |
| 15:13 | reserved/unknown      |

The sample uses distinct channel values: R `0x10a3`, G `0x1463`, B `0x1063`.
This is evidence that configuration may be colour-specific, not merely copied to
all six HUB75 data lanes.

#### CFG4, selected with a 12-clock LAT tail

| Bits  | Meaning                          |
| ----- | -------------------------------- |
| 3:0   | reserved                         |
| 6:4   | cross-elimination level          |
| 12:7  | reserved                         |
| 14:13 | low-grayscale compensation level |
| 15    | reserved                         |

Sample: `0x0000` for all colours.

#### CFG5, selected with a 6-clock LAT tail

Unknown 16-bit register. Sample: `0x0000`.

#### CFG6, selected with a 15-clock LAT tail

| Bits  | Meaning              |
| ----- | -------------------- |
| 8:0   | coupling setting     |
| 10:9  | reserved             |
| 11    | ghost-enhance enable |
| 15:12 | reserved             |

Sample: R `0x0005`, G `0x0019`, B `0x002e`.

#### CFG7, selected with an 11-clock LAT tail

Unknown 16-bit register. Sample: `0x0000`.

### Configuration upload sequence

The source performs this sequence:

1. Keep/start GCLK activity. The tested SM16380SC did not accept grayscale SRAM
   upload with GCLK held low.
2. Send VSYNC: 3 CLK pulses with LAT high and RGB zero.
3. Wait a small guard interval.
4. Keep the GCLK/row engine active for configuration and grayscale upload.
5. For each of CFG1..CFG7:
   1. Send pre-activation: 14 CLK pulses with LAT high and RGB zero.
   2. Repeat the 16-bit R/G/B register words across the entire physical driver
      chain, MSB first.
   3. During only the last driver word, hold LAT high for the register's selector
      count.

The Nucleo repeat count is:

```text
send_num = chain_x_pixels * multiplex / (16 * 2)
```

This embeds assumptions about two HUB75 halves and 16 outputs per sink. Prefer
deriving it from panel topology:

```text
driver_words_per_lane = total_serial_pixels_per_lane / 16
```

Verify it experimentally: too small updates only the last drivers; too large can
move the intended latch position beyond the real chain.

### Grayscale data layout

**Confirmed:** each sink consumes 16 grayscale bits for output 0, then 16 bits
for output 1, through output 15. Within each 16-bit value the source sends bit
15 first. Data for all daisy-chained drivers follows for the same output index
before moving to the next output index. LAT is asserted only on the last clock
of the final driver word for each output group in the original sample.

Logical order, for each uploaded row:

```text
for output = 0..15:
    for driver_section = 0..driver_count-1:
        for grayscale_bit = 15..0:
            drive all RGB lanes with that bit
            LAT = last driver_section && grayscale_bit == 0
            pulse CLK
```

The physical X direction and top/bottom lane order can be reversed or interleaved
by PCB routing. That is a mapper concern, not a change to this wire protocol.

### GCLK and row scan

The Nucleo implementation generates GCLK with a 10 MHz, 50% duty timer and
changes row address after a programmed pulse train. Its steady-state pulse count
per row is:

```text
gclk_per_row = (16 * 64) >> fm_pwm + 3
             = 1024 / 2^fm_pwm + 3
```

The intended evaluation order is:

```text
gclk_per_row = ((16 * 64) >> fm_pwm) + 3
```

`fm_pwm` is the two-bit FMPWM field in CFG1 bits 14:13. It selects the chip's
row-scan/PWM frequency-multiplication mode. Each increment halves the number of
external GCLK pulses required for one row interval:

| CFG1 FMPWM | Base calculation | Extra clocks | Total GCLK/row |
|---:|---:|---:|---:|
| 0 | `1024 >> 0 = 1024` | 3 | 1027 |
| 1 | `1024 >> 1 = 512` | 3 | 515 |
| 2 | `1024 >> 2 = 256` | 3 | 259 |
| 3 | `1024 >> 3 = 128` | 3 | 131 |

The public datasheet advertises 2x/4x/8x/16x row-scan multiplication but omits
the encoding table. The working source proves the relative relationship above,
but does not prove which raw value corresponds to each marketing multiplier.
The exact internal origin of the `16 * 64 = 1024` base is also unpublished; it
must be treated as a working protocol constant rather than a decoded hardware
formula.

The final `+3` is named `GCLK_ADD_PULSE_SM16380` by the reference driver. These
appear to be row-transition/internal-state overhead pulses, separate from the
power-of-two PWM period. Their exact semantic purpose is not documented.

FMPWM and the externally generated pulse count form one setting and must always
be changed together. For example, programming FMPWM 2 but changing A..E every
131 pulses can desynchronize the chip's internal SRAM/PWM row position from the
external row multiplexer.

At an external GCLK frequency `f_gclk` and `R` scan addresses, the approximate
complete scan refresh is:

```text
refresh_hz = f_gclk / (gclk_per_row * R)
```

For a 1 MHz GCLK and 16 scan addresses:

| FMPWM | Approximate refresh |
|---:|---:|
| 0 | 60.9 Hz |
| 1 | 121.4 Hz |
| 2 | 241.3 Hz |
| 3 | 477.1 Hz |

These are upper timing estimates that exclude address settling and software
overhead. Start with FMPWM 3 and 131 pulses because that is the known working
F446RE profile. If a vendor controller is available, count GCLK rising edges
between A..E changes and match the observed count to the table. When testing a
different mode, change both the CFG1 field and `gclk_per_row` atomically.

For `fm_pwm = 3`, this is 131 pulses per row (128 PWM clocks plus 3 extra). For
the verified 1/16-scan, 128x32 panel at 10 MHz, the inferred row-cycle refresh
is approximately:

```text
10,000,000 / (131 * 16) = 4771 Hz
```

That number is an upper timing estimate, not guaranteed visual refresh: command
gaps, row switching, frame uploads and the chip's internal interpretation matter.
The first start uses an inconsistent `+ 3*2` expression in the sample (134
pulses), while the ISR uses `+3` thereafter. Treat the extra clocks as a tuning
margin and capture the working vendor controller if exact behaviour matters.

Row address is updated at each timer repetition boundary. For a binary mux,
write A..E together and allow settling time before the next active pulse. Some
panels use a serial row decoder; the sample supports that as a separate mapper.

## SM16380SH protocol delta

The SH implementation programs a conventional address/value set. Each 16-bit
word appears to be `(address << 8) | value`; for example `0x021f` selects 1/32
scan because the low byte is `rows - 1`.

Baseline DMD_STM32 profile:

```text
021f 0300 0400 0500 0600 0750 0800 0900
0a02 0b0c 0c08 0d00 0e05 0f00 1000 1100
1200 1300 1414 1500 1630 1700 1801 1904
1a03 1b14 1c12 1d00 1e00 1f0c
```

Replace only the low six bits of word `0x02xx` with `scan_rows - 1` until the
other registers are understood. The experimental Raspberry Pi fork contains
dozens of captured profiles for different scan ratios and row-driver pairings;
these demonstrate that values beyond register 2 are panel-specific. Do not
blindly combine values from different profiles.

The RP2040 SH configuration transaction is:

```text
VSYNC: LAT high for 3 CLK
8 plain CLK
LAT high for 11 CLK
8 plain CLK
LAT high for 14 CLK
8 plain CLK
initialise row mux; OE high for 12 CLK; 88 plain CLK
start DCLK/GCLK state machine
shift/latch 0x00aa with LAT high for last 5 CLK
shift/latch 0x01aa with LAT high for last 5 CLK
shift/latch register word with LAT high for last 5 CLK
shift/latch 0x0055 with LAT high for last 5 CLK
shift/latch 0x0155 with LAT high for last 5 CLK
8 plain CLK
```

One configuration register is sent per refresh call; initialisation refreshes
enough times to visit every register. Preserve this known-working behaviour in a
first port. It can later be tested as a single batch.

For SH grayscale data, DMD_STM32 positions 4-bit source colour at internal bits
13..10 (`gclk_bits = 13`) and zero-fills the remainder. The architecture supports
moving the nibble downward for dimming. This is a packing convention inferred
from working code, not a complete definition of the chip's native grayscale
format.

## Recommended clean implementation

Separate policy from hard real-time I/O:

```text
Application / drawing API
        |
linear RGB framebuffer (double buffered)
        |
gamma + brightness LUT, topology mapper
        |
protocol encoder (SM16380 or SM16380SH profile)
        |
immutable transfer descriptors / ping-pong row buffers
        |
DMA + timer/PIO/RMT peripheral backend
        |
HUB75 connector and panel
```

Suggested interfaces:

```c
typedef struct {
    uint16_t scan_rows;
    uint16_t pixels_per_lane;
    uint8_t  chain_drivers;
    uint8_t  fm_pwm;
    uint32_t dclk_hz;
    uint32_t gclk_hz;
    bool     serial_row_mux;
} sm_panel_geometry_t;

typedef struct {
    void (*stop_at_frame_boundary)(void *ctx);
    void (*emit_command)(void *ctx, unsigned lat_clocks);
    void (*emit_parallel16)(void *ctx, uint16_t r, uint16_t g,
                            uint16_t b, unsigned repeat,
                            unsigned final_lat_clocks);
    void (*start_scan)(void *ctx);
} sm_bus_ops_t;
```

Keep `sm16380_profile.c` limited to register construction and ordered protocol
operations. Keep GPIO register addresses, PIO programs and DMA channels entirely
inside a platform backend. Keep panel X/Y transformations in a topology mapper.
This makes protocol tests possible on a desktop waveform recorder.

### Fast path

Do not bit-bang in production. Use:

- one timer/PWM peripheral for continuous CLK/GCLK,
- DMA to a GPIO set/reset register, SPI-like parallel shifter, or programmable
  I/O engine for RGB+LAT,
- a second timer compare or small state machine for row-address transitions,
- two aligned row buffers so the CPU encodes row N+1 while DMA transmits row N,
- a short ISR only at row/frame boundaries.

For RP2040, DMD_STM32's PIO/DMA design is the best starting architecture. For
STM32, use timer-triggered DMA rather than the Nucleo sample's per-bit GPIO loop.
On STM32F4/F7/H7, a timer update/compare event can pace DMA writes to GPIO BSRR;
on parts with suitable SPI/SAI/FMC, pack contiguous RGB pins and use the hardware
shifter. Keep CLK hardware-generated to eliminate software jitter.

Precompute a byte/word LUT mapping a 4- or 8-bit channel value into the required
16-position stream. Encode only dirty rows. Avoid floating point in the refresh
loop: generate gamma/current LUTs when settings change. Align buffers to cache
lines; on cached MCUs, clean the exact DMA range before starting it.

### Throughput budget

For each scan row, grayscale upload cost is approximately:

```text
upload_clocks = 16 outputs * 16 bits * drivers_per_lane
upload_time   = upload_clocks / dclk_hz
```

At 128 serial pixels per lane (8 drivers) and 15 MHz DCLK:

```text
2048 clocks / 15 MHz = 136.5 us per uploaded scan row
32 rows              = 4.37 ms = 229 complete uploads/s
```

Six RGB signals travel in parallel, so they do not multiply the clock count.
Configuration traffic is negligible after startup. Real throughput is lower due
to latches, mapping, DMA handoffs and safe GCLK boundaries. Instrument those gaps
with a debug pin and logic analyser.

### Concurrency and tear-free updates

Use a front framebuffer owned by the scan engine and a back framebuffer owned by
the application. Swap pointers only at VSYNC/row zero. Configuration changes
must follow the same rule: request them atomically, stop at row zero, upload all
required registers, clear/upload a known frame, then resume scanning.

Never busy-wait for row zero with interrupts disabled. Let the scan ISR/state
machine acknowledge a stop request. This is both faster for the application and
avoids a deadlock found easily in naive ports.

## Bring-up procedure

### Required capture channels

Capture at least CLK, LAT, OE, A..E and one RGB lane. Prefer all six RGB lanes.
Use a sample rate at least 5-10 times the highest clock frequency.

### Safe staged test

1. Photograph both sides of the panel and record every IC marking, panel model,
   resolution, connector labels and stated scan ratio.
2. With power off, continuity-map HUB75 pins to SM16380/SH pins and identify row
   decoder chips. This determines whether A..E are binary or serialized.
3. If the vendor controller works, capture startup through several frames. Count
   LAT-high clocks for every LAT event and OE pulses between address changes.
4. Start the new controller at 0.5-1 MHz DCLK, conservative GCLK, black pixels,
   and minimum current gain.
5. Send configuration once, then upload a single dim red pixel. Repeat for green
   and blue to confirm lane order.
6. Display vertical stripes at x = 0, 15, 16, 31 and the final column. This
   exposes driver order, 16-output boundaries and X reversal.
7. Display one row at a time to determine scan ratio/address mapping. Do not
   compensate in the protocol engine; record a mapper table.
8. Display grayscale ramps and camera tests. Tune GCLK count/phase and ghosting
   registers only after mapping is correct.
9. Raise DCLK in steps while watching the farthest driver. Back off from the
   first observed bit error; do not assume the library's 15 MHz ceiling applies
   to this cable/panel.

Expected diagnostic signatures:

| Symptom                                        | First suspect                                            |
| ---------------------------------------------- | -------------------------------------------------------- |
| completely dark                                | wrong chip profile, missing continuous GCLK, OE polarity |
| random flash at boot                           | configuration/current before black data, floating pins   |
| only part of width works                       | wrong driver repeat count or chain length                |
| correct image, wrong rows                      | scan count or mux topology                               |
| 16-pixel blocks permuted                       | section/driver ordering                                  |
| stable low brightness, corrupt high brightness | DCLK integrity or GCLK timing                            |
| ghost line at row transition                   | address settling / OE phase / ghost registers            |
| colours swapped                                | lane map, not grayscale algorithm                        |

### Waveform assertions for automated tests

A mock backend or decoded logic capture should assert:

- every data word is exactly 16 CLK edges and MSB first;
- LAT is low except for the final N edges requested;
- VSYNC has exactly 3 LAT-qualified clocks;
- original SM16380 configuration selectors occur in order
  `4,8,16,12,6,15,11` and each is preceded by 14;
- SH configuration is surrounded by `00aa,01aa,...,0055,0155`, each latched with
  a 5-clock tail;
- row addresses change only at defined GCLK boundaries;
- framebuffer pointer changes occur only at row zero.

## Known uncertainties and next measurements

1. No authoritative SM16380 datasheet was available in the supplied sources.
   Names for original register fields come from one reference implementation.
2. Exact CLK/LAT/OE setup, hold, pulse widths and maximum frequencies remain
   unknown. Measure a vendor controller or obtain the datasheet.
3. The semantic role of every SH register is unknown; captured profiles are
   empirical configurations, not a register definition.
4. The Nucleo code's initial versus steady-state GCLK extra-pulse count differs.
   A capture must determine whether this is intentional settling or an artefact.
5. Panel topology is not specified by the sink-chip model. The row decoder,
   physical lane count and PCB routing must be characterized per panel.
6. Global brightness should initially be implemented in the pixel/gamma LUT.
   Register-based current control should be exposed only after its safe range is
   measured.
7. A brief line glitch remains at roughly two-second intervals on the current
   bit-banged scanner. It persists across all four FMPWM modes and with MCU
   interrupts disabled. Do not attribute it to FMPWM or SysTick without new
   evidence; capture GCLK and A..D and check power/signal integrity.

## Practical first implementation choice

If the target controller is STM32F446RE and the chips are marked **SM16380**, the
shortest route to first light is to build and run the downloaded Nucleo project,
after reducing CFG1 current gain and confirming its pin map in `main.h`. Use it
as a waveform oracle, not the production design.

For production, implement the clean interfaces above and a timer+DMA backend.
Start with 4-bit-per-channel source colour because both DMD_STM32 paths already
validate that packing model. Once the panel is stable, add an 8-bit application
framebuffer with gamma LUT conversion to the chip's 16-bit stream. This retains a
simple drawing API while keeping the real-time path deterministic.

If the chips are **SM16380SH**, start from the RP2040 PIO implementation or the
Raspberry Pi experimental fork and choose the captured profile matching both
scan ratio and row-driver IC. Do not apply the original SM16380 seven-register
sequence to an SH panel.

## Upstream links

- https://github.com/board707/DMD_STM32
- https://github.com/hzeller/rpi-rgb-led-matrix/issues/1866
- https://github.com/kingdo9/rpi-rgb-led-matrix_pwm_experiment
- https://drive.google.com/file/d/10cotodkM1iLoY0CljS72_RwHh7dI84Kq/view
