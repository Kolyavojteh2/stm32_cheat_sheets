#include "sn74hc595.h"

/* --- internal helpers --- */

static inline void sn74hc595_gpio_write(const GPIO_t *pin, GPIO_PinState state)
{
    if (pin == NULL || pin->port == NULL)
    {
        return;
    }

    HAL_GPIO_WritePin(pin->port, pin->pin, state);
}

static inline void sn74hc595_delay_short(void)
{
    /* Short delay to guarantee pulse width; usually not required on STM32 */
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static void sn74hc595_clock_pulse(const SN74HC595_t *instance)
{
    sn74hc595_gpio_write(&instance->clk, GPIO_PIN_SET);
    sn74hc595_delay_short();
    sn74hc595_gpio_write(&instance->clk, GPIO_PIN_RESET);
}

static void sn74hc595_latch_pulse(const SN74HC595_t *instance)
{
    sn74hc595_gpio_write(&instance->latch, GPIO_PIN_SET);
    sn74hc595_delay_short();
    sn74hc595_gpio_write(&instance->latch, GPIO_PIN_RESET);
}

static void sn74hc595_shift_out(const SN74HC595_t *instance, uint8_t value)
{
    if (instance == NULL)
    {
        return;
    }

    /* Keep latch low while shifting data */
    sn74hc595_gpio_write(&instance->latch, GPIO_PIN_RESET);

    for (int8_t i = 7; i >= 0; i--)
    {
        GPIO_PinState bit_state = ((value & (uint8_t)(1U << (uint8_t)i)) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

        sn74hc595_gpio_write(&instance->ds, bit_state);
        sn74hc595_clock_pulse(instance);
    }

    /* Latch data to outputs */
    sn74hc595_latch_pulse(instance);
}

/* --- public API --- */

int sn74hc595_init(SN74HC595_t *instance,
                   const GPIO_t *ds,
                   const GPIO_t *clk,
                   const GPIO_t *latch)
{
    if (instance == NULL || ds == NULL || clk == NULL || latch == NULL)
    {
        return -1;
    }

    if (ds->port == NULL || clk->port == NULL || latch->port == NULL)
    {
        return -2;
    }

    instance->ds = *ds;
    instance->clk = *clk;
    instance->latch = *latch;
    instance->value = 0U;

    /* Ensure default pin states */
    sn74hc595_gpio_write(&instance->ds, GPIO_PIN_RESET);
    sn74hc595_gpio_write(&instance->clk, GPIO_PIN_RESET);
    sn74hc595_gpio_write(&instance->latch, GPIO_PIN_RESET);

    sn74hc595_shift_out(instance, instance->value);

    return 0;
}

int sn74hc595_init_pins(SN74HC595_t *instance,
                        GPIO_TypeDef *ds_port,    uint16_t ds_pin,
                        GPIO_TypeDef *clk_port,   uint16_t clk_pin,
                        GPIO_TypeDef *latch_port, uint16_t latch_pin)
{
    GPIO_t ds = { .port = ds_port, .pin = ds_pin };
    GPIO_t clk = { .port = clk_port, .pin = clk_pin };
    GPIO_t latch = { .port = latch_port, .pin = latch_pin };

    return sn74hc595_init(instance, &ds, &clk, &latch);
}

void sn74hc595_write_value(SN74HC595_t *instance, uint8_t value)
{
    if (instance == NULL)
    {
        return;
    }

    instance->value = value;
    sn74hc595_shift_out(instance, instance->value);
}

void sn74hc595_clear(SN74HC595_t *instance)
{
    sn74hc595_write_value(instance, 0U);
}

void sn74hc595_write_bit(SN74HC595_t *instance, uint8_t bit, uint8_t bit_value)
{
    if (instance == NULL)
    {
        return;
    }

    if (bit > 7U)
    {
        return;
    }

    if (bit_value != 0U)
    {
        instance->value |= (uint8_t)(1U << bit);
    }
    else
    {
        instance->value &= (uint8_t)~(uint8_t)(1U << bit);
    }

    sn74hc595_shift_out(instance, instance->value);
}

void sn74hc595_set_bits(SN74HC595_t *instance, uint8_t mask)
{
    if (instance == NULL)
    {
        return;
    }

    instance->value |= mask;
    sn74hc595_shift_out(instance, instance->value);
}

void sn74hc595_clear_bits(SN74HC595_t *instance, uint8_t mask)
{
    if (instance == NULL)
    {
        return;
    }

    instance->value &= (uint8_t)~mask;
    sn74hc595_shift_out(instance, instance->value);
}

void sn74hc595_refresh(SN74HC595_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    sn74hc595_shift_out(instance, instance->value);
}
