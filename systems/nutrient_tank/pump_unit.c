#include "pump_unit.h"
#include <string.h>

uint8_t pump_unit_init(PumpUnit_t *pump, GPIO_switch_t *sw)
{
    if (pump == NULL || sw == NULL) {
        return 0U;
    }

    memset(pump, 0, sizeof(*pump));
    pump->sw = sw;

    return 1U;
}

uint8_t pump_unit_set_flow_ul_per_s(PumpUnit_t *pump, uint32_t flow_ul_per_s)
{
    if (pump == NULL || flow_ul_per_s == 0U) {
        return 0U;
    }

    pump->cfg.flow_ul_per_s = flow_ul_per_s;
    return 1U;
}

uint8_t pump_unit_set_max_run_time_ms(PumpUnit_t *pump, uint32_t max_run_time_ms)
{
    if (pump == NULL) {
        return 0U;
    }

    pump->cfg.max_run_time_ms = max_run_time_ms;
    return 1U;
}

uint8_t pump_unit_start_for_ms(PumpUnit_t *pump, uint32_t now_ms, uint32_t run_time_ms)
{
    (void)now_ms;
    (void)run_time_ms;

    if (pump == NULL) {
        return 0U;
    }

    /* Skeleton: no hardware actions yet */
    return 1U;
}

uint8_t pump_unit_stop(PumpUnit_t *pump)
{
    if (pump == NULL) {
        return 0U;
    }

    /* Skeleton: no hardware actions yet */
    return 1U;
}

void pump_unit_process(PumpUnit_t *pump, uint32_t now_ms)
{
    (void)pump;
    (void)now_ms;

    /* Skeleton */
}
