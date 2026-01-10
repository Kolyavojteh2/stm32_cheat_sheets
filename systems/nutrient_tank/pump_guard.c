#include "pump_guard.h"
#include <string.h>

static uint8_t pump_guard_has_level_sensor(const PumpGuard_t *guard)
{
    return (guard != NULL && guard->cfg.map_fn != NULL) ? 1U : 0U;
}

uint8_t pump_guard_init(PumpGuard_t *guard, const PumpGuard_Config_t *cfg)
{
    if (guard == NULL || cfg == NULL || cfg->pump == NULL) {
        return 0U;
    }

    memset(guard, 0, sizeof(*guard));
    guard->cfg = *cfg;

    guard->state.block_reason = PUMP_GUARD_BLOCK_NONE;

    return 1U;
}

void pump_guard_update_distance_mm(PumpGuard_t *guard, uint32_t now_ms, uint32_t distance_mm)
{
    uint32_t volume_ul;

    if (guard == NULL) {
        return;
    }

    if (pump_guard_has_level_sensor(guard) == 0U) {
        return;
    }

    volume_ul = guard->cfg.map_fn(guard->cfg.map_ctx, distance_mm);

    guard->state.sensor_fault = 0U;
    guard->state.last_distance_mm = distance_mm;
    guard->state.last_volume_ul = volume_ul;
    guard->state.last_update_ms = now_ms;
}

void pump_guard_set_sensor_fault(PumpGuard_t *guard, uint32_t now_ms)
{
    if (guard == NULL) {
        return;
    }

    if (pump_guard_has_level_sensor(guard) == 0U) {
        return;
    }

    guard->state.sensor_fault = 1U;
    guard->state.last_update_ms = now_ms;
}

void pump_guard_clear_sensor_fault(PumpGuard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    guard->state.sensor_fault = 0U;
}

uint8_t pump_guard_can_run(PumpGuard_t *guard, uint32_t now_ms)
{
    uint32_t age_ms;

    if (guard == NULL || guard->cfg.pump == NULL) {
        return 0U;
    }

    /* Default: allowed */
    guard->state.block_reason = PUMP_GUARD_BLOCK_NONE;

    /* If no level sensor is attached, do not block due to level */
    if (pump_guard_has_level_sensor(guard) == 0U) {
        return 1U;
    }

    /* Sensor fault blocks if configured */
    if (guard->state.sensor_fault != 0U && guard->cfg.block_on_sensor_fault != 0U) {
        guard->state.block_reason = PUMP_GUARD_BLOCK_SENSOR_FAULT;
        return 0U;
    }

    /* Stale level blocks if timeout configured */
    if (guard->cfg.level_stale_timeout_ms != 0U) {
        age_ms = (uint32_t)(now_ms - guard->state.last_update_ms);
        if (age_ms > guard->cfg.level_stale_timeout_ms) {
            guard->state.block_reason = PUMP_GUARD_BLOCK_STALE_LEVEL;
            return 0U;
        }
    }

    /* Low volume check */
    if (guard->state.last_volume_ul < guard->cfg.min_volume_ul) {
        guard->state.block_reason = PUMP_GUARD_BLOCK_LOW_VOLUME;
        return 0U;
    }

    return 1U;
}

uint8_t pump_guard_start_for_ms(PumpGuard_t *guard, uint32_t now_ms, uint32_t run_time_ms)
{
    if (guard == NULL || guard->cfg.pump == NULL) {
        return 0U;
    }

    if (pump_guard_can_run(guard, now_ms) == 0U) {
        return 0U;
    }

    return pump_unit_start_for_ms(guard->cfg.pump, now_ms, run_time_ms);
}

uint8_t pump_guard_start_for_volume_ul(PumpGuard_t *guard,
                                       uint32_t now_ms,
                                       uint32_t volume_ul,
                                       uint32_t *actual_run_time_ms)
{
    if (guard == NULL || guard->cfg.pump == NULL) {
        return 0U;
    }

    if (pump_guard_can_run(guard, now_ms) == 0U) {
        return 0U;
    }

    return pump_unit_start_for_volume_ul(guard->cfg.pump, now_ms, volume_ul, actual_run_time_ms);
}

uint8_t pump_guard_stop(PumpGuard_t *guard)
{
    if (guard == NULL || guard->cfg.pump == NULL) {
        return 0U;
    }

    return pump_unit_stop(guard->cfg.pump);
}

void pump_guard_process(PumpGuard_t *guard, uint32_t now_ms)
{
    if (guard == NULL || guard->cfg.pump == NULL) {
        return;
    }

    /* Always process pump internal timing */
    pump_unit_process(guard->cfg.pump, now_ms);

    /* If running and became blocked, stop immediately */
    if (pump_unit_is_running(guard->cfg.pump) != 0U) {
        if (pump_guard_can_run(guard, now_ms) == 0U) {
            (void)pump_unit_stop(guard->cfg.pump);
        }
    }
}
