#ifndef PUMP_UNIT_H
#define PUMP_UNIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gpio_switch.h"

/* Switch control operations for GPIO_switch_t */
typedef uint8_t (*PumpUnit_SwitchFn)(GPIO_switch_t *sw);

typedef struct
{
    PumpUnit_SwitchFn on;
    PumpUnit_SwitchFn off;
} PumpUnit_SwitchOps_t;

/* Pump runtime state */
typedef struct
{
    uint8_t is_running;

    uint32_t started_at_ms;
    uint32_t requested_run_time_ms;

    uint32_t requested_volume_ul;
    uint32_t estimated_delivered_ul;
} PumpUnit_State_t;

/* Pump configuration */
typedef struct
{
    /* Calibrated flow rate (uL/s) */
    uint32_t flow_ul_per_s;

    /* Safety: maximum continuous run time (ms), 0 = unlimited */
    uint32_t max_run_time_ms;
} PumpUnit_Config_t;

/* Pump instance */
typedef struct
{
    GPIO_switch_t *sw;
    PumpUnit_SwitchOps_t ops;
    PumpUnit_Config_t cfg;
    PumpUnit_State_t state;
} PumpUnit_t;

/* Initialize pump instance (does not start it) */
uint8_t pump_unit_init(PumpUnit_t *pump, GPIO_switch_t *sw);

/* Bind switch operations */
uint8_t pump_unit_set_switch_ops(PumpUnit_t *pump, const PumpUnit_SwitchOps_t *ops);

/* Configure pump parameters */
uint8_t pump_unit_set_flow_ul_per_s(PumpUnit_t *pump, uint32_t flow_ul_per_s);
uint8_t pump_unit_set_max_run_time_ms(PumpUnit_t *pump, uint32_t max_run_time_ms);

/* Control: run for time */
uint8_t pump_unit_start_for_ms(PumpUnit_t *pump, uint32_t now_ms, uint32_t run_time_ms);

/* Control: dose by volume (uses flow_ul_per_s). Returns actual run time via out pointer (optional). */
uint8_t pump_unit_start_for_volume_ul(PumpUnit_t *pump,
                                      uint32_t now_ms,
                                      uint32_t volume_ul,
                                      uint32_t *actual_run_time_ms);

/* Stop immediately */
uint8_t pump_unit_stop(PumpUnit_t *pump);

/* Periodic processing (timeouts, accounting) */
void pump_unit_process(PumpUnit_t *pump, uint32_t now_ms);

/* Inline helpers */
static inline uint8_t pump_unit_is_running(const PumpUnit_t *pump)
{
    return (pump != NULL && pump->state.is_running != 0U) ? 1U : 0U;
}

static inline uint32_t pump_unit_get_estimated_delivered_ul(const PumpUnit_t *pump)
{
    return (pump != NULL) ? pump->state.estimated_delivered_ul : 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* PUMP_UNIT_H */
