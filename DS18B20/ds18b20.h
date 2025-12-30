#ifndef DS18B20_H
#define DS18B20_H

#include <stdint.h>

#if defined(STM32F030x8) || defined(STM32F0xx)
#include "stm32f0xx_hal.h"
#elif defined(STM32F401xE) || defined(STM32F446xx) || defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#else
#error "ds18b20: Unsupported STM32 family. Add proper HAL include selection."
#endif

#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User-configurable retry policy */
#ifndef DS18B20_MAX_RETRIES
#define DS18B20_MAX_RETRIES                 (3U)
#endif

#ifndef DS18B20_RETRY_DELAY_MS
#define DS18B20_RETRY_DELAY_MS              (10U)
#endif

/* Optional critical section hooks (timing robustness) */
#ifndef DS18B20_CRITICAL_ENTER
#define DS18B20_CRITICAL_ENTER()            do { } while (0)
#endif

#ifndef DS18B20_CRITICAL_EXIT
#define DS18B20_CRITICAL_EXIT()             do { } while (0)
#endif

typedef enum
{
    DS18B20_RESOLUTION_9_BIT = 0,
    DS18B20_RESOLUTION_10_BIT,
    DS18B20_RESOLUTION_11_BIT,
    DS18B20_RESOLUTION_12_BIT
} ds18b20_resolution_t;

typedef enum
{
    DS18B20_OK = 0,
    DS18B20_ERR_PARAM = -1,
    DS18B20_ERR_TIMER = -2,
    DS18B20_ERR_PRESENCE = -3,
    DS18B20_ERR_CRC = -4,
    DS18B20_ERR_IO = -5
} ds18b20_status_t;

typedef struct
{
    GPIO_t dq;
    TIM_HandleTypeDef *htim;
    ds18b20_resolution_t resolution;
    uint8_t dq_pin_index;
} DS18B20_t;

/* Initialize DS18B20 instance.
   Requirement: timer counter must run at 1 MHz (1 tick = 1 us) in free-run mode. */
ds18b20_status_t ds18b20_init(DS18B20_t *dev, TIM_HandleTypeDef *htim, GPIO_t dq);

/* Read temperature in Celsius (blocking conversion + scratchpad read). */
ds18b20_status_t ds18b20_read_temperature(DS18B20_t *dev, float *temperature_c);

/* Set sensor resolution (writes config + optional EEPROM copy). */
ds18b20_status_t ds18b20_set_resolution(DS18B20_t *dev, ds18b20_resolution_t resolution);

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_H */
