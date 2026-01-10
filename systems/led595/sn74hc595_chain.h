#ifndef SN74HC595_CHAIN_H
#define SN74HC595_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "sn74hc595.h"

/*
 * SN74HC595 chain driver (bit-banged).
 *
 * This driver supports N cascaded SN74HC595 chips connected in a chain:
 * MCU -> DS of chip[0] -> Q7S to DS of chip[1] -> ... -> chip[N-1].
 *
 * Buffer / byte order:
 * - buffer[0] is shifted out first.
 * - After shifting N bytes, buffer[0] appears on the farthest chip in the chain,
 *   and buffer[N-1] appears on the nearest chip (closest to MCU).
 *
 * Notes:
 * - DS, CLK, LATCH pins must be configured as GPIO output (push-pull) by the user.
 * - Shifting is MSB first for each byte (bit 7 .. bit 0).
 * - You can modify instance->buffer directly and call sn74hc595_chain_refresh()
 *   to apply multiple changes in one transfer.
 */
typedef struct
{
    GPIO_t ds;
    GPIO_t clk;
    GPIO_t latch;

    uint8_t *buffer;
    uint16_t bytes_count;
} SN74HC595_Chain_t;

/* Returns 0 on success, negative value on error */
int sn74hc595_chain_init(SN74HC595_Chain_t *instance,
                         const GPIO_t *ds,
                         const GPIO_t *clk,
                         const GPIO_t *latch,
                         uint8_t *buffer,
                         uint16_t bytes_count);

/* Convenience init if you prefer port/pin arguments */
int sn74hc595_chain_init_pins(SN74HC595_Chain_t *instance,
                              GPIO_TypeDef *ds_port,    uint16_t ds_pin,
                              GPIO_TypeDef *clk_port,   uint16_t clk_pin,
                              GPIO_TypeDef *latch_port, uint16_t latch_pin,
                              uint8_t *buffer,
                              uint16_t bytes_count);

/* Copy data into internal buffer and apply it */
int sn74hc595_chain_write(SN74HC595_Chain_t *instance, const uint8_t *data, uint16_t len);

/* Clear internal buffer (all zeros) and apply it */
void sn74hc595_chain_clear(SN74HC595_Chain_t *instance);

/* Apply current internal buffer to the outputs */
void sn74hc595_chain_refresh(SN74HC595_Chain_t *instance);

/*
 * Modify internal buffer only.
 * Call sn74hc595_chain_refresh() to apply changes.
 */

/* byte_index: 0..bytes_count-1 */
void sn74hc595_chain_set_byte(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t value);

/* bit_index: 0..(bytes_count*8-1), bit_value: 0 clears, non-zero sets */
void sn74hc595_chain_set_bit(SN74HC595_Chain_t *instance, uint16_t bit_index, uint8_t bit_value);

/* byte_index: 0..bytes_count-1 */
void sn74hc595_chain_set_bits(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t mask);
void sn74hc595_chain_clear_bits(SN74HC595_Chain_t *instance, uint16_t byte_index, uint8_t mask);

static inline uint8_t sn74hc595_chain_get_byte(const SN74HC595_Chain_t *instance, uint16_t byte_index)
{
    if (instance == NULL || instance->buffer == NULL || byte_index >= instance->bytes_count)
    {
        return 0U;
    }

    return instance->buffer[byte_index];
}

static inline uint8_t sn74hc595_chain_get_bit(const SN74HC595_Chain_t *instance, uint16_t bit_index)
{
    if (instance == NULL || instance->buffer == NULL || instance->bytes_count == 0U)
    {
        return 0U;
    }

    uint32_t total_bits = (uint32_t)instance->bytes_count * 8U;
    if ((uint32_t)bit_index >= total_bits)
    {
        return 0U;
    }

    uint16_t byte_index = (uint16_t)(bit_index / 8U);
    uint8_t bit = (uint8_t)(bit_index % 8U);

    return (uint8_t)((instance->buffer[byte_index] >> bit) & 0x01U);
}

#ifdef __cplusplus
}
#endif

#endif /* SN74HC595_CHAIN_H */
