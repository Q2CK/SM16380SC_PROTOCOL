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

static inline void gclk_pulse(void)
{
    SM_GCLK_GPIO->BSRR = SM_GCLK_PIN;
    bus_delay();
    SM_GCLK_GPIO->BSRR = (uint32_t)SM_GCLK_PIN << 16u;
    bus_delay();
}

static inline void write_rgb6(uint32_t rgb)
{
    /* Reset all six lanes, then set the requested lanes atomically. */
    SM_BUS_GPIO->BSRR = ((uint32_t)SM_RGB_PIN_MASK << 16u) |
                        (rgb & SM_RGB_PIN_MASK);
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

    SM_ADDR_GPIO->BSRR = ((uint32_t)SM_ADDR_PIN_MASK << 16u) |
                         pins;
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

void SM16380SC_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Establish all-low levels before enabling the pins as outputs. */
    SM_BUS_GPIO->BSRR = (uint32_t)SM_BUS_PIN_MASK << 16u;
    SM_GCLK_GPIO->BSRR = (uint32_t)SM_GCLK_PIN << 16u;
    SM_ADDR_GPIO->BSRR = (uint32_t)SM_ADDR_PIN_MASK << 16u;

    gpio.Pin = SM_BUS_PIN_MASK;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SM_BUS_GPIO, &gpio);

    gpio.Pin = SM_GCLK_PIN;
    HAL_GPIO_Init(SM_GCLK_GPIO, &gpio);

    gpio.Pin = SM_ADDR_PIN_MASK;
    HAL_GPIO_Init(SM_ADDR_GPIO, &gpio);
}

void SM16380SC_Init(void)
{
    const uint16_t cfg1 = make_cfg1();

    set_row(0u);
    write_rgb6(0u);
    bus_clear(SM_DCLK_PIN | SM_LE_PIN);
    SM_GCLK_GPIO->BSRR = (uint32_t)SM_GCLK_PIN << 16u;

    /* Original SM16380/SM16380SC seven-register initialization. */
    send_command(SM_CMD_VSYNC);
    write_config(cfg1,   cfg1,   cfg1,   SM_CMD_CFG1);
    write_config(0x0001, 0x0001, 0x0001, SM_CMD_CFG2);
    write_config(0x10a3, 0x1463, 0x1063, SM_CMD_CFG3);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG4);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG5);
    write_config(0x0005, 0x0019, 0x002e, SM_CMD_CFG6);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG7);
}

/*
 * Electrical test image generated without a framebuffer:
 * - vertical red, green, blue, yellow bars;
 * - lower HUB75 half uses a dimmer level;
 * - a white diagonal marks scan-row/output mapping.
 */
static SM16380SC_Gray6 test_pixel(unsigned row, unsigned chip, unsigned out)
{
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
}

void SM16380SC_UploadTestImage(void)
{
    for (unsigned row = 0; row < SM16380SC_SCAN_ROWS; ++row) {
        for (unsigned out = 0; out < 16u; ++out) {
            /* First word reaches the farthest chip; last reaches the nearest. */
            for (unsigned slot = 0; slot < SM16380SC_CHIPS_PER_LANE; ++slot) {
                const unsigned chip = SM16380SC_CHIPS_PER_LANE - 1u - slot;
                const bool final_chip =
                    slot == (SM16380SC_CHIPS_PER_LANE - 1u);
                const SM16380SC_Gray6 pixel = test_pixel(row, chip, out);

                shift_gray6(&pixel, final_chip ? 1u : 0u);
            }
        }
    }

    /* LE was low after the last grayscale latch. These are three new clocks. */
    send_command(SM_CMD_VSYNC);
}

void SM16380SC_ScanForever(void)
{
    for (;;) {
        for (unsigned row = 0; row < SM16380SC_SCAN_ROWS; ++row) {
            set_row(row);
            for (unsigned pulse = 0; pulse < SM16380SC_GCLK_PER_ROW; ++pulse) {
                gclk_pulse();
            }
        }
    }
}
