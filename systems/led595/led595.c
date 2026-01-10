#include "led595.h"

#include <string.h>

static void led595_apply_if_needed(LED595_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    if (instance->auto_refresh != 0U)
    {
        sn74hc595_chain_refresh(instance->sr);
    }
}

static uint8_t led595_to_physical(const LED595_t *instance, uint8_t logical_on)
{
    if (instance == NULL)
    {
        return 0U;
    }

    if (logical_on != 0U)
    {
        return (instance->active_low != 0U) ? 0U : 1U;
    }

    return (instance->active_low != 0U) ? 1U : 0U;
}

static uint8_t led595_from_physical(const LED595_t *instance, uint8_t physical_bit)
{
    if (instance == NULL)
    {
        return 0U;
    }

    physical_bit = (physical_bit != 0U) ? 1U : 0U;

    if (instance->active_low != 0U)
    {
        return (physical_bit == 0U) ? 1U : 0U;
    }

    return physical_bit;
}

int led595_init(LED595_t *instance, SN74HC595_Chain_t *sr, uint8_t active_low, uint8_t auto_refresh)
{
    if (instance == NULL || sr == NULL || sr->buffer == NULL || sr->bytes_count == 0U)
    {
        return -1;
    }

    instance->sr = sr;
    instance->active_low = (active_low != 0U) ? 1U : 0U;
    instance->auto_refresh = (auto_refresh != 0U) ? 1U : 0U;

    /* Default all LEDs OFF */
    led595_all_off(instance);
    sn74hc595_chain_refresh(instance->sr);

    return 0;
}

void led595_refresh(LED595_t *instance)
{
    if (instance == NULL || instance->sr == NULL)
    {
        return;
    }

    sn74hc595_chain_refresh(instance->sr);
}

void led595_all_off(LED595_t *instance)
{
    if (instance == NULL || instance->sr == NULL || instance->sr->buffer == NULL)
    {
        return;
    }

    uint8_t fill = (instance->active_low != 0U) ? 0xFFU : 0x00U;
    memset(instance->sr->buffer, fill, (size_t)instance->sr->bytes_count);

    led595_apply_if_needed(instance);
}

void led595_all_on(LED595_t *instance)
{
    if (instance == NULL || instance->sr == NULL || instance->sr->buffer == NULL)
    {
        return;
    }

    uint8_t fill = (instance->active_low != 0U) ? 0x00U : 0xFFU;
    memset(instance->sr->buffer, fill, (size_t)instance->sr->bytes_count);

    led595_apply_if_needed(instance);
}

void led595_write(LED595_t *instance, uint16_t led_index, uint8_t on)
{
    if (instance == NULL || instance->sr == NULL)
    {
        return;
    }

    uint8_t physical = led595_to_physical(instance, on);
    sn74hc595_chain_set_bit(instance->sr, led_index, physical);

    led595_apply_if_needed(instance);
}

void led595_set(LED595_t *instance, uint16_t led_index)
{
    led595_write(instance, led_index, 1U);
}

void led595_clear(LED595_t *instance, uint16_t led_index)
{
    led595_write(instance, led_index, 0U);
}

void led595_toggle(LED595_t *instance, uint16_t led_index)
{
    if (instance == NULL)
    {
        return;
    }

    uint8_t current = led595_get(instance, led_index);
    led595_write(instance, led_index, (current == 0U) ? 1U : 0U);
}

uint8_t led595_get(const LED595_t *instance, uint16_t led_index)
{
    if (instance == NULL || instance->sr == NULL)
    {
        return 0U;
    }

    uint8_t physical = sn74hc595_chain_get_bit(instance->sr, led_index);
    return led595_from_physical(instance, physical);
}
