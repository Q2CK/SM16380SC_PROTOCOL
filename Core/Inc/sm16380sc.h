#ifndef SM16380SC_H
#define SM16380SC_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/*
 * First-light profile. Change these three values to match the panel.
 * A 64-pixel-wide panel has four 16-output chips in each RGB serial lane.
 */
#define SM16380SC_SCAN_ROWS       32u
#define SM16380SC_CHIPS_PER_LANE   4u
#define SM16380SC_PANEL_WIDTH     (SM16380SC_CHIPS_PER_LANE * 16u)

/* Conservative software-current setting; valid range is 0..63. */
#define SM16380SC_CURRENT_GAIN     8u
#define SM16380SC_FMPWM_MODE       3u

/* Reference waveform: 1024 / 2^FMPWM plus three row-transition clocks. */
#define SM16380SC_GCLK_PER_ROW \
    ((1024u >> SM16380SC_FMPWM_MODE) + 3u)

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
#define SM_BUS_GPIO               GPIOC
#define SM_DCLK_PIN               GPIO_PIN_4
#define SM_LE_PIN                 GPIO_PIN_5
#define SM_R1_PIN                 GPIO_PIN_6
#define SM_G1_PIN                 GPIO_PIN_7
#define SM_B1_PIN                 GPIO_PIN_8
#define SM_R2_PIN                 GPIO_PIN_9
#define SM_G2_PIN                 GPIO_PIN_10
#define SM_B2_PIN                 GPIO_PIN_11

#define SM_GCLK_GPIO              GPIOA
#define SM_GCLK_PIN               GPIO_PIN_8

#define SM_ADDR_GPIO              GPIOB
#define SM_ADDR_A_PIN             GPIO_PIN_4
#define SM_ADDR_B_PIN             GPIO_PIN_5
#define SM_ADDR_C_PIN             GPIO_PIN_6
#define SM_ADDR_D_PIN             GPIO_PIN_7
#define SM_ADDR_E_PIN             GPIO_PIN_8

#define SM_RGB_PIN_MASK \
    (SM_R1_PIN | SM_G1_PIN | SM_B1_PIN | SM_R2_PIN | SM_G2_PIN | SM_B2_PIN)
#define SM_BUS_PIN_MASK \
    (SM_RGB_PIN_MASK | SM_DCLK_PIN | SM_LE_PIN)
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

void SM16380SC_GPIO_Init(void);
void SM16380SC_Init(void);
void SM16380SC_UploadTestImage(void);
void SM16380SC_ScanForever(void) __attribute__((noreturn));

#endif /* SM16380SC_H */
