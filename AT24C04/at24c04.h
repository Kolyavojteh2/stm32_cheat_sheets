#ifndef AT24C04_H
#define AT24C04_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#if defined(STM32F030x8) || defined(STM32F0xx)
#include "stm32f0xx_hal.h"
#elif defined(STM32F401xE) || defined(STM32F446xx) || defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#elif defined(STM32G030xx) || defined(STM32G0xx)
#include "stm32g0xx_hal.h"
#else
#error "at24c04: Unsupported STM32 family (expected STM32F0xx / STM32F4xx / STM32G0xx)"
#endif


#include "gpio.h"

/* Memory geometry */
#define AT24C04_TOTAL_SIZE_BYTES          (512U)
#define AT24C04_BLOCK_SIZE_BYTES          (256U)
#define AT24C04_PAGE_SIZE_BYTES           (16U)

/* I2C base 7-bit address (without A2/A1/A8 bits applied) is 0b1010xxx */
#define AT24C04_I2C_TYPE_ID               (0x50U) /* 0b1010_000 */

/* Default timeouts (ms) */
#define AT24C04_DEFAULT_I2C_TIMEOUT_MS    (100U)
#define AT24C04_DEFAULT_READY_TIMEOUT_MS  (10U)   /* Total wait after write; real tWR up to few ms */
#define AT24C04_DEFAULT_READY_POLL_MS     (1U)

typedef enum
{
    AT24C04_OK = 0,
    AT24C04_ERR_PARAM,
    AT24C04_ERR_RANGE,
    AT24C04_ERR_I2C,
    AT24C04_ERR_TIMEOUT
} at24c04_status_t;

typedef struct
{
    I2C_HandleTypeDef *hi2c;

    /* Hardware client address pins (A2, A1). For some packages they can be fixed to 0. */
    uint8_t a2;
    uint8_t a1;

    /* Optional WP pin control */
    const GPIO_t *wp_pin;
    uint8_t wp_active_high;

    /* Timeouts */
    uint32_t i2c_timeout_ms;
    uint32_t ready_timeout_ms;
    uint32_t ready_poll_ms;
} AT24C04_t;

/* Time base hooks (override if you want) */
__weak uint32_t at24c04_get_tick(void);
__weak void at24c04_delay_ms(uint32_t ms);

/* Init */
at24c04_status_t at24c04_init(AT24C04_t *dev, I2C_HandleTypeDef *hi2c, uint8_t a2, uint8_t a1);

/* Optional WP pin */
at24c04_status_t at24c04_set_wp_pin(AT24C04_t *dev, const GPIO_t *wp_pin, uint8_t wp_active_high);
at24c04_status_t at24c04_wp_enable(AT24C04_t *dev);
at24c04_status_t at24c04_wp_disable(AT24C04_t *dev);

/* Basic ops */
at24c04_status_t at24c04_read(AT24C04_t *dev, uint16_t mem_addr, uint8_t *data, uint16_t len);
at24c04_status_t at24c04_write(AT24C04_t *dev, uint16_t mem_addr, const uint8_t *data, uint16_t len);

/* Convenience */
static inline at24c04_status_t at24c04_read_u8(AT24C04_t *dev, uint16_t mem_addr, uint8_t *value)
{
    return at24c04_read(dev, mem_addr, value, 1U);
}

static inline at24c04_status_t at24c04_write_u8(AT24C04_t *dev, uint16_t mem_addr, uint8_t value)
{
    return at24c04_write(dev, mem_addr, &value, 1U);
}

#ifdef __cplusplus
}
#endif

#endif /* AT24C04_H */
