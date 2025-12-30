#include "sr04m.h"

/* Read GPIO pin state */
static inline uint8_t sr04m_gpio_read(const GPIO_t *gpio)
{
    GPIO_PinState st = HAL_GPIO_ReadPin(gpio->port, gpio->pin);
    return (st == GPIO_PIN_SET) ? 1U : 0U;
}

/* Write GPIO pin state */
static inline void sr04m_gpio_write(const GPIO_t *gpio, uint8_t level)
{
    HAL_GPIO_WritePin(gpio->port, gpio->pin, (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Get timer counter in microseconds (1 MHz assumed) */
static inline uint32_t sr04m_tim_now_us(const SR04M_t *dev)
{
    return __HAL_TIM_GET_COUNTER(dev->htim);
}

/* Elapsed time on upcounter with wrap */
static uint32_t sr04m_tim_elapsed_us(const SR04M_t *dev, uint32_t start, uint32_t now)
{
    if (now >= start)
    {
        return (now - start);
    }

    return ((dev->timer_arr + 1U - start) + now);
}

/* Busy wait delay using the same 1 MHz timer */
static void sr04m_delay_us(const SR04M_t *dev, uint32_t us)
{
    uint32_t t0 = sr04m_tim_now_us(dev);

    while (sr04m_tim_elapsed_us(dev, t0, sr04m_tim_now_us(dev)) < us)
    {
        /* Busy wait */
    }
}

/* Convert echo pulse width (us) to distance (mm) */
static uint32_t sr04m_pulse_us_to_mm(const SR04M_t *dev, uint32_t pulse_us)
{
    /* distance_mm = pulse_us * speed_mm_s / (2 * 1e6) */
    uint64_t num = (uint64_t)pulse_us * (uint64_t)dev->speed_mm_s;
    uint32_t mm = (uint32_t)((num + 1000000ULL) / 2000000ULL);
    return mm;
}

/* Emit trigger pulse */
static void sr04m_trigger(const SR04M_t *dev)
{
    sr04m_gpio_write(&dev->trig, 0U);
    sr04m_delay_us(dev, 2U);

    sr04m_gpio_write(&dev->trig, 1U);
    sr04m_delay_us(dev, dev->trigger_pulse_us);
    sr04m_gpio_write(&dev->trig, 0U);
}

SR04M_status_t sr04m_init(SR04M_t *dev,
                          GPIO_t trig,
                          GPIO_t echo,
                          TIM_HandleTypeDef *htim)
{
    if (dev == NULL || htim == NULL)
    {
        return SR04M_STATUS_INVALID_PARAM;
    }

    dev->trig = trig;
    dev->echo = echo;
    dev->htim = htim;

    dev->timer_arr = __HAL_TIM_GET_AUTORELOAD(htim);

    /* Defaults tuned for JSN/AJ family */
    dev->speed_mm_s = 343000U;
    dev->trigger_pulse_us = 20U;
    dev->min_cycle_ms = 50U;

    /* 8m default max distance -> echo time */
    sr04m_set_max_distance_mm(dev, 8000U);

    dev->state = SR04M_STATE_IDLE;
    dev->t_rise = 0U;
    dev->t_fall = 0U;
    dev->last_distance_mm = 0U;
    dev->last_valid = 0U;
    dev->last_start_tick_ms = 0U;

    /* Ensure TRIG is low */
    sr04m_gpio_write(&dev->trig, 0U);

    return SR04M_STATUS_OK;
}

void sr04m_set_speed_of_sound_mm_s(SR04M_t *dev, uint32_t speed_mm_s)
{
    if (dev == NULL || speed_mm_s == 0U)
    {
        return;
    }

    dev->speed_mm_s = speed_mm_s;
}

void sr04m_set_trigger_pulse_us(SR04M_t *dev, uint32_t pulse_us)
{
    if (dev == NULL || pulse_us == 0U)
    {
        return;
    }

    dev->trigger_pulse_us = pulse_us;
}

void sr04m_set_max_distance_mm(SR04M_t *dev, uint32_t max_distance_mm)
{
    if (dev == NULL || max_distance_mm == 0U)
    {
        return;
    }

    /* max_echo_us = 2 * distance_mm / speed_mm_per_us */
    /* speed_mm_per_us = speed_mm_s / 1e6 */
    uint64_t num = 2ULL * (uint64_t)max_distance_mm * 1000000ULL;
    uint32_t max_us = (uint32_t)((num + (uint64_t)dev->speed_mm_s - 1ULL) / (uint64_t)dev->speed_mm_s);

    /* Add a small margin */
    dev->max_echo_us = max_us + 2000U;
}

void sr04m_set_min_cycle_ms(SR04M_t *dev, uint32_t min_cycle_ms)
{
    if (dev == NULL)
    {
        return;
    }

    dev->min_cycle_ms = min_cycle_ms;
}

SR04M_status_t sr04m_start(SR04M_t *dev)
{
    uint32_t now_ms;

    if (dev == NULL)
    {
        return SR04M_STATUS_INVALID_PARAM;
    }

    if (sr04m_is_busy(dev) != 0U)
    {
        return SR04M_STATUS_BUSY;
    }

    now_ms = HAL_GetTick();

    /* Respect minimum cycle time between triggers */
    if ((now_ms - dev->last_start_tick_ms) < dev->min_cycle_ms)
    {
        return SR04M_STATUS_NOT_READY;
    }

    dev->last_start_tick_ms = now_ms;
    dev->last_valid = 0U;

    sr04m_trigger(dev);

    dev->state = SR04M_STATE_WAIT_RISE;
    dev->t_rise = sr04m_tim_now_us(dev);

    return SR04M_STATUS_OK;
}

SR04M_status_t sr04m_process(SR04M_t *dev)
{
    uint32_t now_us;

    if (dev == NULL)
    {
        return SR04M_STATUS_INVALID_PARAM;
    }

    if (dev->state == SR04M_STATE_IDLE)
    {
        return SR04M_STATUS_NOT_READY;
    }

    if (dev->state == SR04M_STATE_DONE)
    {
        return SR04M_STATUS_OK;
    }

    if (dev->state == SR04M_STATE_TIMEOUT)
    {
        return SR04M_STATUS_TIMEOUT;
    }

    now_us = sr04m_tim_now_us(dev);

    if (dev->state == SR04M_STATE_WAIT_RISE)
    {
        if (sr04m_gpio_read(&dev->echo) != 0U)
        {
            dev->t_rise = now_us;
            dev->state = SR04M_STATE_WAIT_FALL;
            return SR04M_STATUS_BUSY;
        }

        if (sr04m_tim_elapsed_us(dev, dev->t_rise, now_us) > dev->max_echo_us)
        {
            dev->state = SR04M_STATE_TIMEOUT;
            return SR04M_STATUS_TIMEOUT;
        }

        return SR04M_STATUS_BUSY;
    }

    if (dev->state == SR04M_STATE_WAIT_FALL)
    {
        if (sr04m_gpio_read(&dev->echo) == 0U)
        {
            uint32_t pulse_us;

            dev->t_fall = now_us;
            pulse_us = sr04m_tim_elapsed_us(dev, dev->t_rise, dev->t_fall);

            dev->last_distance_mm = sr04m_pulse_us_to_mm(dev, pulse_us);
            dev->last_valid = 1U;
            dev->state = SR04M_STATE_DONE;

            return SR04M_STATUS_OK;
        }

        if (sr04m_tim_elapsed_us(dev, dev->t_rise, now_us) > dev->max_echo_us)
        {
            dev->state = SR04M_STATE_TIMEOUT;
            return SR04M_STATUS_TIMEOUT;
        }

        return SR04M_STATUS_BUSY;
    }

    return SR04M_STATUS_INVALID_PARAM;
}

void sr04m_abort(SR04M_t *dev)
{
    if (dev == NULL)
    {
        return;
    }

    dev->state = SR04M_STATE_IDLE;
}

SR04M_status_t sr04m_measure_mm(SR04M_t *dev,
                                uint32_t timeout_ms,
                                uint32_t *distance_mm)
{
    SR04M_status_t st;
    uint32_t t0;

    if (dev == NULL || distance_mm == NULL)
    {
        return SR04M_STATUS_INVALID_PARAM;
    }

    st = sr04m_start(dev);
    if (st != SR04M_STATUS_OK)
    {
        return st;
    }

    t0 = HAL_GetTick();

    while (1)
    {
        st = sr04m_process(dev);

        if (st == SR04M_STATUS_OK)
        {
            *distance_mm = dev->last_distance_mm;
            dev->state = SR04M_STATE_IDLE;
            return SR04M_STATUS_OK;
        }

        if (st == SR04M_STATUS_TIMEOUT)
        {
            dev->state = SR04M_STATE_IDLE;
            return SR04M_STATUS_TIMEOUT;
        }

        if ((HAL_GetTick() - t0) >= timeout_ms)
        {
            dev->state = SR04M_STATE_IDLE;
            return SR04M_STATUS_TIMEOUT;
        }
    }
}
