# SM16380SC minimal protocol examples

This example targets the **original SM16380/SM16380SC seven-register
protocol**, not SM16380SH. It is intentionally hardware-neutral: replace the
five GPIO/timer functions with calls for the target MCU.

The concrete values match the verified panel: two daisy-chained 64x32 modules,
eight SM16380SCs per RGB lane, and 16 scan addresses driving two physical rows
at once.

## 1. Basic wire operations

Data is sampled on the rising DCLK edge. LE is the HUB75 LAT/STB signal. A
command or latch type is selected by the number of final DCLK edges for which LE
is high.

```c
#include <stdbool.h>
#include <stdint.h>

// Implement these for the selected MCU.
static void gpio_rgb(bool r, bool g, bool b);
static void gpio_le(bool high);
static void gpio_dclk(bool high);
static void delay_bus(void);              // conservative setup/hold delay
static void scan_engine_start(void);      // continuous GCLK + A..E row scan
static void scan_engine_stop_at_row0(void);
static void rewrite_all_seven_config_registers(void);

static void dclk_pulse(void)
{
    gpio_dclk(false);
    delay_bus();
    gpio_dclk(true);       // SM16380SC samples SDI here
    delay_bus();
    gpio_dclk(false);
}

// Send a command with zero on every physical RGB data lane.
static void command(unsigned le_high_clocks)
{
    gpio_le(false);
    gpio_rgb(false, false, false);

    for (unsigned i = 0; i < le_high_clocks; ++i) {
        gpio_le(true);
        dclk_pulse();
    }

    gpio_le(false);
    delay_bus();
}
```

Important commands used below:

```c
enum {
    CMD_VSYNC     = 3,
    CMD_WRITE_CFG1 = 4,
    CMD_WRITE_CFG5 = 6,
    CMD_WRITE_CFG2 = 8,
    CMD_WRITE_CFG7 = 11,
    CMD_WRITE_CFG4 = 12,
    CMD_PREACTIVE  = 14,
    CMD_WRITE_CFG6 = 15,
    CMD_WRITE_CFG3 = 16,
};
```

## 2. Shift a 16-bit value into R, G and B simultaneously

The three colour registers may contain different values. On a HUB75 panel the
same operation normally drives six lanes (`R1/G1/B1/R2/G2/B2`) simultaneously;
extend `gpio_rgb()` to a six-bit GPIO word when implementing a real panel.

```c
static void shift_rgb16(uint16_t red, uint16_t green, uint16_t blue,
                        unsigned final_le_clocks)
{
    for (int bit = 15; bit >= 0; --bit) {
        const uint16_t mask = (uint16_t)(1u << bit);
        gpio_rgb((red   & mask) != 0,
                 (green & mask) != 0,
                 (blue  & mask) != 0);

        // LE is high only during the final N clocks of this packet.
        gpio_le((unsigned)bit < final_le_clocks);
        dclk_pulse();
    }

    gpio_le(false);
}
```

For a daisy chain, the LE tail belongs only to the final 16-bit word:

```c
static void broadcast_config(uint16_t red, uint16_t green, uint16_t blue,
                             unsigned chips_per_lane,
                             unsigned register_selector)
{
    for (unsigned chip = 0; chip < chips_per_lane; ++chip) {
        const bool final_chip = chip == chips_per_lane - 1;
        shift_rgb16(red, green, blue,
                    final_chip ? register_selector : 0);
    }
}
```

## 3. Build and send the configuration

CFG1 contains current gain, scan count, and scan-frequency multiplication:

```c
static uint16_t make_cfg1(unsigned current_gain,
                          unsigned scan_rows,
                          unsigned fm_pwm)
{
    return (uint16_t)(0x8000u
        | (current_gain & 0x3fu)
        | (((scan_rows - 1u) & 0x3fu) << 7)
        | ((fm_pwm & 0x03u) << 13));
}
```

For 1/16 scan, FM-PWM 3, and a deliberately low gain of 8, CFG1 is `0xe788`:

```text
0x8000 | (15 << 7) | (3 << 13) | 8 = 0xe788
```

The reference project uses gain 63 (`0xefbf`). Do not begin bring-up at that
maximum value.

```c
static void write_one_config(unsigned selector,
                             uint16_t red, uint16_t green, uint16_t blue,
                             unsigned chips_per_lane)
{
    command(CMD_PREACTIVE);               // 14 LE-qualified clocks
    broadcast_config(red, green, blue,
                     chips_per_lane, selector);
}

static void sm16380sc_configure(unsigned chips_per_lane,
                                unsigned scan_rows)
{
    const uint16_t cfg1 = make_cfg1(8, scan_rows, 3);

    // Known values from the working STM32 reference implementation.
    const uint16_t cfg2 = 0x0001;
    const uint16_t cfg4 = 0x0000;
    const uint16_t cfg5 = 0x0000;
    const uint16_t cfg7 = 0x0000;

    scan_engine_stop_at_row0();

    command(CMD_VSYNC);                   // 3 LE-qualified clocks

    // The working reference starts GCLK/row scanning before it writes the
    // seven configuration registers. Preserve that ordering for bring-up.
    scan_engine_start();

    // The reference uses slightly different CFG3/CFG6 values per colour.
    write_one_config(CMD_WRITE_CFG1, cfg1,   cfg1,   cfg1,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG2, cfg2,   cfg2,   cfg2,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG3, 0x10a3, 0x1463, 0x1063,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG4, cfg4,   cfg4,   cfg4,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG5, cfg5,   cfg5,   cfg5,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG6, 0x0005, 0x0019, 0x002e,
                     chips_per_lane);
    write_one_config(CMD_WRITE_CFG7, cfg7,   cfg7,   cfg7,
                     chips_per_lane);
}
```

The exact initialization waveform is therefore:

```text
LE×3                                              VSYNC
[start continuous GCLK and row scan]
LE×14, [CFG1 repeated across chain, final LE×4]   CFG1
LE×14, [CFG2 repeated across chain, final LE×8]   CFG2
LE×14, [CFG3 repeated across chain, final LE×16]  CFG3
LE×14, [CFG4 repeated across chain, final LE×12]  CFG4
LE×14, [CFG5 repeated across chain, final LE×6]   CFG5
LE×14, [CFG6 repeated across chain, final LE×15]  CFG6
LE×14, [CFG7 repeated across chain, final LE×11]  CFG7
```

`LE×N` means LE is high during exactly N rising DCLK edges.

## 4. Upload a simple image

Each chip contains 16 outputs and each output receives a 16-bit grayscale value.
For every scan row, send output 0 for every chip in the chain, then output 1 for
every chip, through output 15. The final word of each 16-output group is latched
with a one-clock LE tail in the known reference implementation.

This demonstration stores one RGB value for every scan row, chip, and sink
output:

```c
#define SCAN_ROWS       16
#define CHIPS_PER_LANE   8

typedef struct {
    uint16_t r, g, b;
} rgb16_t;

static rgb16_t image[SCAN_ROWS][CHIPS_PER_LANE][16];

static void clear_image(void)
{
    for (unsigned row = 0; row < SCAN_ROWS; ++row)
        for (unsigned chip = 0; chip < CHIPS_PER_LANE; ++chip)
            for (unsigned output = 0; output < 16; ++output)
                image[row][chip][output] = (rgb16_t){0, 0, 0};
}

static void set_sink_pixel(unsigned row, unsigned chip, unsigned output,
                           uint16_t r, uint16_t g, uint16_t b)
{
    image[row][chip][output] = (rgb16_t){r, g, b};
}

static void upload_image(void)
{
    // Required on the tested hardware: keep GCLK running throughout upload.
    // Holding GCLK low left the display showing random cold-start SRAM.

    for (unsigned row = 0; row < SCAN_ROWS; ++row) {
        for (unsigned output = 0; output < 16; ++output) {
            for (unsigned chip = 0; chip < CHIPS_PER_LANE; ++chip) {
                const rgb16_t p = image[row][chip][output];
                const bool final_chip = chip == CHIPS_PER_LANE - 1;

                shift_rgb16(p.r, p.g, p.b,
                            final_chip ? 1 : 0);
            }
        }
    }

    // Select/swap the freshly uploaded frame.
    command(CMD_VSYNC);

    // The verified first-light firmware now repeats all seven configuration
    // writes here, matching the known-working reference's post-upload path.
    // Call the same register-write helper used by sm16380sc_configure(), while
    // leaving GCLK active.
    rewrite_all_seven_config_registers();
}
```

Display one dim red sink output:

```c
int main(void)
{
    hardware_init();

    clear_image();
    sm16380sc_configure(CHIPS_PER_LANE, SCAN_ROWS);

    // Logical scan row 0, first chip, sink OUT0, dim red.
    set_sink_pixel(0, 0, 0, 0x0800, 0x0000, 0x0000);
    upload_image();

    for (;;) {
        // The hardware timer/DMA scan engine must keep generating GCLK and
        // changing A..E. The foreground loop need not bit-bang the display.
    }
}
```

To display white on that sink, use the same value on all channels:

```c
set_sink_pixel(0, 0, 0, 0x0800, 0x0800, 0x0800);
```

To generate a grayscale ramp across the first chip:

```c
for (unsigned output = 0; output < 16; ++output) {
    uint16_t level = (uint16_t)(output * 0x0800u);
    set_sink_pixel(0, 0, output, level, level, level);
}
upload_image();
```

## 5. GCLK and row scan engine

Uploading SRAM is only half of the protocol. The display remains dark without a
continuous GCLK and row-address sequence.

For the reference `fm_pwm = 3` setup:

```c
enum { GCLK_PULSES_PER_ROW = (16 * 64 >> 3) + 3 }; // 131
```

Conceptually:

```c
static void scan_forever_conceptual(void)
{
    for (;;) {
        for (unsigned row = 0; row < SCAN_ROWS; ++row) {
            set_hub75_address(row);  // update A..E together
            delay_address_settle();

            for (unsigned i = 0; i < GCLK_PULSES_PER_ROW; ++i)
                pulse_gclk();
        }
    }
}
```

Do not use that CPU loop in production. Configure a timer for GCLK and use a
timer repetition/update interrupt, DMA, or programmable I/O state machine to
change the row after 131 pulses. The reference uses 10 MHz GCLK. Begin slower
while validating the waveform.

## 6. Mapping this to actual XY pixels

`set_sink_pixel(row, chip, output, ...)` addresses the electrical storage order,
not necessarily screen coordinate `(x,y)`. PCB wiring can reverse chip order,
permute 16-output blocks, and map the HUB75 upper/lower lanes to different row
banks.

First display these tests and record where they appear:

```c
set_sink_pixel(0, 0,  0, 0x0800, 0, 0);
set_sink_pixel(0, 0, 15, 0, 0x0800, 0);
set_sink_pixel(1, 0,  0, 0, 0, 0x0800);
```

Then implement a separate mapper:

```c
void set_xy(unsigned x, unsigned y, rgb16_t color)
{
    physical_location_t p = panel_map_xy(x, y);
    image[p.scan_row][p.chip][p.output] = color;
}
```

Keeping mapping separate prevents a panel-specific X/Y permutation from
contaminating the SM16380SC protocol driver.

## 7. Practical cautions

* At VDD = 5 V, use a 74AHCT-family logic buffer; the documented VIH threshold
  is 3.5 V, above a guaranteed 3.3 V MCU high level.
* Configure and upload black before enabling a high current setting.
* Verify `CHIPS_PER_LANE` from the PCB or a logic capture.
* Real HUB75 panels require all six RGB lanes, not only the three-lane shorthand
  shown in the low-level example.
* If the chip ignores this initialization, capture LE/DCLK from its original
  controller. Confirm the selector sequence before trying SM16380SH commands.
