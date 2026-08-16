#include "sm16380sc.h"
#include <stdbool.h>

enum {
    SM_CMD_VSYNC      = 3u,
    SM_CMD_CFG1       = 4u,
    SM_CMD_CFG5       = 6u,
    SM_CMD_CFG2       = 8u,
    SM_CMD_CFG7       = 11u,
    SM_CMD_CFG4       = 12u,
    SM_CMD_PREACTIVE  = 14u,
    SM_CMD_CFG6       = 15u,
    SM_CMD_CFG3       = 16u,
};

/* Keep setup/hold visible on a logic analyser. Tune only after first light. */
static inline void bus_delay(void)
{
    for (volatile unsigned i = 0; i < 10u; ++i) {
        __NOP();
    }
}

static inline void bus_set(uint32_t pins)
{
    SM_BUS_GPIO->BSRR = pins;
}

static inline void bus_clear(uint32_t pins)
{
    SM_BUS_GPIO->BSRR = pins << 16u;
}

static inline void dclk_pulse(void)
{
    bus_set(SM_DCLK_PIN);
    bus_delay();
    bus_clear(SM_DCLK_PIN);
    bus_delay();
}

static inline void write_rgb6(uint32_t rgb)
{
    const uint32_t high = rgb & SM_RGB_PIN_MASK;
    const uint32_t low = SM_RGB_PIN_MASK & ~high;

    /* Never request set and reset for the same GPIO in one BSRR write. */
    SM_BUS_GPIO->BSRR = (low << 16u) | high;
}

static uint32_t pack_gray_bit(const SM16380SC_Gray6 *gray, unsigned bit)
{
    const uint16_t mask = (uint16_t)(1u << bit);
    uint32_t pins = 0u;

    if ((gray->r1 & mask) != 0u) pins |= SM_R1_PIN;
    if ((gray->g1 & mask) != 0u) pins |= SM_G1_PIN;
    if ((gray->b1 & mask) != 0u) pins |= SM_B1_PIN;
    if ((gray->r2 & mask) != 0u) pins |= SM_R2_PIN;
    if ((gray->g2 & mask) != 0u) pins |= SM_G2_PIN;
    if ((gray->b2 & mask) != 0u) pins |= SM_B2_PIN;

    return pins;
}

/* LE is high for exactly the final le_tail_clocks rising DCLK edges. */
static void shift_gray6(const SM16380SC_Gray6 *gray,
                        unsigned le_tail_clocks)
{
    for (int bit = 15; bit >= 0; --bit) {
        write_rgb6(pack_gray_bit(gray, (unsigned)bit));

        if ((unsigned)bit < le_tail_clocks) {
            bus_set(SM_LE_PIN);
        } else {
            bus_clear(SM_LE_PIN);
        }

        bus_delay();
        dclk_pulse();
    }

    bus_clear(SM_LE_PIN);
    write_rgb6(0u);
    bus_delay();
}

/* Command clocks are additional clocks and never overlap grayscale payload. */
static void send_command(unsigned le_high_clocks)
{
    bus_clear(SM_LE_PIN | SM_DCLK_PIN);
    write_rgb6(0u);
    bus_delay();

    bus_set(SM_LE_PIN);
    for (unsigned i = 0; i < le_high_clocks; ++i) {
        dclk_pulse();
    }
    bus_clear(SM_LE_PIN);
    bus_delay();
}

static void set_row(unsigned row)
{
    uint32_t pins = 0u;

    if ((row & 0x01u) != 0u) pins |= SM_ADDR_A_PIN;
    if ((row & 0x02u) != 0u) pins |= SM_ADDR_B_PIN;
    if ((row & 0x04u) != 0u) pins |= SM_ADDR_C_PIN;
    if ((row & 0x08u) != 0u) pins |= SM_ADDR_D_PIN;
    if ((row & 0x10u) != 0u) pins |= SM_ADDR_E_PIN;

    const uint32_t high = pins & SM_ADDR_PIN_MASK;
    const uint32_t low = SM_ADDR_PIN_MASK & ~high;
    SM_ADDR_GPIO->BSRR = (low << 16u) | high;
    bus_delay();
}

static uint16_t make_cfg1(void)
{
    return (uint16_t)(0x8000u |
        (SM16380SC_CURRENT_GAIN & 0x3fu) |
        (((SM16380SC_SCAN_ROWS - 1u) & 0x3fu) << 7u) |
        ((SM16380SC_FMPWM_MODE & 0x03u) << 13u));
}

static void broadcast_config(uint16_t red, uint16_t green, uint16_t blue,
                             unsigned selector)
{
    const SM16380SC_Gray6 value = {
        .r1 = red,   .g1 = green, .b1 = blue,
        .r2 = red,   .g2 = green, .b2 = blue,
    };

    for (unsigned chip = 0; chip < SM16380SC_CHIPS_PER_LANE; ++chip) {
        const bool final_chip = chip == (SM16380SC_CHIPS_PER_LANE - 1u);
        shift_gray6(&value, final_chip ? selector : 0u);
    }
}

static void write_config(uint16_t red, uint16_t green, uint16_t blue,
                         unsigned selector)
{
    send_command(SM_CMD_PREACTIVE);
    broadcast_config(red, green, blue, selector);
}

static void configure_all_registers(void)
{
    const uint16_t cfg1 = make_cfg1();

    write_config(cfg1,   cfg1,   cfg1,   SM_CMD_CFG1);
    write_config(0x0001, 0x0001, 0x0001, SM_CMD_CFG2);
    write_config(0x10a3, 0x1463, 0x1063, SM_CMD_CFG3);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG4);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG5);
    write_config(0x0005, 0x0019, 0x002e, SM_CMD_CFG6);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG7);
}

void SM16380SC_Init(TIM_HandleTypeDef *gclk_timer)
{
    set_row(0u);
    write_rgb6(0u);
    bus_clear(SM_DCLK_PIN | SM_LE_PIN);

    /*
     * TIM1 runs continuously. Its repetition counter makes one update event
     * after exactly SM16380SC_GCLK_PER_ROW PWM periods, not after every GCLK.
     */
    __HAL_TIM_SET_COUNTER(gclk_timer, 0u);
    __HAL_TIM_CLEAR_FLAG(gclk_timer, TIM_FLAG_UPDATE);
    if (HAL_TIM_PWM_Start(gclk_timer, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
    __HAL_TIM_CLEAR_FLAG(gclk_timer, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_IT(gclk_timer, TIM_IT_UPDATE);

    /* Original SM16380/SM16380SC seven-register initialization. */
    send_command(SM_CMD_VSYNC);
    configure_all_registers();
}

/*
 * Electrical test image generated without a framebuffer:
 * - vertical red, green, blue, yellow bars;
 * - lower HUB75 half uses a dimmer level;
 * - a white diagonal marks scan-row/output mapping.
 */
static SM16380SC_Gray6 test_pixel(unsigned row, unsigned chip, unsigned out)
{
#if SM16380SC_TEST_PATTERN == SM16380SC_TEST_SOLID_RED
    (void)row;
    (void)chip;
    (void)out;
    return (SM16380SC_Gray6) {
        .r1 = SM16380SC_TEST_LEVEL,
        .r2 = SM16380SC_TEST_LEVEL,
    };
#elif SM16380SC_TEST_PATTERN == SM16380SC_TEST_RAINBOW
    const unsigned x = chip * 16u + out;
    const uint16_t full = 0xf000u;
    const unsigned wheel = x * 6u;
    const unsigned segment = wheel / SM16380SC_PANEL_WIDTH;
    const uint16_t fade = (uint16_t)(((wheel % SM16380SC_PANEL_WIDTH) *
                                      (uint32_t)full) /
                                     SM16380SC_PANEL_WIDTH);
    const uint16_t rise = fade;
    const uint16_t fall = (uint16_t)(full - fade);
    uint16_t r = 0u, g = 0u, b = 0u;

    /* Red -> yellow -> green -> cyan -> blue -> magenta -> red. */
    switch (segment) {
    case 0u: r = full; g = rise;                         break;
    case 1u: r = fall; g = full;                         break;
    case 2u:           g = full; b = rise;               break;
    case 3u:           g = fall; b = full;               break;
    case 4u: r = rise;           b = full;               break;
    default: r = full;           b = fall;               break;
    }

    (void)row;
    return (SM16380SC_Gray6) {
        .r1 = r, .g1 = g, .b1 = b,
        .r2 = r, .g2 = g, .b2 = b,
    };
#elif SM16380SC_TEST_PATTERN == SM16380SC_TEST_TV_PATTERN
    const unsigned x = chip * 16u + out;
    const uint16_t full = 0xf000u;
    const uint16_t dim = 0x3000u;
    SM16380SC_Gray6 pixel = {0};

    /* Generate one RGB value for each of the two simultaneously driven rows. */
    for (unsigned half = 0; half < 2u; ++half) {
        const unsigned y = row + half * SM16380SC_SCAN_ROWS;
        uint16_t r = 0u, g = 0u, b = 0u;

        if (x == 0u || x == (SM16380SC_PANEL_WIDTH - 1u) ||
            y == 0u || y == 31u) {
            /* White frame proves the complete 128x32 extent. */
            r = g = b = full;
        } else if (y < 20u) {
            /* TV bars: white, yellow, cyan, green, magenta, red, blue. */
            const unsigned bar = (x * 7u) / SM16380SC_PANEL_WIDTH;
            static const uint8_t colors[7] = {
                7u, 6u, 3u, 2u, 5u, 4u, 1u
            };
            const uint8_t color = colors[bar > 6u ? 6u : bar];
            if ((color & 4u) != 0u) r = full;
            if ((color & 2u) != 0u) g = full;
            if ((color & 1u) != 0u) b = full;
        } else if (y < 25u) {
            /* Eight steps from black to white expose bit/brightness errors. */
            const uint16_t level = (uint16_t)(((x * 8u) /
                SM16380SC_PANEL_WIDTH) * 0x2000u);
            r = g = b = level;
        } else {
            /* Fine checkerboard plus coloured 16-pixel chip boundaries. */
            if ((((x >> 2u) ^ (y >> 1u)) & 1u) != 0u) {
                r = g = b = dim;
            }
            if ((x & 15u) == 0u) {
                r = full;
                g = (x & 16u) != 0u ? full : 0u;
                b = (x & 32u) != 0u ? full : 0u;
            }
        }

        if (half == 0u) {
            pixel.r1 = r; pixel.g1 = g; pixel.b1 = b;
        } else {
            pixel.r2 = r; pixel.g2 = g; pixel.b2 = b;
        }
    }

    return pixel;
#else
    const unsigned x = chip * 16u + out;
    const uint16_t top_level = 0x2000u;
    const uint16_t bottom_level = 0x0800u;
    SM16380SC_Gray6 pixel = {0};

    switch ((x / 16u) & 3u) {
    case 0u:
        pixel.r1 = top_level;
        pixel.r2 = bottom_level;
        break;
    case 1u:
        pixel.g1 = top_level;
        pixel.g2 = bottom_level;
        break;
    case 2u:
        pixel.b1 = top_level;
        pixel.b2 = bottom_level;
        break;
    default:
        pixel.r1 = top_level;
        pixel.g1 = top_level;
        pixel.r2 = bottom_level;
        pixel.g2 = bottom_level;
        break;
    }

    if (x == (row % SM16380SC_PANEL_WIDTH)) {
        pixel.r1 = pixel.g1 = pixel.b1 = 0x3000u;
        pixel.r2 = pixel.g2 = pixel.b2 = 0x1800u;
    }

    return pixel;
#endif
}

void SM16380SC_UploadTestImage(void)
{
    for (unsigned row = 0; row < SM16380SC_SCAN_ROWS; ++row) {
        for (unsigned out = 0; out < 16u; ++out) {
            /*
             * Match the proven driver: logical chip sections are serialized
             * in increasing x order. The panel's internal cascade performs
             * the physical shift; reversing here scrambles 16-pixel blocks.
             */
            for (unsigned slot = 0; slot < SM16380SC_CHIPS_PER_LANE; ++slot) {
                const unsigned chip = slot;
                const bool final_chip =
                    slot == (SM16380SC_CHIPS_PER_LANE - 1u);
                const SM16380SC_Gray6 pixel = test_pixel(row, chip, out);

                shift_gray6(&pixel, final_chip ? 1u : 0u);
            }
        }
    }

    /* Match the working reference's post-upload commit/reconfigure sequence. */
    send_command(SM_CMD_VSYNC);
    configure_all_registers();
}

void SM16380SC_RowPeriodElapsed(void)
{
    static unsigned row = 0u;

    row++;
    if (row >= SM16380SC_SCAN_ROWS) {
        row = 0u;
    }
    set_row(row);
}
