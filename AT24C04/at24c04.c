#include "at24c04.h"

/* -------- Internal helpers -------- */

/* Build 7-bit I2C address: 1010 A2 A1 A8 (R/W handled by HAL). */
static uint16_t at24c04_build_dev_addr_8bit(const AT24C04_t *dev, uint16_t mem_addr)
{
    uint8_t a8 = (uint8_t)((mem_addr >> 8) & 0x01U);
    uint8_t a2 = (uint8_t)(dev->a2 & 0x01U);
    uint8_t a1 = (uint8_t)(dev->a1 & 0x01U);

    uint8_t addr7 =
        (uint8_t)(AT24C04_I2C_TYPE_ID |
                  (uint8_t)(a2 << 2) |
                  (uint8_t)(a1 << 1) |
                  (uint8_t)(a8 << 0));

    /* STM32 HAL expects 7-bit address shifted left by 1. */
    return (uint16_t)(addr7 << 1);
}

static at24c04_status_t at24c04_wait_ready(const AT24C04_t *dev, uint16_t mem_addr)
{
    uint32_t start = at24c04_get_tick();

    /* After a write, device NACKs until internal write cycle completes. */
    while ((at24c04_get_tick() - start) < dev->ready_timeout_ms)
    {
        uint16_t dev_addr = at24c04_build_dev_addr_8bit(dev, mem_addr);

        if (HAL_I2C_IsDeviceReady(dev->hi2c, dev_addr, 1U, dev->i2c_timeout_ms) == HAL_OK)
        {
            return AT24C04_OK;
        }

        if (dev->ready_poll_ms > 0U)
        {
            at24c04_delay_ms(dev->ready_poll_ms);
        }
    }

    return AT24C04_ERR_TIMEOUT;
}

static at24c04_status_t at24c04_validate_args(AT24C04_t *dev)
{
    if (dev == NULL || dev->hi2c == NULL)
    {
        return AT24C04_ERR_PARAM;
    }

    if (dev->i2c_timeout_ms == 0U)
    {
        dev->i2c_timeout_ms = AT24C04_DEFAULT_I2C_TIMEOUT_MS;
    }

    if (dev->ready_timeout_ms == 0U)
    {
        dev->ready_timeout_ms = AT24C04_DEFAULT_READY_TIMEOUT_MS;
    }

    if (dev->ready_poll_ms == 0U)
    {
        dev->ready_poll_ms = AT24C04_DEFAULT_READY_POLL_MS;
    }

    return AT24C04_OK;
}

/* -------- Weak hooks -------- */

__weak uint32_t at24c04_get_tick(void)
{
    return HAL_GetTick();
}

__weak void at24c04_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* -------- Public API -------- */

at24c04_status_t at24c04_init(AT24C04_t *dev, I2C_HandleTypeDef *hi2c, uint8_t a2, uint8_t a1)
{
    if (dev == NULL)
    {
        return AT24C04_ERR_PARAM;
    }

    dev->hi2c = hi2c;
    dev->a2 = (uint8_t)(a2 & 0x01U);
    dev->a1 = (uint8_t)(a1 & 0x01U);

    dev->wp_pin = NULL;
    dev->wp_active_high = 1U;

    dev->i2c_timeout_ms = AT24C04_DEFAULT_I2C_TIMEOUT_MS;
    dev->ready_timeout_ms = AT24C04_DEFAULT_READY_TIMEOUT_MS;
    dev->ready_poll_ms = AT24C04_DEFAULT_READY_POLL_MS;

    return at24c04_validate_args(dev);
}

at24c04_status_t at24c04_set_wp_pin(AT24C04_t *dev, const GPIO_t *wp_pin, uint8_t wp_active_high)
{
    at24c04_status_t st = at24c04_validate_args(dev);
    if (st != AT24C04_OK)
    {
        return st;
    }

    dev->wp_pin = wp_pin;
    dev->wp_active_high = (uint8_t)(wp_active_high ? 1U : 0U);

    return AT24C04_OK;
}

at24c04_status_t at24c04_wp_enable(AT24C04_t *dev)
{
    at24c04_status_t st = at24c04_validate_args(dev);
    if (st != AT24C04_OK)
    {
        return st;
    }

    if (dev->wp_pin == NULL)
    {
        return AT24C04_ERR_PARAM;
    }

    /* WP asserted -> writes disabled */
    GPIO_PinState s = dev->wp_active_high ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(dev->wp_pin->port, dev->wp_pin->pin, s);

    return AT24C04_OK;
}

at24c04_status_t at24c04_wp_disable(AT24C04_t *dev)
{
    at24c04_status_t st = at24c04_validate_args(dev);
    if (st != AT24C04_OK)
    {
        return st;
    }

    if (dev->wp_pin == NULL)
    {
        return AT24C04_ERR_PARAM;
    }

    /* WP deasserted -> writes enabled */
    GPIO_PinState s = dev->wp_active_high ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(dev->wp_pin->port, dev->wp_pin->pin, s);

    return AT24C04_OK;
}

at24c04_status_t at24c04_read(AT24C04_t *dev, uint16_t mem_addr, uint8_t *data, uint16_t len)
{
    at24c04_status_t st = at24c04_validate_args(dev);
    if (st != AT24C04_OK)
    {
        return st;
    }

    if (data == NULL || len == 0U)
    {
        return AT24C04_ERR_PARAM;
    }

    if ((uint32_t)mem_addr + (uint32_t)len > AT24C04_TOTAL_SIZE_BYTES)
    {
        return AT24C04_ERR_RANGE;
    }

    /* Split reads across 256-byte blocks because A8 is part of device address. */
    uint16_t addr = mem_addr;
    uint16_t remaining = len;
    uint8_t *p = data;

    while (remaining > 0U)
    {
        uint16_t in_block_off = (uint16_t)(addr & 0xFFU);
        uint16_t block_left = (uint16_t)(AT24C04_BLOCK_SIZE_BYTES - in_block_off);
        uint16_t chunk = (remaining < block_left) ? remaining : block_left;

        uint16_t dev_addr = at24c04_build_dev_addr_8bit(dev, addr);
        uint16_t word_addr = (uint16_t)(addr & 0xFFU);

        if (HAL_I2C_Mem_Read(dev->hi2c,
                             dev_addr,
                             (uint16_t)word_addr,
                             I2C_MEMADD_SIZE_8BIT,
                             p,
                             chunk,
                             dev->i2c_timeout_ms) != HAL_OK)
        {
            return AT24C04_ERR_I2C;
        }

        addr = (uint16_t)(addr + chunk);
        p += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return AT24C04_OK;
}

at24c04_status_t at24c04_write(AT24C04_t *dev, uint16_t mem_addr, const uint8_t *data, uint16_t len)
{
    at24c04_status_t st = at24c04_validate_args(dev);
    if (st != AT24C04_OK)
    {
        return st;
    }

    if (data == NULL || len == 0U)
    {
        return AT24C04_ERR_PARAM;
    }

    if ((uint32_t)mem_addr + (uint32_t)len > AT24C04_TOTAL_SIZE_BYTES)
    {
        return AT24C04_ERR_RANGE;
    }

    /* Page write: up to 16 bytes, must not cross page boundary. Also must not cross 256-byte block boundary. */
    uint16_t addr = mem_addr;
    uint16_t remaining = len;
    const uint8_t *p = data;

    while (remaining > 0U)
    {
        uint16_t page_off = (uint16_t)(addr % AT24C04_PAGE_SIZE_BYTES);
        uint16_t page_left = (uint16_t)(AT24C04_PAGE_SIZE_BYTES - page_off);

        uint16_t in_block_off = (uint16_t)(addr & 0xFFU);
        uint16_t block_left = (uint16_t)(AT24C04_BLOCK_SIZE_BYTES - in_block_off);

        uint16_t chunk = remaining;
        if (chunk > page_left)
        {
            chunk = page_left;
        }
        if (chunk > block_left)
        {
            chunk = block_left;
        }

        uint16_t dev_addr = at24c04_build_dev_addr_8bit(dev, addr);
        uint16_t word_addr = (uint16_t)(addr & 0xFFU);

        if (HAL_I2C_Mem_Write(dev->hi2c,
                              dev_addr,
                              (uint16_t)word_addr,
                              I2C_MEMADD_SIZE_8BIT,
                              (uint8_t *)p,
                              chunk,
                              dev->i2c_timeout_ms) != HAL_OK)
        {
            return AT24C04_ERR_I2C;
        }

        /* Wait until write cycle completes (ACK polling). */
        st = at24c04_wait_ready(dev, addr);
        if (st != AT24C04_OK)
        {
            return st;
        }

        addr = (uint16_t)(addr + chunk);
        p += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return AT24C04_OK;
}
