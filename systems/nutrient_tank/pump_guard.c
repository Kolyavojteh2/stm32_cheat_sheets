#include "pump_guard.h"
#include <string.h>

uint8_t pump_guard_init(PumpGuard_t *guard, const PumpGuard_Config_t *cfg)
{
    if (guard == NULL || cfg == NULL || cfg->pump == NULL) {
        return 0U;
    }

    memset(guard, 0, sizeof(*guard));
    guard->cfg = *cfg;

    return 1U;
}

void pump_guard_update_distance_mm(PumpGuard_t *guard, uint32_t now_ms, uint32_t distance_mm)
{
    (void)now_ms;
    (void)distance_mm;

    if (guard == NULL) {
        return;
    }

    /* Skeleton */
}

void pump_guard_set_sensor_fault(PumpGuard_t *guard, uint32_t now_ms)
{
    (void)now_ms;

    if (guard == NULL) {
        return;
    }

    /* Skeleton */
}

void pump_guard_clear_sensor_fault(PumpGuard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    /* Skeleton */
}

uint8_t pump_guard_can_run(PumpGuard_t *guard, uint32_t now_ms)
{
    (void)now_ms;

    if (guard == NULL) {
        return 0U;
    }

    /* Skeleton: allow by default */
    guard->state.block_reason = PUMP_GUARD_BLOCK_NONE;
    return 1U;
}

uint8_t pump_guard_start_for_ms(PumpGuard_t *guard, uint32_t now_ms, uint32_t run_time_ms)
{
    if (guard == NULL) {
        return 0U;
    }

    if (pump_guard_can_run(guard, now_ms) == 0U) {
        return 0U;
    }

    return pump_unit_start_for_ms(guard->cfg.pump, now_ms, run_time_ms);
}

uint8_t pump_guard_stop(PumpGuard_t *guard)
{
    if (guard == NULL) {
        return 0U;
    }

    return pump_unit_stop(guard->cfg.pump);
}

void pump_guard_process(PumpGuard_t *guard, uint32_t now_ms)
{
    (void)guard;
    (void)now_ms;

    /* Skeleton */
}
