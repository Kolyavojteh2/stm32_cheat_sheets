#include "nutrient_tank.h"
#include <string.h>

uint8_t nutrient_tank_init(NutrientTank_t *tank, const NutrientTank_Config_t *cfg)
{
    if (tank == NULL || cfg == NULL) {
        return 0U;
    }

    memset(tank, 0, sizeof(*tank));
    tank->cfg = *cfg;

    tank->st.state = NUTRIENT_TANK_STATE_IDLE;
    tank->st.last_error = NUTRIENT_TANK_ERR_NONE;

    return 1U;
}

void nutrient_tank_reset(NutrientTank_t *tank)
{
    if (tank == NULL) {
        return;
    }

    tank->st.state = NUTRIENT_TANK_STATE_IDLE;
    tank->st.last_error = NUTRIENT_TANK_ERR_NONE;
    tank->st.has_active_cmd = 0U;
    memset(&tank->st.active_cmd, 0, sizeof(tank->st.active_cmd));
}

void nutrient_tank_process(NutrientTank_t *tank, uint32_t now_ms)
{
    (void)tank;
    (void)now_ms;

    /* Skeleton */
}

uint8_t nutrient_tank_submit_command(NutrientTank_t *tank, const NutrientTank_Command_t *cmd)
{
    if (tank == NULL || cmd == NULL) {
        return 0U;
    }

    /* Skeleton: accept by default */
    tank->st.active_cmd = *cmd;
    tank->st.has_active_cmd = 1U;
    return 1U;
}

void nutrient_tank_update_main_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm)
{
    (void)now_ms;
    (void)distance_mm;

    if (tank == NULL) {
        return;
    }

    /* Skeleton */
}

void nutrient_tank_set_main_sensor_fault(NutrientTank_t *tank, uint32_t now_ms)
{
    (void)now_ms;

    if (tank == NULL) {
        return;
    }

    /* Skeleton */
}

void nutrient_tank_update_return_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm)
{
    (void)now_ms;
    (void)distance_mm;

    if (tank == NULL) {
        return;
    }

    /* Skeleton */
}

void nutrient_tank_set_return_sensor_fault(NutrientTank_t *tank, uint32_t now_ms)
{
    (void)now_ms;

    if (tank == NULL) {
        return;
    }

    /* Skeleton */
}

void nutrient_tank_emergency_stop(NutrientTank_t *tank)
{
    if (tank == NULL) {
        return;
    }

    /* Skeleton */
}
