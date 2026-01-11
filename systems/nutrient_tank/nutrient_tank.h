#ifndef NUTRIENT_TANK_H
#define NUTRIENT_TANK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "pump_guard.h"
#include "tank_sensors.h"
#include "recipe_controller.h"

#define NUTRIENT_TANK_NUTRIENT_MAX_PUMPS    4U

/* Tank level mapping: distance (mm) -> volume (uL) */
typedef uint32_t (*NutrientTank_VolumeMapFn)(void *ctx, uint32_t distance_mm);

typedef enum
{
    NUTRIENT_TANK_LEVEL_OK = 0,
    NUTRIENT_TANK_LEVEL_LOW,
    NUTRIENT_TANK_LEVEL_CRITICAL,
    NUTRIENT_TANK_LEVEL_HIGH
} NutrientTank_LevelState_t;

typedef struct
{
    /* If map_fn is NULL, level is treated as unavailable */
    NutrientTank_VolumeMapFn map_fn;
    void *map_ctx;

    uint32_t last_distance_mm;
    uint32_t last_volume_ul;
    uint32_t last_update_ms;

    uint8_t valid;
    uint8_t fault;

    /* 0 = do not check staleness */
    uint32_t stale_timeout_ms;
} NutrientTank_Level_t;

/* Hysteresis thresholds and policies for a tank */
typedef struct
{
    /* Main tank thresholds */
    uint32_t main_low_ul;
    uint32_t main_resume_ul;
    uint32_t main_critical_ul;
    uint32_t main_high_ul;

    /* Return tank thresholds */
    uint32_t return_request_ul;
    uint32_t return_resume_ul;

    /* Request return when main below this threshold */
    uint32_t main_request_return_ul;

    /* Block return/additions when main above this threshold */
    uint32_t main_block_return_ul;
} NutrientTank_LevelPolicy_t;

/* Stabilization timings after operations */
typedef struct
{
    /* After any dosing (water/nutrient/pH/return) run aeration and then settle */
    uint32_t after_dose_aerate_ms;
    uint32_t after_dose_settle_ms;

    /* After explicit aeration command */
    uint32_t after_aerate_settle_ms;
} NutrientTank_Timing_t;

typedef enum
{
    NUTRIENT_TANK_STATE_IDLE = 0,
    NUTRIENT_TANK_STATE_EXECUTING,
    NUTRIENT_TANK_STATE_AERATE_AFTER_DOSE,
    NUTRIENT_TANK_STATE_WAIT_SETTLE,
    NUTRIENT_TANK_STATE_ERROR,
    NUTRIENT_TANK_STATE_STOPPED
} NutrientTank_State_t;

typedef enum
{
    NUTRIENT_TANK_ERR_NONE = 0,
    NUTRIENT_TANK_ERR_INVALID_ARG,
    NUTRIENT_TANK_ERR_BUSY,
    NUTRIENT_TANK_ERR_PUMP_BLOCKED,
    NUTRIENT_TANK_ERR_SENSOR_FAULT,
    NUTRIENT_TANK_ERR_SENSOR_STALE,
    NUTRIENT_TANK_ERR_TIMEOUT
} NutrientTank_Error_t;

/* Events are used to inform external logic (MQTT/host/manager) */
typedef enum
{
    NUTRIENT_TANK_EVENT_NONE = 0,

    /* Level-related */
    NUTRIENT_TANK_EVENT_MAIN_LOW,
    NUTRIENT_TANK_EVENT_MAIN_CRITICAL,
    NUTRIENT_TANK_EVENT_MAIN_RESUMED,
    NUTRIENT_TANK_EVENT_RETURN_HIGH,

    /* Suggestions / requests */
    NUTRIENT_TANK_EVENT_REQUEST_RETURN,
    NUTRIENT_TANK_EVENT_REQUEST_REFILL,

    /* Control flow (reserved for closed-loop) */
    NUTRIENT_TANK_EVENT_CONTROL_DONE,
    NUTRIENT_TANK_EVENT_CONTROL_ERROR,

    /* Safety */
    NUTRIENT_TANK_EVENT_OPERATION_BLOCKED
} NutrientTank_EventType_t;

typedef struct
{
    NutrientTank_EventType_t type;

    /* Optional details */
    uint32_t main_volume_ul;
    uint32_t return_volume_ul;

    NutrientTank_Error_t error;
    PumpGuard_BlockReason_t block_reason;
} NutrientTank_Event_t;

/* Dosing targets for manual commands */
typedef enum
{
    NUTRIENT_TANK_DOSE_WATER = 0,
    NUTRIENT_TANK_DOSE_NUTRIENT,
    NUTRIENT_TANK_DOSE_PH_UP,
    NUTRIENT_TANK_DOSE_PH_DOWN,
    NUTRIENT_TANK_DOSE_DRAIN,
    NUTRIENT_TANK_DOSE_RETURN
} NutrientTank_DoseKind_t;

typedef enum
{
    NUTRIENT_TANK_CMD_NONE = 0,

    /* Manual */
    NUTRIENT_TANK_CMD_AERATE_FOR_MS,
    NUTRIENT_TANK_CMD_CIRCULATION_SET,
    NUTRIENT_TANK_CMD_DOSE_VOLUME,

    /* Closed-loop (not implemented in this step) */
    NUTRIENT_TANK_CMD_CONTROL_START,
    NUTRIENT_TANK_CMD_CONTROL_STOP,

    /* Safety */
    NUTRIENT_TANK_CMD_EMERGENCY_STOP
} NutrientTank_CommandType_t;

typedef struct
{
    NutrientTank_CommandType_t type;

    union
    {
        struct
        {
            uint32_t duration_ms;
        } aerate;

        struct
        {
            uint8_t enable;
        } circulation;

        struct
        {
            NutrientTank_DoseKind_t kind;

            /* Used only when kind == NUTRIENT_TANK_DOSE_NUTRIENT */
            uint8_t nutrient_index;

            uint32_t volume_ul;
        } dose;

        struct
        {
            uint8_t enable_ph;
            uint8_t enable_tds;

            int32_t target_ph_x1000;
            int32_t ph_tolerance_x1000;

            int32_t target_tds_ppm;
            int32_t tds_tolerance_ppm;
        } control;
    } p;
} NutrientTank_Command_t;

typedef struct
{
    /* Pumps (guards) */
    PumpGuard_t *water_in;
    PumpGuard_t *nutrients[NUTRIENT_TANK_NUTRIENT_MAX_PUMPS];
    uint8_t nutrient_count;

    PumpGuard_t *ph_up;
    PumpGuard_t *ph_down;

    PumpGuard_t *air;
    PumpGuard_t *circulation;

    PumpGuard_t *drain;
    PumpGuard_t *return_pump;

    /* Level sensors of tanks (optional, push-model) */
    NutrientTank_Level_t main_level;
    NutrientTank_Level_t return_level;

    NutrientTank_LevelPolicy_t level_policy;
    NutrientTank_Timing_t timing;

    /* pH/TDS/temperature aggregator (not used in this step) */
    TankSensors_t *sensors;

    /* Closed-loop logic (not executed in this step) */
    RecipeController_t *recipe;

    /* Event queue size is set by init() using event_buffer_len */
    uint8_t event_queue_size;
} NutrientTank_Config_t;

typedef struct
{
    NutrientTank_State_t state;
    NutrientTank_Error_t last_error;

    NutrientTank_LevelState_t main_level_state;
    NutrientTank_LevelState_t return_level_state;

    /* Circulation desired state (auto-stopped on low main level) */
    uint8_t circulation_requested;

    NutrientTank_Command_t active_cmd;
    uint8_t has_active_cmd;

    uint32_t state_started_at_ms;
    uint32_t wait_until_ms;

    /* Rising-edge tracking to avoid event spam */
    uint8_t request_return_active;
    uint8_t request_refill_active;

    uint8_t control_active;
    uint8_t control_generated_cmd;

    /* Simple ring-buffer for events */
    uint8_t ev_wr;
    uint8_t ev_rd;
} NutrientTank_StateData_t;

typedef struct
{
    NutrientTank_Config_t cfg;
    NutrientTank_StateData_t st;

    /* Event queue storage is external (user-provided) */
    NutrientTank_Event_t *events;
} NutrientTank_t;

/* Init / reset */
uint8_t nutrient_tank_init(NutrientTank_t *tank,
                           const NutrientTank_Config_t *cfg,
                           NutrientTank_Event_t *event_buffer,
                           uint8_t event_buffer_len);

void nutrient_tank_reset(NutrientTank_t *tank);

/* Periodic processing */
void nutrient_tank_process(NutrientTank_t *tank, uint32_t now_ms);

/* Commands */
uint8_t nutrient_tank_submit_command(NutrientTank_t *tank, const NutrientTank_Command_t *cmd);

/* Sensor inputs (push model) */
void nutrient_tank_update_main_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm);
void nutrient_tank_set_main_sensor_fault(NutrientTank_t *tank, uint32_t now_ms);

void nutrient_tank_update_return_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm);
void nutrient_tank_set_return_sensor_fault(NutrientTank_t *tank, uint32_t now_ms);

/* Events */
uint8_t nutrient_tank_pop_event(NutrientTank_t *tank, NutrientTank_Event_t *ev_out);

/* Stop everything immediately */
void nutrient_tank_emergency_stop(NutrientTank_t *tank);

static inline uint8_t nutrient_tank_is_control_active(const NutrientTank_t *tank)
{
    return (tank != NULL && tank->st.control_active != 0U) ? 1U : 0U;
}

static inline NutrientTank_State_t nutrient_tank_get_state(const NutrientTank_t *tank)
{
    return (tank != NULL) ? tank->st.state : NUTRIENT_TANK_STATE_ERROR;
}

static inline NutrientTank_Error_t nutrient_tank_get_last_error(const NutrientTank_t *tank)
{
    return (tank != NULL) ? tank->st.last_error : NUTRIENT_TANK_ERR_INVALID_ARG;
}

#ifdef __cplusplus
}
#endif

#endif /* NUTRIENT_TANK_H */
