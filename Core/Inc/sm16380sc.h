#ifndef SM16380SC_H
#define SM16380SC_H

#include "main.h"
#include <stdint.h>

/*
 * Two daisy-chained 64x32 panels form one 128x32 display. Each panel has
 * 24 SM16380SCs: four chips on each of the six HUB75 RGB data lanes. The two
 * panels therefore form an eight-chip serial chain on every lane. Two physical
 * rows are active together, so 32 physical rows require 16 scan addresses.
 */
#define SM16380SC_SCAN_ROWS       16u
#define SM16380SC_CHIPS_PER_LANE   8u
#define SM16380SC_PANEL_WIDTH     (SM16380SC_CHIPS_PER_LANE * 16u)

/* Conservative software-current setting; valid range is 0..63. */
#define SM16380SC_CURRENT_GAIN     63u
#define SM16380SC_FMPWM_MODE       3u

/* First-light diagnostic: every grayscale word is identical. */
#define SM16380SC_TEST_SOLID_RED    0u
#define SM16380SC_TEST_COLOR_BARS   1u
#define SM16380SC_TEST_TV_PATTERN   2u
#define SM16380SC_TEST_RAINBOW      3u
#define SM16380SC_TEST_PATTERN      SM16380SC_TEST_RAINBOW
#define SM16380SC_TEST_LEVEL        0x2000u

/* Reference waveform: 1024 / 2^FMPWM plus three row-transition clocks. */
#define SM16380SC_GCLK_PER_ROW \
    ((1024u >> SM16380SC_FMPWM_MODE) + 3u)

/* TIM1 RCR is configured for 131 periods by CubeMX in this implementation. */
#if SM16380SC_GCLK_PER_ROW != 131u
#error "Update TIM1 repetition count when changing SM16380SC_FMPWM_MODE"
#endif

/*
 * NUCLEO-F446RE wiring copied from the working reference project. RGB/DCLK/LE
 * share GPIOC. Row A..E are contiguous on GPIOB. GCLK is PA8.
 *
 * HUB75  -> STM32 pin
 * CLK    -> PC4
 * LAT/LE -> PC5
 * R1     -> PC6
 * G1     -> PC7
 * B1     -> PC8
 * R2     -> PC9
 * G2     -> PC10
 * B2     -> PC11
 * OE     -> PA8  (SM16380SC GCLK; active pulse, not conventional blanking OE)
 * A      -> PB4
 * B      -> PB5
 * C      -> PB6
 * D      -> PB7
 * E      -> PB8
 */
#define SM_BUS_GPIO               SM_DCLK_GPIO_Port
#define SM_DCLK_PIN               SM_DCLK_Pin
#define SM_LE_PIN                 SM_LE_Pin
#define SM_R1_PIN                 SM_R1_Pin
#define SM_G1_PIN                 SM_G1_Pin
#define SM_B1_PIN                 SM_B1_Pin
#define SM_R2_PIN                 SM_R2_Pin
#define SM_G2_PIN                 SM_G2_Pin
#define SM_B2_PIN                 SM_B2_Pin

#define SM_ADDR_GPIO              SM_ADDR_A_GPIO_Port
#define SM_ADDR_A_PIN             SM_ADDR_A_Pin
#define SM_ADDR_B_PIN             SM_ADDR_B_Pin
#define SM_ADDR_C_PIN             SM_ADDR_C_Pin
#define SM_ADDR_D_PIN             SM_ADDR_D_Pin
#define SM_ADDR_E_PIN             SM_ADDR_E_Pin

#define SM_RGB_PIN_MASK \
    (SM_R1_PIN | SM_G1_PIN | SM_B1_PIN | SM_R2_PIN | SM_G2_PIN | SM_B2_PIN)
#define SM_ADDR_PIN_MASK \
    (SM_ADDR_A_PIN | SM_ADDR_B_PIN | SM_ADDR_C_PIN | SM_ADDR_D_PIN | SM_ADDR_E_PIN)

typedef struct {
    uint16_t r1;
    uint16_t g1;
    uint16_t b1;
    uint16_t r2;
    uint16_t g2;
    uint16_t b2;
} SM16380SC_Gray6;

void SM16380SC_Init(TIM_HandleTypeDef *gclk_timer);
void SM16380SC_UploadTestImage(void);
void SM16380SC_RowPeriodElapsed(void);

#endif /* SM16380SC_H */
