#ifndef PUMP_GUARD_H
#define PUMP_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "pump_unit.h"

/* Why pump is blocked */
typedef enum
{
    PUMP_GUARD_BLOCK_NONE = 0,
    PUMP_GUARD_BLOCK_SENSOR_FAULT,
    PUMP_GUARD_BLOCK_LOW_VOLUME,
    PUMP_GUARD_BLOCK_STALE_LEVEL
} PumpGuard_BlockReason_t;

/* Level mapping callback: distance (mm) -> volume (uL) */
typedef uint32_t (*PumpGuard_VolumeMapFn)(void *ctx, uint32_t distance_mm);

typedef struct
{
    PumpUnit_t *pump;

    /* If level sensor is absent: set map_fn = NULL.
       In this mode guard never blocks due to level. */
    PumpGuard_VolumeMapFn map_fn;
    void *map_ctx;

    uint32_t min_volume_ul;

    /* If sensor exists and fault/no-data: block pump */
    uint8_t block_on_sensor_fault;

    /* If sensor exists: how long level can be considered valid */
    uint32_t level_stale_timeout_ms;
} PumpGuard_Config_t;

typedef struct
{
    uint8_t has_level;
    uint8_t sensor_fault;

    uint32_t last_distance_mm;
    uint32_t last_volume_ul;
    uint32_t last_update_ms;

    PumpGuard_BlockReason_t block_reason;
} PumpGuard_State_t;

typedef struct
{
    PumpGuard_Config_t cfg;
    PumpGuard_State_t state;
} PumpGuard_t;

/* Init guard */
uint8_t pump_guard_init(PumpGuard_t *guard, const PumpGuard_Config_t *cfg);

/* Update level measurement (call from your SR04M handling code) */
void pump_guard_update_distance_mm(PumpGuard_t *guard, uint32_t now_ms, uint32_t distance_mm);

/* Mark sensor fault (no data / error) */
void pump_guard_set_sensor_fault(PumpGuard_t *guard, uint32_t now_ms);

/* Clear sensor fault */
void pump_guard_clear_sensor_fault(PumpGuard_t *guard);

/* Check if pump is allowed to run right now */
uint8_t pump_guard_can_run(PumpGuard_t *guard, uint32_t now_ms);

/* Start/stop via guard (will check blocking) */
uint8_t pump_guard_start_for_ms(PumpGuard_t *guard, uint32_t now_ms, uint32_t run_time_ms);
uint8_t pump_guard_stop(PumpGuard_t *guard);

/* Process periodic checks */
void pump_guard_process(PumpGuard_t *guard, uint32_t now_ms);

/* Inline helper */
static inline PumpGuard_BlockReason_t pump_guard_get_block_reason(const PumpGuard_t *guard)
{
    return (guard != NULL) ? guard->state.block_reason : PUMP_GUARD_BLOCK_SENSOR_FAULT;
}

#ifdef __cplusplus
}
#endif

#endif /* PUMP_GUARD_H */
