#include "dht22.h"

/* Default timings tuned for DHT22 protocol */
void dht22_default_config(DHT22_Config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    cfg->start_low_ms = 2;
    cfg->response_timeout_us = 200;
    cfg->bit_timeout_us = 120;
    cfg->bit_threshold_us = 40;
    cfg->use_internal_pullup = 1;
}

static inline uint32_t dht22_timer_now(const DHT22_t *dev)
{
    return __HAL_TIM_GET_COUNTER(dev->htim);
}

static inline uint32_t dht22_timer_diff_us(const DHT22_t *dev, uint32_t start, uint32_t end)
{
    if (end >= start) {
        return end - start;
    }

    return (dev->timer_period + 1U - start) + end;
}

static DHT22_Status_t dht22_delay_us(const DHT22_t *dev, uint32_t us)
{
    uint32_t start;
    uint32_t now;

    if ((dev == NULL) || (dev->htim == NULL)) {
        return DHT22_STATUS_ERR_NO_TIMEBASE;
    }

    start = dht22_timer_now(dev);

    while (1) {
        now = dht22_timer_now(dev);
        if (dht22_timer_diff_us(dev, start, now) >= us) {
            break;
        }
    }

    return DHT22_STATUS_OK;
}

static inline GPIO_PinState dht22_line_read(const DHT22_t *dev)
{
    return HAL_GPIO_ReadPin(dev->data_pin.port, dev->data_pin.pin);
}

static inline void dht22_line_write_low(const DHT22_t *dev)
{
    HAL_GPIO_WritePin(dev->data_pin.port, dev->data_pin.pin, GPIO_PIN_RESET);
}

static inline void dht22_line_write_high(const DHT22_t *dev)
{
    HAL_GPIO_WritePin(dev->data_pin.port, dev->data_pin.pin, GPIO_PIN_SET);
}

static void dht22_gpio_set_output_od(const DHT22_t *dev)
{
    GPIO_InitTypeDef init;

    init.Pin = dev->data_pin.pin;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = (dev->cfg.use_internal_pullup != 0U) ? GPIO_PULLUP : GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(dev->data_pin.port, &init);
}

static void dht22_gpio_set_input(const DHT22_t *dev)
{
    GPIO_InitTypeDef init;

    init.Pin = dev->data_pin.pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = (dev->cfg.use_internal_pullup != 0U) ? GPIO_PULLUP : GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(dev->data_pin.port, &init);
}

static inline uint32_t dht22_irq_save_disable(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void dht22_irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static DHT22_Status_t dht22_wait_for_level(const DHT22_t *dev, GPIO_PinState level, uint32_t timeout_us)
{
    uint32_t start;
    uint32_t now;

    start = dht22_timer_now(dev);

    while (dht22_line_read(dev) != level) {
        now = dht22_timer_now(dev);
        if (dht22_timer_diff_us(dev, start, now) >= timeout_us) {
            return DHT22_STATUS_ERR_TIMEOUT;
        }
    }

    return DHT22_STATUS_OK;
}

static DHT22_Status_t dht22_ensure_timer_started(DHT22_t *dev)
{
    HAL_StatusTypeDef st;

    if ((dev == NULL) || (dev->htim == NULL)) {
        return DHT22_STATUS_ERR_NO_TIMEBASE;
    }

    if (dev->timer_started != 0U) {
        return DHT22_STATUS_OK;
    }

    st = HAL_TIM_Base_Start(dev->htim);
    if (st != HAL_OK) {
        return DHT22_STATUS_ERR_HAL;
    }

    dev->timer_started = 1U;
    return DHT22_STATUS_OK;
}

DHT22_Status_t dht22_init(DHT22_t *dev, GPIO_t data_pin, TIM_HandleTypeDef *htim, const DHT22_Config_t *cfg)
{
    DHT22_Config_t tmp;
    DHT22_Status_t st;

    if ((dev == NULL) || (htim == NULL)) {
        return DHT22_STATUS_ERR_NULL;
    }

    dev->data_pin = data_pin;
    dev->htim = htim;
    dev->timer_period = __HAL_TIM_GET_AUTORELOAD(htim);
    dev->timer_started = 0U;

    if (cfg != NULL) {
        dev->cfg = *cfg;
    } else {
        dht22_default_config(&tmp);
        dev->cfg = tmp;
    }

    st = dht22_ensure_timer_started(dev);
    if (st != DHT22_STATUS_OK) {
        return st;
    }

    /* Put line to idle high via open-drain + pull-up */
    dht22_gpio_set_output_od(dev);
    dht22_line_write_high(dev);

    return DHT22_STATUS_OK;
}

DHT22_Status_t dht22_read_raw(DHT22_t *dev, uint8_t raw[5])
{
    uint8_t data[5];
    uint32_t primask;
    uint32_t t_start;
    uint32_t t_end;
    uint32_t high_us;
    DHT22_Status_t st;
    uint8_t byte_idx;
    uint8_t bit_idx;
    uint8_t bit_val;

    if ((dev == NULL) || (raw == NULL)) {
        return DHT22_STATUS_ERR_NULL;
    }

    st = dht22_ensure_timer_started(dev);
    if (st != DHT22_STATUS_OK) {
        return st;
    }

    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    data[4] = 0;

    /* Host start: drive low >= 1 ms (typical 2 ms) */
    dht22_gpio_set_output_od(dev);
    dht22_line_write_low(dev);
    HAL_Delay(dev->cfg.start_low_ms);

    /*
     * Timing critical section:
     *  - release line and sample pulses in microseconds
     */
    primask = dht22_irq_save_disable();

    dht22_gpio_set_input(dev);
    st = dht22_delay_us(dev, 40);
    if (st != DHT22_STATUS_OK) {
        dht22_irq_restore(primask);
        return st;
    }

    /* Sensor response: LOW ~80 us, HIGH ~80 us, then LOW ~50 us (start bit 0) */
    st = dht22_wait_for_level(dev, GPIO_PIN_RESET, dev->cfg.response_timeout_us);
    if (st != DHT22_STATUS_OK) {
        dht22_irq_restore(primask);
        return st;
    }

    st = dht22_wait_for_level(dev, GPIO_PIN_SET, dev->cfg.response_timeout_us);
    if (st != DHT22_STATUS_OK) {
        dht22_irq_restore(primask);
        return st;
    }

    st = dht22_wait_for_level(dev, GPIO_PIN_RESET, dev->cfg.response_timeout_us);
    if (st != DHT22_STATUS_OK) {
        dht22_irq_restore(primask);
        return st;
    }

    /* Read 40 bits */
    for (byte_idx = 0; byte_idx < 5; byte_idx++) {
        for (bit_idx = 0; bit_idx < 8; bit_idx++) {
            /* Each bit: LOW ~50 us, then HIGH (26-28 us for 0, ~70 us for 1) */

            st = dht22_wait_for_level(dev, GPIO_PIN_SET, dev->cfg.bit_timeout_us);
            if (st != DHT22_STATUS_OK) {
                dht22_irq_restore(primask);
                return st;
            }

            t_start = dht22_timer_now(dev);

            st = dht22_wait_for_level(dev, GPIO_PIN_RESET, dev->cfg.bit_timeout_us);
            if (st != DHT22_STATUS_OK) {
                dht22_irq_restore(primask);
                return st;
            }

            t_end = dht22_timer_now(dev);
            high_us = dht22_timer_diff_us(dev, t_start, t_end);

            bit_val = (high_us > dev->cfg.bit_threshold_us) ? 1U : 0U;

            data[byte_idx] <<= 1;
            data[byte_idx] |= (uint8_t)bit_val;
        }
    }

    dht22_irq_restore(primask);

    raw[0] = data[0];
    raw[1] = data[1];
    raw[2] = data[2];
    raw[3] = data[3];
    raw[4] = data[4];

    return DHT22_STATUS_OK;
}

DHT22_Status_t dht22_read(DHT22_t *dev, DHT22_Data_t *out)
{
    uint8_t raw[5];
    uint8_t sum;
    uint16_t rh;
    uint16_t t_u16;
    int16_t t_x10;
    DHT22_Status_t st;

    if ((dev == NULL) || (out == NULL)) {
        return DHT22_STATUS_ERR_NULL;
    }

    st = dht22_read_raw(dev, raw);
    if (st != DHT22_STATUS_OK) {
        return st;
    }

    sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (sum != raw[4]) {
        return DHT22_STATUS_ERR_CHECKSUM;
    }

    rh = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    t_u16 = (uint16_t)(((uint16_t)raw[2] << 8) | raw[3]);

    /* Temperature sign bit is bit15 (MSB of raw[2]) */
    if ((t_u16 & 0x8000U) != 0U) {
        t_u16 &= 0x7FFFU;
        t_x10 = -(int16_t)t_u16;
    } else {
        t_x10 = (int16_t)t_u16;
    }

    out->humidity_x10 = rh;
    out->temperature_x10 = t_x10;

    out->raw[0] = raw[0];
    out->raw[1] = raw[1];
    out->raw[2] = raw[2];
    out->raw[3] = raw[3];
    out->raw[4] = raw[4];

    return DHT22_STATUS_OK;
}

const char *dht22_status_str(DHT22_Status_t status)
{
    switch (status) {
        case DHT22_STATUS_OK:
            return "OK";
        case DHT22_STATUS_ERR_NULL:
            return "NULL";
        case DHT22_STATUS_ERR_NO_TIMEBASE:
            return "NO_TIMEBASE";
        case DHT22_STATUS_ERR_TIMEOUT:
            return "TIMEOUT";
        case DHT22_STATUS_ERR_CHECKSUM:
            return "CHECKSUM";
        case DHT22_STATUS_ERR_HAL:
            return "HAL";
        default:
            return "UNKNOWN";
    }
}
