#include "ds18b20.h"

/* 1-Wire commands */
#define DS18B20_CMD_SKIP_ROM                (0xCCU)
#define DS18B20_CMD_CONVERT_T               (0x44U)
#define DS18B20_CMD_READ_SCRATCHPAD         (0xBEU)
#define DS18B20_CMD_WRITE_SCRATCHPAD        (0x4EU)
#define DS18B20_CMD_COPY_SCRATCHPAD         (0x48U)

/* Conversion delays in ms for 9..12-bit resolution */
static const uint16_t ds18b20_conversion_delay_ms[4] = { 94U, 188U, 375U, 750U };

/* Return 1 if pin mask is exactly one bit (single GPIO pin). */
static inline uint8_t ds18b20_is_single_pin(uint16_t pin)
{
    return (pin != 0U && ((uint16_t)(pin & (uint16_t)(pin - 1U)) == 0U)) ? 1U : 0U;
}

/* Convert GPIO_PIN_x mask into 0..15 index. Assumes single pin. */
static uint8_t ds18b20_pin_to_index(uint16_t pin)
{
    for (uint8_t i = 0U; i < 16U; i++)
    {
        if (((uint16_t)(pin >> i) & 0x1U) != 0U)
        {
            return i;
        }
    }
    return 0xFFU;
}

/* Busy-wait delay in microseconds using timer counter (1 tick = 1 us). */
static void ds18b20_delay_us(const DS18B20_t *dev, uint32_t delay_us)
{
    uint32_t start = __HAL_TIM_GET_COUNTER(dev->htim);

    while ((uint32_t)(__HAL_TIM_GET_COUNTER(dev->htim) - start) < delay_us)
    {
        __NOP();
    }
}

/* Configure DQ as open-drain output and drive low. */
static inline void ds18b20_dq_drive_low(DS18B20_t *dev)
{
    GPIO_TypeDef *port = dev->dq.port;
    uint32_t pos2 = (uint32_t)dev->dq_pin_index * 2U;

    /* Output mode */
    port->MODER = (port->MODER & ~(0x3U << pos2)) | (0x1U << pos2);

    /* Open-drain */
    port->OTYPER |= (1U << dev->dq_pin_index);

    /* No pull-up/down (external pull-up is expected) */
    port->PUPDR &= ~(0x3U << pos2);

    /* High speed */
    port->OSPEEDR = (port->OSPEEDR & ~(0x3U << pos2)) | (0x3U << pos2);

    /* Drive low */
    port->BSRR = ((uint32_t)dev->dq.pin << 16U);
}

/* Release DQ (input Hi-Z). */
static inline void ds18b20_dq_release(DS18B20_t *dev)
{
    GPIO_TypeDef *port = dev->dq.port;
    uint32_t pos2 = (uint32_t)dev->dq_pin_index * 2U;

    /* Input mode */
    port->MODER &= ~(0x3U << pos2);

    /* No pull-up/down */
    port->PUPDR &= ~(0x3U << pos2);
}

/* Read DQ level (0/1). */
static inline uint8_t ds18b20_dq_read(const DS18B20_t *dev)
{
    return ((dev->dq.port->IDR & dev->dq.pin) != 0U) ? 1U : 0U;
}

/* Dallas/Maxim CRC8 (poly 0x31 reflected => 0x8C). */
static uint8_t ds18b20_crc8_maxim(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0U;

    while (len-- != 0U)
    {
        uint8_t inbyte = *data++;
        for (uint8_t i = 0U; i < 8U; i++)
        {
            uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01U);
            crc >>= 1U;
            if (mix != 0U)
            {
                crc ^= 0x8CU;
            }
            inbyte >>= 1U;
        }
    }

    return crc;
}

/* 1-Wire reset + presence detect. */
static uint8_t ds18b20_reset(DS18B20_t *dev)
{
    uint8_t presence;

    DS18B20_CRITICAL_ENTER();

    ds18b20_dq_drive_low(dev);
    ds18b20_delay_us(dev, 500U);

    ds18b20_dq_release(dev);
    ds18b20_delay_us(dev, 70U);

    presence = (ds18b20_dq_read(dev) == 0U) ? 1U : 0U;

    ds18b20_delay_us(dev, 410U);

    DS18B20_CRITICAL_EXIT();

    return presence;
}

/* Write one bit (LSB-first). */
static void ds18b20_write_bit(DS18B20_t *dev, uint8_t bit)
{
    DS18B20_CRITICAL_ENTER();

    if (bit != 0U)
    {
        /* Write '1': low 1..15 us, release till end of slot */
        ds18b20_dq_drive_low(dev);
        ds18b20_delay_us(dev, 6U);
        ds18b20_dq_release(dev);
        ds18b20_delay_us(dev, 64U);
    }
    else
    {
        /* Write '0': low ~60 us */
        ds18b20_dq_drive_low(dev);
        ds18b20_delay_us(dev, 60U);
        ds18b20_dq_release(dev);
        ds18b20_delay_us(dev, 10U);
    }

    DS18B20_CRITICAL_EXIT();
}

/* Write one byte (LSB-first). */
static void ds18b20_write_byte(DS18B20_t *dev, uint8_t data)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        ds18b20_write_bit(dev, (uint8_t)(data & 0x01U));
        data >>= 1U;
    }
}

/* Read one bit. */
static uint8_t ds18b20_read_bit(DS18B20_t *dev)
{
    uint8_t bit;

    DS18B20_CRITICAL_ENTER();

    /* Read slot: low >= 1 us, release, sample around 15 us from start */
    ds18b20_dq_drive_low(dev);
    ds18b20_delay_us(dev, 2U);
    ds18b20_dq_release(dev);

    ds18b20_delay_us(dev, 13U);
    bit = ds18b20_dq_read(dev);

    ds18b20_delay_us(dev, 45U);

    DS18B20_CRITICAL_EXIT();

    return bit;
}

/* Read one byte (LSB-first). */
static uint8_t ds18b20_read_byte(DS18B20_t *dev)
{
    uint8_t data = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        if (ds18b20_read_bit(dev) != 0U)
        {
            data |= (uint8_t)(1U << i);
        }
    }

    return data;
}

/* Start temperature conversion (SKIP ROM). */
static uint8_t ds18b20_start_conversion(DS18B20_t *dev)
{
    if (ds18b20_reset(dev) == 0U)
    {
        return 0U;
    }

    ds18b20_write_byte(dev, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(dev, DS18B20_CMD_CONVERT_T);

    return 1U;
}

/* Read scratchpad (9 bytes) and verify CRC. */
static uint8_t ds18b20_read_scratchpad(DS18B20_t *dev, uint8_t sp[9])
{
    if (ds18b20_reset(dev) == 0U)
    {
        return 0U;
    }

    ds18b20_write_byte(dev, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(dev, DS18B20_CMD_READ_SCRATCHPAD);

    for (uint8_t i = 0U; i < 9U; i++)
    {
        sp[i] = ds18b20_read_byte(dev);
    }

    return (ds18b20_crc8_maxim(sp, 8U) == sp[8]) ? 1U : 0U;
}

ds18b20_status_t ds18b20_init(DS18B20_t *dev, TIM_HandleTypeDef *htim, GPIO_t dq)
{
    HAL_StatusTypeDef st;

    if (dev == NULL || htim == NULL || dq.port == NULL || ds18b20_is_single_pin(dq.pin) == 0U)
    {
        return DS18B20_ERR_PARAM;
    }

    dev->dq = dq;
    dev->htim = htim;
    dev->resolution = DS18B20_RESOLUTION_12_BIT;
    dev->dq_pin_index = ds18b20_pin_to_index(dq.pin);

    /* Timer must be initialized before use. */
    if (HAL_TIM_Base_GetState(htim) == HAL_TIM_STATE_RESET)
    {
        return DS18B20_ERR_TIMER;
    }

    /* Start timer if needed. Treat HAL_BUSY as acceptable (already running). */
    st = HAL_TIM_Base_Start(htim);
    if (st != HAL_OK && st != HAL_BUSY)
    {
        return DS18B20_ERR_TIMER;
    }

    /* Release line initially (Hi-Z). */
    ds18b20_dq_release(dev);

    /* Optional presence check. */
    if (ds18b20_reset(dev) == 0U)
    {
        return DS18B20_ERR_PRESENCE;
    }

    return DS18B20_OK;
}

ds18b20_status_t ds18b20_read_temperature(DS18B20_t *dev, float *temperature_c)
{
    if (dev == NULL || temperature_c == NULL)
    {
        return DS18B20_ERR_PARAM;
    }

    if (dev->resolution > DS18B20_RESOLUTION_12_BIT)
    {
        return DS18B20_ERR_PARAM;
    }

    for (uint32_t attempt = 0U; attempt < DS18B20_MAX_RETRIES; attempt++)
    {
        if (ds18b20_start_conversion(dev) == 0U)
        {
            HAL_Delay(DS18B20_RETRY_DELAY_MS);
            continue;
        }

        HAL_Delay((uint32_t)ds18b20_conversion_delay_ms[dev->resolution]);

        uint8_t sp[9];
        if (ds18b20_read_scratchpad(dev, sp) != 0U)
        {
            int16_t raw = (int16_t)((uint16_t)sp[0] | ((uint16_t)sp[1] << 8U));
            *temperature_c = (float)raw / 16.0f;
            return DS18B20_OK;
        }

        HAL_Delay(DS18B20_RETRY_DELAY_MS);
    }

    return DS18B20_ERR_CRC;
}

ds18b20_status_t ds18b20_set_resolution(DS18B20_t *dev, ds18b20_resolution_t resolution)
{
    if (dev == NULL)
    {
        return DS18B20_ERR_PARAM;
    }

    if (resolution > DS18B20_RESOLUTION_12_BIT)
    {
        return DS18B20_ERR_PARAM;
    }

    if (ds18b20_reset(dev) == 0U)
    {
        return DS18B20_ERR_PRESENCE;
    }

    ds18b20_write_byte(dev, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(dev, DS18B20_CMD_WRITE_SCRATCHPAD);

    /* TH and TL bytes (defaults). */
    ds18b20_write_byte(dev, 0x4BU);
    ds18b20_write_byte(dev, 0x46U);

    /* Config register: bits 6:5 select resolution. */
    {
        uint8_t cfg = 0x1FU;
        cfg |= (uint8_t)((uint8_t)resolution << 5U);
        ds18b20_write_byte(dev, cfg);
    }

    /* Copy scratchpad to EEPROM (t_WR up to 10 ms). */
    if (ds18b20_reset(dev) == 0U)
    {
        return DS18B20_ERR_PRESENCE;
    }

    ds18b20_write_byte(dev, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(dev, DS18B20_CMD_COPY_SCRATCHPAD);

    HAL_Delay(15U);

    dev->resolution = resolution;

    return DS18B20_OK;
}
