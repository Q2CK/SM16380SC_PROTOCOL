# SM16380SC display protocol — beginner-friendly guide

This guide explains how an STM32 talks to an LED display containing SM16380SC
chips. It assumes you understand basic binary numbers and that a GPIO pin can be
set high or low. You do not need to know DMA, interrupts, or advanced electronics.

For exact register details and reverse-engineering evidence, read
`SM16380_PROTOCOL.md`. This document focuses on understanding the big picture.

## 1. What is the SM16380SC?

An LED panel contains lots of LEDs, but an STM32 cannot control every LED using
a separate wire. There would be far too many wires.

Instead, the panel contains SM16380SC driver chips. Each chip controls 16 LED
connections:

```text
SM16380SC
  OUT0  -> LED column/output 0
  OUT1  -> LED column/output 1
  ...
  OUT15 -> LED column/output 15
```

Each output receives a 16-bit brightness number:

```text
0x0000 = completely off
0x0001 = almost off
0x8000 = about half brightness
0xffff = maximum grayscale value
```

The chip stores these numbers in internal memory called SRAM. After the STM32
uploads an image, the SM16380SC produces the fast PWM pulses that control LED
brightness by itself.

This is different from a basic HUB75 panel, where the controller must create all
brightness timing continuously.

## 2. Our display arrangement

We have two 64x32 modules connected in a chain:

```text
STM32 -> panel 1 DATA IN
panel 1 DATA OUT -> panel 2 DATA IN

visible result: 128x32 pixels
```

Each 64x32 module has 24 SM16380SC chips:

```text
8 red chips
8 green chips
8 blue chips
= 24 chips
```

The HUB75 connector has six color-data wires:

```text
R1 G1 B1 -> one physical row group
R2 G2 B2 -> another physical row group
```

Each color has two data wires. Therefore the eight chips of one color on one
module are divided into two chains of four chips:

```text
R1 -> 4 red chips
R2 -> 4 red chips
G1 -> 4 green chips
G2 -> 4 green chips
B1 -> 4 blue chips
B2 -> 4 blue chips
```

After connecting two modules, every data wire passes through eight chips:

```text
4 chips on panel 1 + 4 chips on panel 2 = 8 chips per data lane

8 chips * 16 outputs = 128 horizontal positions
```

## 3. Important wires

| Display wire | What it does                                             |
| ------------ | -------------------------------------------------------- |
| R1/G1/B1     | Sends brightness bits for the first row group            |
| R2/G2/B2     | Sends brightness bits for the second row group           |
| CLK/DCLK     | Tells chips when to read the next data bit               |
| LAT/LE       | Marks the end of a data group and sends special commands |
| OE/GCLK      | Advances the chips' internal PWM and row timing          |
| A/B/C/D/E    | Selects the physical LED row on the panel                |
| GND          | Common electrical reference                              |

All six color wires transfer data at the same time. One DCLK pulse can therefore
send six bits: R1, G1, B1, R2, G2, and B2.

## 4. Sending one bit

The chip reads SDI when DCLK changes from low to high.

```text
1. Put the next bit on the color-data wire.
2. Set LE low or high as required.
3. Wait briefly so the signals become stable.
4. Change DCLK from low to high.
5. Wait briefly.
6. Change DCLK back to low.
```

In code, it looks roughly like this:

```c
set_data(bit);
set_le(le_state);
set_dclk(0);
short_delay();
set_dclk(1);       // chip reads the data here
short_delay();
set_dclk(0);
```

## 5. Sending a 16-bit brightness number

Brightness values are sent most-significant bit first. That means bit 15 is sent
first and bit 0 is sent last:

```c
for (int bit = 15; bit >= 0; --bit) {
    set_data((brightness >> bit) & 1);
    pulse_dclk();
}
```

For example, `0x8001` is:

```text
1000 0000 0000 0001
^                      bit 15 sent first
                   ^   bit 0 sent last
```

## 6. What LE does

LE has two jobs:

1. It marks the end of a group of grayscale data.
2. It sends commands when held high for a particular number of DCLK pulses.

### Grayscale latch

After sending one OUT number for all chips in a lane, LE is high during only the
last DCLK pulse:

```text
chip 7 word: 16 clocks, LE low
chip 6 word: 16 clocks, LE low
...
chip 1 word: 16 clocks, LE low
chip 0 word: 15 clocks LE low, final clock LE high
```

That one high clock tells the chips: “This group is complete.”

### Commands

Some commands are created by sending extra DCLK pulses while LE stays high:

```text
LE high for 3 DCLK pulses  = VSYNC
LE high for 14 pulses      = configuration pre-activation
```

The number of pulses matters. Accidentally leaving LE high for too long can send
the wrong command.

## 7. Daisy chains: why data is sent backwards

Imagine pushing eight blocks through a tube. Every new block pushes the older
blocks farther down the tube.

The chips behave the same way:

```text
STM32 -> chip 0 -> chip 1 -> chip 2 -> ... -> chip 7
```

Although the first electrical word propagates farthest, the logical chip names
used by our verified panel mapper already include that physical direction. The
working software order is:

```text
send logical chip 0 data first
send logical chip 1 data second
...
send logical chip 7 data last
latch the group
```

We tested both orders. Reversing these indices produced scrambled coloured
rectangles; increasing order produced a perfect horizontal rainbow.

## 8. How one full grayscale frame is uploaded

Our panel uses 16 scan addresses. For every scan address, each chip needs one
brightness number for each of its 16 outputs.

The complete loop is:

```c
for (row = 0; row < 16; ++row) {
    for (output = 0; output < 16; ++output) {
        for (chip = 0; chip < 8; ++chip) {
            send_16_bit_brightness(row, output, chip);
        }

        // LE is high on the final DCLK of the final chip word.
        latch_output_group();
    }
}

send_vsync();
```

The actual six-lane implementation sends six brightness words in parallel:

```text
red 1, green 1, blue 1, red 2, green 2, blue 2
```

### Number of DCLK pulses

For our display:

```text
16 scan rows
* 16 outputs per chip
* 8 chips per lane
* 16 bits per brightness value
= 32,768 DCLK pulses per frame upload
```

We do not multiply this by six because all six color lanes work simultaneously.

## 9. VSYNC happens after the data

VSYNC is a separate command. Its three clocks do not replace the last three
grayscale clocks.

Correct sequence:

```text
1. Send the final grayscale bit with LE high for one DCLK.
2. Set LE low.
3. Set all RGB data wires low.
4. Set LE high.
5. Generate three new DCLK pulses.
6. Set LE low.
```

Timeline:

```text
                     final data latch          VSYNC
LE   _________________/‾\_____________________/‾‾‾‾‾‾‾‾\____
DCLK ...__/‾\___/‾\______            ___/‾\_/‾\_/‾\_______
                   ^ final bit            ^   ^   ^
```

The VSYNC command tells the chips that the newly uploaded frame is complete and
can be displayed.

## 10. Why there are only 16 scan rows for a 32-row panel

The panel activates two physical rows at the same time:

```text
scan address 0 -> physical rows 0 and 16
scan address 1 -> physical rows 1 and 17
...
scan address 15 -> physical rows 15 and 31
```

The first row uses R1/G1/B1. The second uses R2/G2/B2.

Therefore:

```text
32 physical rows / 2 simultaneous rows = 16 scan addresses
```

The following values must all be 16:

```text
CFG1 scan-row count
number of SRAM rows uploaded
number of A/B/C/D/E addresses scanned
```

If they disagree, the image may flicker, repeat rows, or appear scrambled.

## 11. What the CFG1 scan setting does

CFG1 tells the SM16380SC how many internal SRAM rows belong to one frame. The
value is stored as “number of rows minus one”:

```text
1 scan row  -> store 0
8 scan rows -> store 7
16 rows     -> store 15
32 rows     -> store 31
64 rows     -> store 63
```

For our display:

```c
scan_field = 16 - 1;  // 15
cfg1 |= scan_field << 7;
```

The chip does not control A–E. It only advances its internal SRAM/PWM row. The
STM32 must change A–E at the matching moment.

## 12. What is GCLK?

DCLK uploads data. GCLK is different: it makes the internal PWM/display engine
run.

On the tested panels, GCLK is also required while configuration and grayscale
data are being transferred. With GCLK stopped, the chips kept displaying random
cold-start SRAM. Pulsing GCLK alongside the serial transfer made the uploaded
uniform-red image appear.

For each row:

```text
1. Set A–E to the required row address.
2. Generate the required number of GCLK pulses.
3. Move to the next row address.
4. After row 15, return to row 0.
```

Our simple firmware repeats this forever.

## 13. What is FMPWM?

FMPWM is a two-bit setting in CFG1. It changes how many external GCLK pulses the
chip requires before advancing one row interval.

The working reference uses:

```c
gclk_per_row = ((1024 >> fmpwm) + 3);
```

| FMPWM | Calculation | GCLK pulses per row |
| ----: | ----------: | ------------------: |
|     0 |  `1024 + 3` |                1027 |
|     1 |   `512 + 3` |                 515 |
|     2 |   `256 + 3` |                 259 |
|     3 |   `128 + 3` |                 131 |

Increasing FMPWM by one halves the main GCLK count, so the display can scan rows
faster at the same external GCLK frequency.

The public datasheet says the chip supports scan-frequency multiplication, but
does not explain the internal `1024` formula or the exact meaning of the extra
three clocks. Those numbers come from a working STM32 driver and should be
treated as tested protocol constants.

Never change FMPWM without also changing the number of generated GCLK pulses.
Otherwise the chip's internal row and the STM32's external A–E row can become
unsynchronized.

For initial testing, use:

```c
#define SM16380SC_FMPWM_MODE 3
#define SM16380SC_GCLK_PER_ROW 131
```

## 14. Refresh rate

Approximate refresh rate is:

```text
refresh rate = GCLK frequency /
               (GCLK pulses per row * number of scan rows)
```

For example, with 1 MHz GCLK, FMPWM 3, and 16 scan rows:

```text
1,000,000 / (131 * 16) = about 477 complete scans per second
```

The real value is slightly lower because software also spends time changing row
addresses and executing loops.

## 15. How the 32-Kbit SRAM relates to the outputs

One chip stores 16-bit brightness for 16 outputs:

```text
one row = 16 outputs * 16 bits = 256 bits = 32 bytes
```

At the maximum 64 scan rows:

```text
64 * 16 outputs * 16 bits
= 16,384 bits
= 16 Kbit
```

The chip advertises 32 Kbit of SRAM, exactly twice the maximum visible-frame
payload. The working driver can upload a new frame while the previous frame is
being scanned, then commit it with VSYNC. This strongly suggests two 16-Kbit
frame/staging banks.

The public datasheet does not explicitly name these as two banks, so this is a
strong evidence-based explanation rather than a manufacturer-confirmed memory
diagram.

For our 16-scan-row configuration, one visible frame per chip contains:

```text
16 rows * 16 outputs * 16 bits
= 4096 bits
= 512 bytes
```

Do not assume the unused space becomes many user-selectable framebuffers. The
internal organization is not publicly documented.

## 16. Startup sequence

At a high level:

```text
1. Configure all GPIO pins as outputs and initially low.
2. Start or otherwise keep GCLK active.
3. Send the seven configuration registers.
4. Upload a complete grayscale frame while GCLK remains active.
5. Return LE low.
6. Send the separate three-clock VSYNC command.
7. Re-send the seven registers, matching the verified reference/firmware path.
8. Continue GCLK and change A–E from 0 through 15.
```

The exact configuration-register sequence is documented in the engineering
reference. During first testing, use a low current-gain setting and a dim image.

## 17. Common problems

| What you see                       | Likely reason                                         |
| ---------------------------------- | ----------------------------------------------------- |
| Completely dark                    | No GCLK, wrong chip profile, or logic voltage too low |
| Only 16 pixels work                | Wrong number of chips per lane                        |
| 16-pixel blocks reversed           | Daisy-chain chip order is reversed                    |
| Pixels reversed inside every block | OUT0..OUT15 mapping is reversed                       |
| Rows repeat                        | CFG1 scan count does not match A–E scanning           |
| Image flickers or rolls            | GCLK phase/count, row boundary, power, or signal integrity |
| Colors are swapped                 | RGB wire mapping is wrong                             |
| Random flash at startup            | SRAM was not cleared before display scanning          |

## 18. Electrical warning

The STM32 uses 3.3 V GPIO. If the SM16380SC is powered from 5 V, its documented
guaranteed high-input threshold is 70% of VDD:

```text
0.7 * 5 V = 3.5 V
```

An STM32's 3.3 V output is below that guaranteed threshold. Use a fast
74AHCT-family buffer between the STM32 and panel. Connect grounds together, but
power the LED panels from a separate supply capable of providing their current.

## 19. The most useful logic-analyser checks

For one full frame on our panel, verify:

```text
16 rows * 16 outputs * 8 chips * 16 bits = 32,768 DCLK edges
```

Also check:

- Brightness words are sent bit 15 first.
- There is one single-clock LE latch after every eight chip words.
- The final grayscale latch is followed by LE low.
- VSYNC then uses three additional DCLK pulses with LE high.
- GCLK remains active during configuration, upload, and VSYNC.
- A–E counts from 0 through 15.
- Each address remains selected for 131 GCLK pulses when FMPWM is 3.

If these conditions are true, the basic protocol timing is probably correct and
remaining problems are most likely physical pixel mapping or electrical signal
quality.
