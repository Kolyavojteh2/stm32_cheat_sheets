#ifndef SN74HC595_H
#define SN74HC595_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * HAL include:
 * - For Cube projects, these device macros are normally provided by CMSIS.
 * - The driver supports STM32F030C8T6, STM32F401RE, STM32F446RE.
 */
#if defined(STM32F0xx) || defined(STM32F030x8)
#include "stm32f0xx_hal.h"
#elif defined(STM32F4xx) || defined(STM32F401xE) || defined(STM32F446xx)
#include "stm32f4xx_hal.h"
#else
#error "Unsupported STM32 family for sn74hc595. Add the correct HAL include here."
#endif

#include "gpio.h"

/*
 * SN74HC595 8-bit serial-in, parallel-out shift register (bit-banged).
 *
 * Notes:
 * - DS, CLK, LATCH pins must be configured as GPIO output (push-pull) by the user.
 * - This driver shifts MSB first (bit 7 .. bit 0).
 */
typedef struct
{
    GPIO_t ds;
    GPIO_t clk;
    GPIO_t latch;

    uint8_t value;
} SN74HC595_t;

/* Returns 0 on success, negative value on error */
int sn74hc595_init(SN74HC595_t *instance,
                   const GPIO_t *ds,
                   const GPIO_t *clk,
                   const GPIO_t *latch);

/* Convenience init if you prefer port/pin arguments */
int sn74hc595_init_pins(SN74HC595_t *instance,
                        GPIO_TypeDef *ds_port,    uint16_t ds_pin,
                        GPIO_TypeDef *clk_port,   uint16_t clk_pin,
                        GPIO_TypeDef *latch_port, uint16_t latch_pin);

void sn74hc595_write_value(SN74HC595_t *instance, uint8_t value);
void sn74hc595_clear(SN74HC595_t *instance);

/* bit: 0..7, bit_value: 0 clears, non-zero sets */
void sn74hc595_write_bit(SN74HC595_t *instance, uint8_t bit, uint8_t bit_value);

void sn74hc595_set_bits(SN74HC595_t *instance, uint8_t mask);
void sn74hc595_clear_bits(SN74HC595_t *instance, uint8_t mask);

/* Re-send cached value to the shift register */
void sn74hc595_refresh(SN74HC595_t *instance);

static inline uint8_t sn74hc595_get_value(const SN74HC595_t *instance)
{
    return (instance != NULL) ? instance->value : 0U;
}

/* Optional legacy aliases (enable only if you must keep old API names) */
#ifdef SN74HC595_ENABLE_LEGACY_API
#define SN74HC595_init        sn74hc595_init_pins
#define SN74HC595_write_value sn74hc595_write_value
#define SN74HC595_clear       sn74hc595_clear
#define SN74HC595_write_bit   sn74hc595_write_bit
#endif

#ifdef __cplusplus
}
#endif

#endif /* SN74HC595_H */
