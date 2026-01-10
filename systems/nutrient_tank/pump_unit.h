#ifndef PUMP_UNIT_H
#define PUMP_UNIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gpio_switch.h"

/* Pump runtime state */
typedef struct
{
    uint8_t is_running;
    uint32_t started_at_ms;
    uint32_t stop_at_ms;
} PumpUnit_State_t;

/* Pump configuration */
typedef struct
{
    uint32_t flow_ul_per_s;
    uint32_t max_run_time_ms;
} PumpUnit_Config_t;

/* Pump instance */
typedef struct
{
    GPIO_switch_t *sw;
    PumpUnit_Config_t cfg;
    PumpUnit_State_t state;
} PumpUnit_t;

/* Initialize pump instance */
uint8_t pump_unit_init(PumpUnit_t *pump, GPIO_switch_t *sw);

/* Configure pump parameters */
uint8_t pump_unit_set_flow_ul_per_s(PumpUnit_t *pump, uint32_t flow_ul_per_s);
uint8_t pump_unit_set_max_run_time_ms(PumpUnit_t *pump, uint32_t max_run_time_ms);

/* Control */
uint8_t pump_unit_start_for_ms(PumpUnit_t *pump, uint32_t now_ms, uint32_t run_time_ms);
uint8_t pump_unit_stop(PumpUnit_t *pump);

/* Periodic processing (timeouts, etc.) */
void pump_unit_process(PumpUnit_t *pump, uint32_t now_ms);

/* Inline helpers */
static inline uint8_t pump_unit_is_running(const PumpUnit_t *pump)
{
    return (pump != NULL && pump->state.is_running != 0U) ? 1U : 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* PUMP_UNIT_H */
