#include "recipe_controller.h"
#include <string.h>

static uint32_t rc_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static uint8_t rc_is_nutrient_enabled(const RecipeController_t *rc, uint8_t idx)
{
    uint8_t mask;

    if (rc == NULL) {
        return 0U;
    }

    if (idx >= rc->cfg.nutrient_count) {
        return 0U;
    }

    mask = rc->cfg.nutrient_enable_mask;

    /* mask==0 means all enabled */
    if (mask == 0U) {
        return 1U;
    }

    return ((mask & (uint8_t)(1U << idx)) != 0U) ? 1U : 0U;
}

static uint8_t rc_pick_next_nutrient_index(RecipeController_t *rc, uint8_t *out_idx)
{
    uint8_t i;
    uint8_t start;

    if (rc == NULL || out_idx == NULL) {
        return 0U;
    }

    if (rc->cfg.nutrient_count == 0U || rc->cfg.nutrient_count > RECIPE_NUTRIENT_MAX_PUMPS) {
        return 0U;
    }

    start = (uint8_t)(rc->last_nutrient_index + 1U);

    for (i = 0U; i < rc->cfg.nutrient_count; i++) {
        uint8_t idx = (uint8_t)((start + i) % rc->cfg.nutrient_count);
        if (rc_is_nutrient_enabled(rc, idx) != 0U) {
            *out_idx = idx;
            rc->last_nutrient_index = idx;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t rc_can_dose(RecipeController_t *rc, uint32_t dose_ul)
{
    if (rc == NULL) {
        return 0U;
    }

    if (dose_ul == 0U) {
        return 0U;
    }

    if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
        return 0U;
    }

    if (rc->cfg.max_total_dose_ul != 0U) {
        if ((uint64_t)rc->total_dosed_ul + (uint64_t)dose_ul > (uint64_t)rc->cfg.max_total_dose_ul) {
            return 0U;
        }
    }

    return 1U;
}

uint8_t recipe_controller_init(RecipeController_t *rc, const RecipeController_Config_t *cfg)
{
    if (rc == NULL || cfg == NULL) {
        return 0U;
    }

    if (cfg->nutrient_count == 0U || cfg->nutrient_count > RECIPE_NUTRIENT_MAX_PUMPS) {
        return 0U;
    }

    memset(rc, 0, sizeof(*rc));
    rc->cfg = *cfg;
    rc->last_nutrient_index = 0xFFU;

    return 1U;
}

uint8_t recipe_controller_set_targets(RecipeController_t *rc, const RecipeController_Targets_t *targets)
{
    if (rc == NULL || targets == NULL) {
        return 0U;
    }

    rc->targets = *targets;
    return 1U;
}

void recipe_controller_start(RecipeController_t *rc)
{
    if (rc == NULL) {
        return;
    }

    rc->active = 1U;
    rc->error = 0U;
    rc->total_dosed_ul = 0U;
    rc->last_nutrient_index = 0xFFU;
}

void recipe_controller_stop(RecipeController_t *rc)
{
    if (rc == NULL) {
        return;
    }

    rc->active = 0U;
}

RecipeStep_t recipe_controller_next_step(RecipeController_t *rc,
                                        int32_t ph_x1000,
                                        int32_t tds_ppm,
                                        uint8_t sensors_fresh)
{
    RecipeStep_t step;
    uint8_t idx;
    uint32_t dose_ul;

    memset(&step, 0, sizeof(step));
    step.type = RECIPE_STEP_NONE;

    if (rc == NULL || rc->active == 0U) {
        return step;
    }

    if (rc->error != 0U) {
        step.type = RECIPE_STEP_ERROR;
        return step;
    }

    if (sensors_fresh == 0U) {
        return step;
    }

    /* TDS control first (nutrients + dilution) */
    if (rc->targets.enable_tds != 0U) {
        int32_t low_thr = rc->targets.target_tds_ppm - rc->targets.tds_tolerance_ppm;
        int32_t high_thr = rc->targets.target_tds_ppm + rc->targets.tds_tolerance_ppm;

        if (tds_ppm < low_thr) {

            dose_ul = rc->cfg.tds_nutrient_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_pick_next_nutrient_index(rc, &idx) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc_can_dose(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_NUTRIENT;
            step.nutrient_index = idx;
            step.dose_volume_ul = dose_ul;

            rc->total_dosed_ul += dose_ul;
            return step;
        }

        if (tds_ppm > high_thr) {

            dose_ul = rc->cfg.tds_water_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_WATER;
            step.nutrient_index = 0U;
            step.dose_volume_ul = dose_ul;

            rc->total_dosed_ul += dose_ul;
            return step;
        }
    }

    /* pH control after TDS */
    if (rc->targets.enable_ph != 0U) {
        int32_t low_thr = rc->targets.target_ph_x1000 - rc->targets.ph_tolerance_x1000;
        int32_t high_thr = rc->targets.target_ph_x1000 + rc->targets.ph_tolerance_x1000;

        if (ph_x1000 < low_thr) {

            dose_ul = rc->cfg.ph_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_PH_UP;
            step.dose_volume_ul = dose_ul;

            rc->total_dosed_ul += dose_ul;
            return step;
        }

        if (ph_x1000 > high_thr) {

            dose_ul = rc->cfg.ph_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_PH_DOWN;
            step.dose_volume_ul = dose_ul;

            rc->total_dosed_ul += dose_ul;
            return step;
        }
    }

    /* Everything in tolerance */
    step.type = RECIPE_STEP_DONE;
    return step;
}
