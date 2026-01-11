#ifndef DHT22_H
#define DHT22_H

#ifdef __cplusplus
extern "C" {
#endif

/* Select proper HAL include for required MCUs */
#if defined(STM32F030x8)
    #include "stm32f0xx_hal.h"
#elif defined(STM32G030xx)
    #include "stm32g0xx_hal.h"
#elif defined(STM32F401xE) || defined(STM32F446xx)
    #include "stm32f4xx_hal.h"
#else
    #error "dht22: Unsupported MCU. Add proper HAL include selection here."
#endif

#include <stdint.h>
#include "gpio.h"

typedef enum
{
    DHT22_STATUS_OK = 0,
    DHT22_STATUS_ERR_NULL = -1,
    DHT22_STATUS_ERR_NO_TIMEBASE = -2,
    DHT22_STATUS_ERR_TIMEOUT = -3,
    DHT22_STATUS_ERR_CHECKSUM = -4,
    DHT22_STATUS_ERR_HAL = -5
} DHT22_Status_t;

typedef struct
{
    uint16_t start_low_ms;
    uint16_t response_timeout_us;
    uint16_t bit_timeout_us;
    uint16_t bit_threshold_us;
    uint8_t use_internal_pullup;
} DHT22_Config_t;

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint8_t raw[5];
} DHT22_Data_t;

typedef struct
{
    GPIO_t data_pin;
    TIM_HandleTypeDef *htim;
    uint32_t timer_period;
    uint8_t timer_started;
    DHT22_Config_t cfg;
} DHT22_t;

/* Fill config with defaults */
void dht22_default_config(DHT22_Config_t *cfg);

/*
 * Initialize DHT22 instance.
 * Requirements:
 *  - htim must be configured to 1 MHz counter frequency (1 tick = 1 us)
 *  - timer base can be 16-bit or 32-bit; period is used for wrap handling
 */
DHT22_Status_t dht22_init(DHT22_t *dev, GPIO_t data_pin, TIM_HandleTypeDef *htim, const DHT22_Config_t *cfg);

/* Read sensor and decode to x10 units (no float) */
DHT22_Status_t dht22_read(DHT22_t *dev, DHT22_Data_t *out);

/* Read raw 5 bytes (humidity hi/lo, temp hi/lo, checksum) */
DHT22_Status_t dht22_read_raw(DHT22_t *dev, uint8_t raw[5]);

/* Optional: string for debugging/logging */
const char *dht22_status_str(DHT22_Status_t status);

#ifdef __cplusplus
}
#endif

#endif /* DHT22_H */
