#include "sn74hc595_chain.h"

#include <string.h>

/* --- internal helpers --- */

static inline void sn74hc595_chain_gpio_write(const GPIO_t *pin, GPIO_PinState state)
{
    if (pin == NULL || pin->port == NULL)
    {
        return;
    }

    HAL_GPIO_WritePin(pin->port, pin->pin, state);
}

static inline void sn74hc595_chain_delay_short(void)
{
    /* Short delay to guarantee pulse width; usually not required on STM32 */
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static void sn74hc595_chain_clock_pulse(const SN74HC595_Chain_t *instance)
{
    sn74hc595_chain_gpio_write(&instance->clk, GPIO_PIN_SET);
    sn74hc595_chain_delay_short();
    sn74hc595_chain_gpio_write(&instance->clk, GPIO_PIN_RESET);
}

static void sn74hc595_chain_latch_pulse(const SN74HC595_Chain_t *instance)
{
    sn74hc595_chain_gpio_write(&instance->latch, GPIO_PIN_SET);
    sn74hc595_chain_delay_short();
    sn74hc595_chain_gpio_write(&instance->latch, GPIO_PIN_RESET);
}

static void sn74hc595_chain_shift_out_byte(const SN74HC595_Chain_t *instance, uint8_t value)
{
    /* Shift MSB first */
    for (int8_t i = 7; i >= 0; i--)
    {
        GPIO_PinState bit_state = ((value & (uint8_t)(1U << (uint8_t)i)) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

        sn74hc595_chain_gpio_write(&instance->ds, bit_state);
        sn74hc595_chain_clock_pulse(instance);
    }
}

static void sn74hc595_chain_shift_out_buffer(const SN74HC595_Chain_t *instance)
{
    if (instance == NULL || instance->buffer == NULL || instance->bytes_count == 0U)
    {
        return;
    }

    /* Keep latch low while shifting data */
    sn74hc595_chain_gpio_write(&instance->latch, GPIO_PIN_RESET);

    for (uint16_t i = 0; i < instance->bytes_count; i++)
    {
        sn74hc595_chain_shift_out_byte(instance, instance->buffer[i]);
    }

    /* Latch data to outputs */
    sn74hc595_chain_latch_pulse(instance);

    /* Optional: restore DS low */
    sn74hc595_chain_gpio_write(&instance->ds, GPIO_PIN_RESET);
}

/* --- public API --- */

int sn74hc595_chain_init(SN74HC595_Chain_t *instance,
                         const GPIO_t *ds,
                         const GPIO_t *clk,
                         const GPIO_t *latch,
                         uint8_t *buffer,
                         uint16_t bytes_count)
{
    if (instance == NULL || ds == NULL || clk == NULL || latch == NULL || buffer == NULL)
    {
        return -1;
    }

    if (bytes_count == 0U)
    {
        return -2;
    }

    if (ds->port == NULL || clk->port == NULL || latch->port == NULL)
    {
        return -3;
    }

    instance->ds = *ds;
    instance->clk = *clk;
    instance->latch = *latch;
    instance->buffer = buffer;
    instance->bytes_count = bytes_count;

    /* Ensure default pin states */
    sn74hc595_chain_gpio_write(&instance->ds, GPIO_PIN_RESET);
    sn74hc595_chain_gpio_write(&instance->clk, GPIO_PIN_RESET);
    sn74hc595_chain_gpio_write(&instance->latch, GPIO_PIN_RESET);

    /* Default outputs low */
    memset(instance->buffer, 0, (size_t)instance->bytes_count);

    sn74hc595_chain_shift_out_buffer(instance);

    return 0;
}

int sn74hc595_chain_init_pins(SN74HC595_Chain_t *instance,
                              GPIO_TypeDef *ds_port,    uint16_t ds_pin,
                              GPIO_TypeDef *clk_port,   uint16_t clk_pin,
                              GPIO_TypeDef *latch_port, uint16_t latch_pin,
                              uint8_t *buffer,
                              uint16_t bytes_count)
{
    GPIO_t ds = { .port = ds_port, .pin = ds_pin };
    GPIO_t clk = { .port = clk_port, .pin = clk_pin };
    GPIO_t latch = { .port = latch_port, .pin = latch_pin };

    return sn74hc595_chain_init(instance, &ds, &clk, &latch, buffer, bytes_count);
}

int sn74hc595_chain_write(SN74HC595_Chain_t *instance, const uint8_t *data, uint16_t len)
{
    if (instance == NULL || instance->buffer == NULL || data == NULL)
    {
        return -1;
    }

    if (len != instance->bytes_count)
    {
        return -2;
    }

    memcpy(instance->buffer, data, (size_t)instance->bytes_count);
    sn74hc595_chain_shift_out_buffer(instance);

    return 0;
}

void sn74hc595_chain_clear(SN74HC595_Chain_t *instance)
{
    if (instance == NULL || instance->buffer == NULL)
    {
        return;
    }

    memset(instance->buffer, 0, (size_t)instance->bytes_count);
    sn74hc595_chain_shift_out_buffer(instance);
}

void sn74hc595_chain_refresh(SN74HC595_Chain_t *instance)
{
    sn74hc595_chain_shift_out_buffer(instance);
}

void sn74hc595_chain_set_byte(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t value)
{
    if (instance == NULL || instance->buffer == NULL || byte_index >= instance->bytes_count)
    {
        return;
    }

    instance->buffer[byte_index] = value;
}

void sn74hc595_chain_set_bit(SN74HC595_Chain_t *instance, uint16_t bit_index, uint8_t bit_value)
{
    if (instance == NULL || instance->buffer == NULL || instance->bytes_count == 0U)
    {
        return;
    }

    uint32_t total_bits = (uint32_t)instance->bytes_count * 8U;
    if ((uint32_t)bit_index >= total_bits)
    {
        return;
    }

    uint16_t byte_index = (uint16_t)(bit_index / 8U);
    uint8_t bit = (uint8_t)(bit_index % 8U);
    uint8_t mask = (uint8_t)(1U << bit);

    if (bit_value != 0U)
    {
        instance->buffer[byte_index] |= mask;
    }
    else
    {
        instance->buffer[byte_index] &= (uint8_t)~mask;
    }
}

void sn74hc595_chain_set_bits(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t mask)
{
    if (instance == NULL || instance->buffer == NULL || byte_index >= instance->bytes_count)
    {
        return;
    }

    instance->buffer[byte_index] |= mask;
}

void sn74hc595_chain_clear_bits(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t mask)
{
    if (instance == NULL || instance->buffer == NULL || byte_index >= instance->bytes_count)
    {
        return;
    }

    instance->buffer[byte_index] &= (uint8_t)~mask;
}
