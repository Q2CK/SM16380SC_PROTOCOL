# SM16380SC grayscale-data upload manual

Scope: writing pixel grayscale data after the SM16380SC has already been
configured. This document deliberately does not discuss configuration-register
contents.

The description is reconstructed from the working STM32 implementation and the
public SM16380SC V1.1 datasheet. The public datasheet confirms 16 outputs,
16-bit grayscale, 1–64 scan, 32-Kbit SRAM, rising-edge DCLK sampling, and
LE-qualified commands, but omits the complete grayscale command specification.

## 1. The essential model

An SM16380SC does not receive one pixel every display refresh like a conventional
HUB75 shift-register driver. Instead:

1. The host serially uploads a complete set of 16-bit grayscale words into each
   driver's internal SRAM.
2. A frame/VSync command makes the newly uploaded data displayable.
3. The host continuously provides GCLK and external row selection.
4. The chip reads its SRAM and generates PWM for OUT0 through OUT15 internally.

For one configured scan address, one chip needs exactly 16 grayscale words:

```text
OUT0  = 16 bits
OUT1  = 16 bits
...
OUT15 = 16 bits
----------------
total = 256 bits per scan row per chip
```

With `R` configured scan rows, one visible frame per chip is:

```text
frame_bits_per_chip = R * 16 outputs * 16 grayscale bits
                    = R * 256 bits
                    = R * 32 bytes
```

Examples:

| Scan mode | Data per chip per visible frame |
| --------: | ------------------------------: |
|       1/8 |          2,048 bits = 256 bytes |
|      1/16 |          4,096 bits = 512 bytes |
|      1/32 |        8,192 bits = 1,024 bytes |
|      1/40 |       10,240 bits = 1,280 bytes |
|      1/52 |       13,312 bits = 1,664 bytes |
|      1/64 |       16,384 bits = 2,048 bytes |

## 2. What the advertised 32-Kbit SRAM means

At the largest supported scan count:

```text
64 rows * 16 outputs * 16 bits = 16,384 bits = 16 Kbit
```

That is exactly half of the advertised 32-Kbit SRAM:

```text
32 Kbit / 16 Kbit per maximum-size frame = 2 maximum-size frame payloads
```

The working driver uploads a complete new grayscale frame while GCLK continues
to display/scan the preceding frame, then sends a 3-clock LE-qualified VSYNC.
The most consistent interpretation is two 16-Kbit frame/staging banks: one is
read by the PWM engine while the other receives DCLK data, followed by a
VSYNC-controlled swap or commit.

This two-bank organization is a **strong inference**, not an explicitly
published memory diagram. The public datasheet says only that the 32-Kbit SRAM
can store a complete frame for 1–64 scan. It does not name the banks or define
their swap logic.

Do not assume that configuring 1/32 scan provides four independently selectable
frames. The likely physical organization remains two fixed maximum-depth banks,
with only the configured number of rows used in each bank.

The SRAM is per monochrome sink chip. An RGB pixel is stored across three
separate chips or three parallel serial chains:

```text
red sink chip   stores the 16-bit red component
green sink chip stores the 16-bit green component
blue sink chip  stores the 16-bit blue component
```

On a normal HUB75 interface, R1/G1/B1/R2/G2/B2 shift in parallel. Consequently,
six SRAM streams are written in the same number of DCLK cycles as one stream.

## 3. Daisy-chain model

For each colour/data lane:

```text
controller SDI -> chip 0 -> chip 1 -> ... -> chip N-1
                                 each SDO feeds the next SDI
```

Every DCLK rising edge moves one bit one position deeper into the chain. To
load one grayscale word for the same output of all `N` chips, transmit `N`
16-bit words, or `16*N` DCLK edges.

The abstract shift-register rule says the earliest word propagates farthest.
However, the word indices used by the proven reference and by our verified
128x32 panel mapper are serialized in increasing logical-X order:

```text
transmit logical chip 0 first
transmit logical chip 1 second
...
transmit logical chip N-1 last
latch the group
```

This naming already incorporates the panel cascade direction. Reversing it in
our firmware produced scrambled 16-pixel colour rectangles; increasing order
produced a perfect 128-pixel rainbow. Other PCBs may require a different
logical-X mapper, but do not change the protocol loop merely from the abstract
"first word travels farthest" rule.

## 4. Exact grayscale serialization order

The working reference implementation uses this nesting:

```text
for scan_row = 0 .. R-1:
    for output = OUT0 .. OUT15:
        for every chip in the daisy chain:
            for grayscale_bit = bit15 .. bit0:
                put that bit on SDI
                pulse DCLK
        latch this output group
commit/swap the completed frame with VSYNC
```

In words:

1. Row is the outermost dimension.
2. Within a row, OUT0 is sent first and OUT15 last.
3. Within an output, one complete 16-bit grayscale word is sent for every chip
   in that serial lane.
4. Each word is sent most-significant bit first.
5. LE is asserted only on the last DCLK of the final chip word for that output
   group; this is a one-clock grayscale-group latch. It is not the three-clock
   VSYNC command.

For two chips and one row, the wire stream is:

```text
OUT0:  chip0[15:0], chip1[15:0], LE high on the final bit
OUT1:  chip0[15:0], chip1[15:0], LE high on the final bit
...
OUT15: chip0[15:0], chip1[15:0], LE high on the final bit
```

Then the same 16 output groups are sent for scan row 1, row 2, and so on.

The SRAM row address is implicit in upload order. The reference uploader does
not set A..E to select an SRAM write row. A..E belong to the external display
scan engine and may continue changing while SRAM is uploaded.

## 5. DCLK, SDI and LE timing

The SM16380SC datasheet states that SDI is shifted on the DCLK rising edge.
Therefore:

```text
set SDI bit
set LE state
wait setup time
DCLK rising edge
wait hold/high time
DCLK falling edge
```

For ordinary bits, LE is low. For the last bit of the last chip in one OUTn
group, LE is high across the DCLK rising edge:

```text
                         final bit
SDI  -------------------< 0 or 1 >----------------
LE   ______________________/‾‾‾‾‾\________________
DCLK __/‾\__/‾\__/‾\__/‾\__/‾‾\___________________
                              ^ sampled and latched
```

Take LE low after the edge. Do not leave it high into the next group because
the number of LE-qualified clocks encodes commands.

## 6. Minimal single-lane implementation

```c
typedef struct {
    uint16_t value[64][16];  // [scan row][OUT number]
} chip_frame_t;

static void shift_one_bit(bool bit, bool le)
{
    gpio_sdi(bit);
    gpio_le(le);
    gpio_dclk(false);
    delay_setup();
    gpio_dclk(true);          // sampled here
    delay_hold();
    gpio_dclk(false);
}

static void shift_gray16(uint16_t gray, bool latch_on_last_bit)
{
    for (int bit = 15; bit >= 0; --bit) {
        bool last_bit = bit == 0;
        shift_one_bit((gray & (1u << bit)) != 0,
                      latch_on_last_bit && last_bit);
    }
    gpio_le(false);
}

static void upload_lane(const chip_frame_t frame[],
                        unsigned chip_count,
                        unsigned scan_rows)
{
    for (unsigned row = 0; row < scan_rows; ++row) {
        for (unsigned out = 0; out < 16; ++out) {
            for (unsigned chip = 0; chip < chip_count; ++chip) {
                bool final_word = chip == chip_count - 1u;
                shift_gray16(frame[chip].value[row][out], final_word);
            }
        }
    }
}
```

This sends one data lane. A real RGB panel should write all physical RGB lanes
on every edge rather than call it separately six times.

## 7. Six-lane HUB75 implementation

Represent each DCLK bit as one packed GPIO value:

```c
enum {
    BUS_R1 = 1u << 0,
    BUS_G1 = 1u << 1,
    BUS_B1 = 1u << 2,
    BUS_R2 = 1u << 3,
    BUS_G2 = 1u << 4,
    BUS_B2 = 1u << 5,
};

typedef struct {
    uint16_t r1, g1, b1;
    uint16_t r2, g2, b2;
} hub75_gray6_t;

static uint32_t pack_bit(const hub75_gray6_t *p, unsigned bit)
{
    uint16_t m = (uint16_t)(1u << bit);
    return ((p->r1 & m) ? BUS_R1 : 0)
         | ((p->g1 & m) ? BUS_G1 : 0)
         | ((p->b1 & m) ? BUS_B1 : 0)
         | ((p->r2 & m) ? BUS_R2 : 0)
         | ((p->g2 & m) ? BUS_G2 : 0)
         | ((p->b2 & m) ? BUS_B2 : 0);
}

static void shift_gray6(const hub75_gray6_t *p, bool latch_word)
{
    for (int bit = 15; bit >= 0; --bit) {
        gpio_write_rgb6(pack_bit(p, (unsigned)bit));
        gpio_le(latch_word && bit == 0);
        dclk_pulse();
    }
    gpio_le(false);
}
```

The complete panel uploader is then:

```c
// storage[row][electrical chip][OUT number]
static hub75_gray6_t storage[MAX_SCAN_ROWS][MAX_CHIPS][16];

static void upload_panel(unsigned scan_rows, unsigned chips_per_lane)
{
    for (unsigned row = 0; row < scan_rows; ++row) {
        for (unsigned out = 0; out < 16; ++out) {
            for (unsigned chip = 0; chip < chips_per_lane; ++chip) {
                bool final_chip = chip == chips_per_lane - 1u;
                shift_gray6(&storage[row][chip][out], final_chip);
            }
        }
    }

    send_le_qualified_command(3);  // VSYNC / frame commit
}
```

The 3-clock VSYNC is shown where the working reference performs its frame
boundary operation. It is a separate command after the last grayscale latch:

```text
1. Shift the final grayscale bit with LE high for that one DCLK edge.
2. Take LE low. DCLK is low between operations.
3. Optionally wait a conservative bus guard interval.
4. Keep RGB/SDI at zero and take LE high.
5. Generate three new, complete DCLK pulses while LE remains high.
6. Take LE low. VSYNC is now complete.
```

Do **not** start VSYNC three clocks before the grayscale payload ends. Its three
DCLK edges are additional command clocks; none carries grayscale payload.

Keep GCLK active while sending configuration, grayscale payload, and commands.
This is now verified on the physical SM16380SC panels: with GCLK held low during
upload, the display continued to show different power-up SRAM noise after every
cold start; adding a bit-banged GCLK pulse alongside each serial DCLK made the
uniform-red upload work. A production backend should generate GCLK continuously
with a timer. The current first-light firmware interleaves one software GCLK
pulse after every DCLK pulse because it intentionally uses no timer.

### End-of-frame edge timeline

Suppose `D0` is the least-significant bit of the final grayscale word. The
correct end of the upload is:

```text
operation             SDI/RGB       LE       DCLK rising edges
---------------------------------------------------------------
previous gray bit     D1            0        gray edge -1
final gray/latch bit  D0            1        gray edge  0
inter-command gap     0             0        none
VSYNC clock 1         0             1        command edge 1
VSYNC clock 2         0             1        command edge 2
VSYNC clock 3         0             1        command edge 3
idle                  0             0        none
```

Visually:

```text
                  final grayscale latch       separate VSYNC
LE    ____________/‾\________________________/‾‾‾‾‾‾‾‾‾\____
DCLK  ___/‾\___/‾\____                  ___/‾\_/‾\_/‾\______
                 ^ D0                      ^   ^   ^
SDI   ... D1 ... D0 ______________________ 0___0___0_________
```

The low interval between the one-edge latch and VSYNC may be short, but it must
exist as a distinct LE-low state. Use a full idle DCLK period during initial
bring-up unless a captured controller waveform demonstrates a tighter valid
transition.

## 8. Worked chain example

Assume:

```text
scan count       = 16
chips per lane   = 8
parallel lanes   = R1/G1/B1/R2/G2/B2
```

For row 0 and OUT0, prepare four `hub75_gray6_t` words. Send them in electrical
logical chip order 0 through 7. Each takes 16 clocks. LE is low for the first
127 clocks and high only on clock 128.

Repeat this for OUT1 through OUT15, then repeat all outputs for row 1 through
row 15.

Clock count:

```text
per OUT group = 8 chips * 16 bits        = 128 clocks
per row       = 16 outputs * 128 clocks  = 2,048 clocks
per frame     = 16 rows * 2,048 clocks   = 32,768 clocks
```

At 15 MHz DCLK:

```text
upload time = 32,768 / 15,000,000 = 2.185 ms
maximum theoretical upload rate   = 457.8 frames/s
```

All six data lanes are parallel, so multiplying by six is incorrect for wire
time. The panel receives six bits on every DCLK edge.

The memory payload is:

```text
per chip       = 16 rows * 16 OUT * 16 bits = 4,096 bits
eight-chip lane = 8 * 4,096                 = 32,768 bits
six lanes       = 6 * 32,768                = 196,608 bits
```

Each physical chip stores only its own 4,096-bit portion. Daisy chaining changes
transfer length, not the SRAM size of an individual chip.

## 9. Mapping pixels to chip/output/row

The protocol accepts this electrical address:

```text
(data lane, scan row, chip position, OUT number)
```

It does not accept `(x,y)` directly. Convert logical coordinates separately:

```c
typedef struct {
    unsigned row;
    unsigned chip;
    unsigned out;
    unsigned lane;
} physical_pixel_t;

physical_pixel_t p = panel_map_xy(x, y);
```

A common but not guaranteed horizontal mapping is:

```text
chip = x / 16
out  = x % 16
```

The actual panel may reverse chip order, reverse OUT0..OUT15, interleave
sections, or use a row-decoder IC that transforms logical rows. Determine the
mapping with single-output test patterns before optimizing the encoder.

## 10. Updating only part of a frame

The known implementation uploads a complete frame in strict sequential order.
There is no confirmed random SRAM address command in the public material.
Therefore the safe update unit is the full configured frame:

```text
R rows * 16 outputs * N chips * 16 DCLK edges
```

Even if one logical pixel changes, rebuild or reuse the packed stream and upload
the entire frame. DMA makes this inexpensive and preserves deterministic SRAM
write-pointer alignment.

If a transfer is interrupted, do not resume from an assumed location. Restart
the full upload at a known VSYNC/frame boundary. Otherwise all following words
may be shifted to the wrong row/output address.

## 11. Production implementation advice

Prepack each frame in exact wire order:

```text
[row][OUT][logical chip 0..N-1][bit 15..0]
```

For every bit position, store the six RGB lane bits and an LE bit in a byte or
GPIO word. DMA that buffer to a GPIO port or programmable-I/O peripheral paced
by a timer. This removes all nesting and bit extraction from the real-time path.

Two useful buffer strategies are:

1. Full packed frame: fastest DMA startup and simplest waveform; uses
   `scan_rows * 16 * chips * 16 * sizeof(bus_word)` controller RAM.
2. Ping-pong OUT/row blocks: encode the next block while DMA transmits the
   current one; much lower controller RAM with slightly more scheduling work.

Always swap application framebuffers and issue panel VSYNC at a row-zero frame
boundary. Do not change configuration, chain length, or scan count halfway
through a grayscale upload.

## 12. Logic-analyser checklist

For configured scan count `R` and chain length `N`, one upload must show:

- exactly `R * 16 * N * 16` grayscale DCLK rising edges;
- one single-clock LE pulse after every `N * 16` DCLK edges;
- exactly `R * 16` grayscale latch pulses;
- MSB-first bit order within every 16-bit word;
- a one-clock LE grayscale latch on the final payload edge, followed by LE low;
- then a separate final 3-clock LE-qualified VSYNC/commit operation using three
  additional DCLK edges;
- GCLK remaining active throughout serial configuration/upload/commit.

If only the last 16-pixel block changes, chain length or per-group word count is
too small. This panel is verified with increasing logical chip order; if another
PCB renders whole blocks reversed, correct its logical-X mapper. If output
positions within every block are reversed, reverse OUT0..OUT15 in the mapper,
not the 16-bit grayscale bit order.
