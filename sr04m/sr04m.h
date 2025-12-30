#ifndef SR04M_H
#define SR04M_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32F0xx)
#include "stm32f0xx_hal.h"
#elif defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#else
#error "Unsupported STM32 family. Define STM32F0xx or STM32F4xx."
#endif

#include "gpio.h"
#include <stdint.h>

typedef enum
{
    SR04M_STATUS_OK = 0,
    SR04M_STATUS_BUSY,
    SR04M_STATUS_TIMEOUT,
    SR04M_STATUS_INVALID_PARAM,
    SR04M_STATUS_NOT_READY
} SR04M_status_t;

typedef enum
{
    SR04M_STATE_IDLE = 0,
    SR04M_STATE_WAIT_RISE,
    SR04M_STATE_WAIT_FALL,
    SR04M_STATE_DONE,
    SR04M_STATE_TIMEOUT
} SR04M_state_t;

typedef struct
{
    GPIO_t trig;
    GPIO_t echo;

    TIM_HandleTypeDef *htim;

    uint32_t timer_arr;
    uint32_t speed_mm_s;

    uint32_t trigger_pulse_us;
    uint32_t max_echo_us;
    uint32_t min_cycle_ms;

    SR04M_state_t state;

    uint32_t t_rise;
    uint32_t t_fall;

    uint32_t last_distance_mm;
    uint8_t  last_valid;
    uint32_t last_start_tick_ms;
} SR04M_t;

/* Initialize instance.
   Timer must be configured to 1 MHz and started before use. */
SR04M_status_t sr04m_init(SR04M_t *dev,
                          GPIO_t trig,
                          GPIO_t echo,
                          TIM_HandleTypeDef *htim);

/* Optional configuration helpers */
void sr04m_set_speed_of_sound_mm_s(SR04M_t *dev, uint32_t speed_mm_s);
void sr04m_set_trigger_pulse_us(SR04M_t *dev, uint32_t pulse_us);
void sr04m_set_max_distance_mm(SR04M_t *dev, uint32_t max_distance_mm);
void sr04m_set_min_cycle_ms(SR04M_t *dev, uint32_t min_cycle_ms);

/* Non-blocking measurement */
SR04M_status_t sr04m_start(SR04M_t *dev);
SR04M_status_t sr04m_process(SR04M_t *dev);
void sr04m_abort(SR04M_t *dev);

/* Blocking measurement (internally uses polling, but returns only when done/timeout) */
SR04M_status_t sr04m_measure_mm(SR04M_t *dev,
                                uint32_t timeout_ms,
                                uint32_t *distance_mm);

/* Inline helpers */
static inline uint8_t sr04m_is_busy(const SR04M_t *dev)
{
    return (dev != NULL && (dev->state == SR04M_STATE_WAIT_RISE || dev->state == SR04M_STATE_WAIT_FALL)) ? 1U : 0U;
}

static inline uint8_t sr04m_has_last(const SR04M_t *dev)
{
    return (dev != NULL && dev->last_valid != 0U) ? 1U : 0U;
}

static inline uint32_t sr04m_last_distance_mm(const SR04M_t *dev)
{
    return (dev != NULL) ? dev->last_distance_mm : 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* SR04M_H */
