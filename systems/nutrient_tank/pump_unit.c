#include "pump_unit.h"
#include <string.h>

/* Convert volume (uL) to run time (ms) using flow (uL/s).
   Uses integer math with rounding up to ensure at least requested volume. */
static uint32_t pump_unit_volume_to_time_ms(uint32_t volume_ul, uint32_t flow_ul_per_s)
{
    uint64_t numerator;

    if (flow_ul_per_s == 0U) {
        return 0U;
    }

    numerator = ((uint64_t) volume_ul) * 1000ULL;

    /* Ceil division */
    return (uint32_t) ((numerator + (uint64_t)flow_ul_per_s - 1ULL) / (uint64_t)flow_ul_per_s);
}

uint8_t pump_unit_init(PumpUnit_t *pump, GPIO_switch_t *sw)
{
    if (pump == NULL || sw == NULL) {
        return 0U;
    }

    memset(pump, 0, sizeof(*pump));
    pump->sw = sw;

    return 1U;
}

uint8_t pump_unit_set_switch_ops(PumpUnit_t *pump, const PumpUnit_SwitchOps_t *ops)
{
    if (pump == NULL || ops == NULL) {
        return 0U;
    }

    if (ops->on == NULL || ops->off == NULL) {
        return 0U;
    }

    pump->ops = *ops;
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
    uint32_t effective_run_time_ms;

    if (pump == NULL || pump->sw == NULL) {
        return 0U;
    }

    if (pump->ops.on == NULL || pump->ops.off == NULL) {
        return 0U;
    }

    if (run_time_ms == 0U) {
        return 0U;
    }

    effective_run_time_ms = run_time_ms;

    /* Apply safety max runtime if configured */
    if (pump->cfg.max_run_time_ms != 0U && effective_run_time_ms > pump->cfg.max_run_time_ms) {
        effective_run_time_ms = pump->cfg.max_run_time_ms;
    }

    /* Start switch first; if it fails, do not change state */
    if (pump->ops.on(pump->sw) == 0U) {
        return 0U;
    }

    pump->state.is_running = 1U;
    pump->state.started_at_ms = now_ms;
    pump->state.requested_run_time_ms = effective_run_time_ms;

    pump->state.requested_volume_ul = 0U;
    pump->state.estimated_delivered_ul = 0U;

    return 1U;
}

uint8_t pump_unit_start_for_volume_ul(PumpUnit_t *pump,
                                      uint32_t now_ms,
                                      uint32_t volume_ul,
                                      uint32_t *actual_run_time_ms)
{
    uint32_t run_time_ms;

    if (pump == NULL) {
        return 0U;
    }

    if (pump->cfg.flow_ul_per_s == 0U) {
        return 0U;
    }

    if (volume_ul == 0U) {
        return 0U;
    }

    run_time_ms = pump_unit_volume_to_time_ms(volume_ul, pump->cfg.flow_ul_per_s);

    if (pump_unit_start_for_ms(pump, now_ms, run_time_ms) == 0U) {
        return 0U;
    }

    pump->state.requested_volume_ul = volume_ul;

    if (actual_run_time_ms != NULL) {
        *actual_run_time_ms = pump->state.requested_run_time_ms;
    }

    return 1U;
}

uint8_t pump_unit_stop(PumpUnit_t *pump)
{
    if (pump == NULL || pump->sw == NULL) {
        return 0U;
    }

    if (pump->ops.on == NULL || pump->ops.off == NULL) {
        return 0U;
    }

    /* Stop switch first; still update state only if hardware stop succeeded */
    if (pump->ops.off(pump->sw) == 0U) {
        return 0U;
    }

    pump->state.is_running = 0U;
    pump->state.requested_run_time_ms = 0U;

    return 1U;
}

void pump_unit_process(PumpUnit_t *pump, uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint64_t delivered_ul;

    if (pump == NULL) {
        return;
    }

    if (pump->state.is_running == 0U) {
        return;
    }

    elapsed_ms = (uint32_t)(now_ms - pump->state.started_at_ms);

    /* Estimate delivered volume (uL) from elapsed time */
    if (pump->cfg.flow_ul_per_s != 0U) {
        delivered_ul = ((uint64_t)pump->cfg.flow_ul_per_s * (uint64_t)elapsed_ms) / 1000ULL;
        if (delivered_ul > 0xFFFFFFFFULL) {
            delivered_ul = 0xFFFFFFFFULL;
        }
        pump->state.estimated_delivered_ul = (uint32_t)delivered_ul;
    }

    /* Stop when requested time elapsed */
    if (elapsed_ms >= pump->state.requested_run_time_ms) {
        (void)pump_unit_stop(pump);
        return;
    }

    /* Safety: stop if max continuous time exceeded */
    if (pump->cfg.max_run_time_ms != 0U && elapsed_ms >= pump->cfg.max_run_time_ms) {
        (void)pump_unit_stop(pump);
        return;
    }
}
