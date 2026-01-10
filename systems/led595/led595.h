#ifndef LED595_H
#define LED595_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sn74hc595_chain.h"

/*
 * LED control on top of SN74HC595 chain.
 *
 * Logical LED indexing:
 * - led_index: 0..(sr->bytes_count * 8 - 1)
 * - byte = led_index / 8, bit = led_index % 8
 *
 * Polarity:
 * - active_low = 0: output '1' turns LED on (active-high)
 * - active_low = 1: output '0' turns LED on (active-low)
 */
typedef struct
{
    SN74HC595_Chain_t *sr;

    uint8_t active_low;
    uint8_t auto_refresh;
} LED595_t;

/*
 * auto_refresh:
 * - 0: functions only update internal buffer; you call led595_refresh() when needed
 * - 1: each function applies change immediately
 *
 * Returns 0 on success, negative value on error.
 */
int led595_init(LED595_t *instance, SN74HC595_Chain_t *sr, uint8_t active_low, uint8_t auto_refresh);

void led595_refresh(LED595_t *instance);
void led595_all_off(LED595_t *instance);
void led595_all_on(LED595_t *instance);

void led595_write(LED595_t *instance, uint16_t led_index, uint8_t on);
void led595_set(LED595_t *instance, uint16_t led_index);
void led595_clear(LED595_t *instance, uint16_t led_index);
void led595_toggle(LED595_t *instance, uint16_t led_index);

uint8_t led595_get(const LED595_t *instance, uint16_t led_index);

#ifdef __cplusplus
}
#endif

#endif /* LED595_H */
