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

static uint16_t rc_get_weight(const RecipeController_t *rc, uint8_t idx)
{
    uint32_t sum_ratio = 0U;
    uint32_t sum_parts = 0U;
    uint8_t i;

    if (rc == NULL) {
        return 0U;
    }

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        if (rc_is_nutrient_enabled(rc, i) != 0U) {
            sum_ratio += (uint32_t)rc->cfg.nutrient_ratio[i];
            sum_parts += (uint32_t)rc->cfg.nutrient_parts_per_l[i];
        }
    }

    if (sum_ratio > 0U) {
        return rc->cfg.nutrient_ratio[idx];
    }

    if (sum_parts > 0U) {
        return rc->cfg.nutrient_parts_per_l[idx];
    }

    /* Default: equal weights */
    return 1U;
}

static uint32_t rc_sum_weights(const RecipeController_t *rc)
{
    uint32_t sum = 0U;
    uint8_t i;

    if (rc == NULL) {
        return 0U;
    }

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        if (rc_is_nutrient_enabled(rc, i) != 0U) {
            sum += (uint32_t)rc_get_weight(rc, i);
        }
    }

    return sum;
}

/* Compute nutrient step in uL for current tank volume using per-liter settings.
   Returns 0 if per-liter not configured or volume unknown. */
static uint32_t rc_calc_nutrient_step_ul(const RecipeController_t *rc, uint32_t main_volume_ul)
{
    uint64_t step_ul_per_l = 0ULL;
    uint64_t scaled_ul;

    if (rc == NULL) {
        return 0U;
    }

    if (main_volume_ul == 0U) {
        return 0U;
    }

    /* Derive per-liter step from "parts per liter" and part volume if possible */
    if (rc->cfg.nutrient_part_volume_ul != 0U) {
        uint32_t sum_parts = 0U;
        uint8_t i;

        for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
            if (rc_is_nutrient_enabled(rc, i) != 0U) {
                sum_parts += (uint32_t)rc->cfg.nutrient_parts_per_l[i];
            }
        }

        if (sum_parts > 0U) {
            step_ul_per_l = (uint64_t)rc->cfg.nutrient_part_volume_ul * (uint64_t)sum_parts;

            if (rc->cfg.tds_nutrient_step_portion_x1000 != 0U) {
                step_ul_per_l = (step_ul_per_l * (uint64_t)rc->cfg.tds_nutrient_step_portion_x1000 + 999ULL) / 1000ULL;
            }
        }
    }

    /* Direct per-liter step overrides if provided */
    if (rc->cfg.tds_nutrient_step_ul_per_l != 0U) {
        step_ul_per_l = (uint64_t)rc->cfg.tds_nutrient_step_ul_per_l;
    }

    if (step_ul_per_l == 0ULL) {
        return 0U;
    }

    /* Scale: uL = (uL/L) * (volume_uL / 1,000,000 uL per L) with ceil */
    scaled_ul = step_ul_per_l * (uint64_t)main_volume_ul;
    scaled_ul = (scaled_ul + 1000000ULL - 1ULL) / 1000000ULL;

    if (scaled_ul > 0xFFFFFFFFULL) {
        scaled_ul = 0xFFFFFFFFULL;
    }

    return (uint32_t)scaled_ul;
}

static uint32_t rc_calc_water_step_ul(const RecipeController_t *rc, uint32_t main_volume_ul)
{
    uint64_t step_ul_per_l = 0ULL;
    uint64_t scaled_ul;

    if (rc == NULL) {
        return 0U;
    }

    if (rc->cfg.tds_water_step_ul_per_l == 0U || main_volume_ul == 0U) {
        return 0U;
    }

    step_ul_per_l = (uint64_t)rc->cfg.tds_water_step_ul_per_l;

    scaled_ul = step_ul_per_l * (uint64_t)main_volume_ul;
    scaled_ul = (scaled_ul + 1000000ULL - 1ULL) / 1000000ULL;

    if (scaled_ul > 0xFFFFFFFFULL) {
        scaled_ul = 0xFFFFFFFFULL;
    }

    return (uint32_t)scaled_ul;
}

static uint8_t rc_can_dose_planned(const RecipeController_t *rc, uint32_t dose_ul)
{
    if (rc == NULL || dose_ul == 0U) {
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

static void rc_clear_mix(RecipeController_t *rc)
{
    uint8_t i;

    if (rc == NULL) {
        return;
    }

    rc->mix_active = 0U;
    rc->mix_next_index = 0U;

    for (i = 0U; i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        rc->mix_remaining_ul[i] = 0U;
    }
}

/* Build pending mix plan:
   total_ul is split by weights across enabled nutrients.
   Remaining amounts are stored in mix_remaining_ul[]. */
static uint8_t rc_build_mix_plan(RecipeController_t *rc, uint32_t total_ul)
{
    uint32_t sum_w;
    uint32_t assigned = 0U;
    uint32_t remainder;
    uint8_t i;

    if (rc == NULL) {
        return 0U;
    }

    rc_clear_mix(rc);

    if (total_ul == 0U) {
        return 0U;
    }

    sum_w = rc_sum_weights(rc);
    if (sum_w == 0U) {
        return 0U;
    }

    /* First pass: floor distribution */
    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        uint16_t w;

        if (rc_is_nutrient_enabled(rc, i) == 0U) {
            rc->mix_remaining_ul[i] = 0U;
            continue;
        }

        w = rc_get_weight(rc, i);
        if (w == 0U) {
            rc->mix_remaining_ul[i] = 0U;
            continue;
        }

        rc->mix_remaining_ul[i] = (uint32_t)(((uint64_t)total_ul * (uint64_t)w) / (uint64_t)sum_w);
        assigned += rc->mix_remaining_ul[i];
    }

    /* Second pass: distribute remainder by walking enabled nutrients (stable and simple) */
    remainder = (total_ul > assigned) ? (total_ul - assigned) : 0U;
    while (remainder > 0U) {
        uint8_t progressed = 0U;

        for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
            if (remainder == 0U) {
                break;
            }
            if (rc_is_nutrient_enabled(rc, i) != 0U && rc_get_weight(rc, i) != 0U) {
                rc->mix_remaining_ul[i] += 1U;
                remainder--;
                progressed = 1U;
            }
        }

        if (progressed == 0U) {
            break;
        }
    }

    rc->mix_active = 1U;
    rc->mix_next_index = 0U;

    return 1U;
}

/* Get next chunk from pending mix (round-robin across channels).
   Applies max_single_dose_ul by chunking. */
static uint8_t rc_next_mix_dose(RecipeController_t *rc, uint8_t *out_idx, uint32_t *out_ul)
{
    uint8_t i;
    uint8_t start;
    uint32_t max_chunk;

    if (rc == NULL || out_idx == NULL || out_ul == NULL) {
        return 0U;
    }

    if (rc->mix_active == 0U) {
        return 0U;
    }

    max_chunk = rc->cfg.max_single_dose_ul;
    start = rc->mix_next_index;

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        uint8_t idx = (uint8_t)((start + i) % rc->cfg.nutrient_count);

        if (rc_is_nutrient_enabled(rc, idx) == 0U) {
            continue;
        }

        if (rc->mix_remaining_ul[idx] == 0U) {
            continue;
        }

        *out_idx = idx;

        if (max_chunk != 0U) {
            *out_ul = rc_min_u32(rc->mix_remaining_ul[idx], max_chunk);
        } else {
            *out_ul = rc->mix_remaining_ul[idx];
        }

        rc->mix_remaining_ul[idx] -= *out_ul;
        rc->mix_next_index = (uint8_t)((idx + 1U) % rc->cfg.nutrient_count);

        return 1U;
    }

    /* Nothing left */
    rc_clear_mix(rc);
    return 0U;
}

static uint8_t rc_mix_has_remaining(const RecipeController_t *rc)
{
    uint8_t i;

    if (rc == NULL || rc->mix_active == 0U) {
        return 0U;
    }

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        if (rc->mix_remaining_ul[i] != 0U) {
            return 1U;
        }
    }

    return 0U;
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
    rc_clear_mix(rc);

    /* Default portion scale to 1.0 if not provided but part-based config is used */
    if (rc->cfg.tds_nutrient_step_portion_x1000 == 0U) {
        rc->cfg.tds_nutrient_step_portion_x1000 = 1000U;
    }

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

    rc_clear_mix(rc);
}

void recipe_controller_stop(RecipeController_t *rc)
{
    if (rc == NULL) {
        return;
    }

    rc->active = 0U;
    rc_clear_mix(rc);
}

RecipeStep_t recipe_controller_next_step(RecipeController_t *rc,
                                        int32_t ph_x1000,
                                        int32_t tds_ppm,
                                        uint8_t sensors_fresh,
                                        uint32_t main_volume_ul)
{
    RecipeStep_t step;
    uint8_t idx;
    uint32_t dose_ul;

    int32_t tds_low_thr;
    int32_t tds_high_thr;
    int32_t ph_low_thr;
    int32_t ph_high_thr;

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

    tds_low_thr = rc->targets.target_tds_ppm - rc->targets.tds_tolerance_ppm;
    tds_high_thr = rc->targets.target_tds_ppm + rc->targets.tds_tolerance_ppm;

    ph_low_thr = rc->targets.target_ph_x1000 - rc->targets.ph_tolerance_x1000;
    ph_high_thr = rc->targets.target_ph_x1000 + rc->targets.ph_tolerance_x1000;

    /* If we have an active mix but TDS already above high threshold -> cancel remaining mix */
    if (rc->mix_active != 0U && rc->targets.enable_tds != 0U) {
        if (tds_ppm > tds_high_thr) {
            rc_clear_mix(rc);
        }
    }

    /* Continue pending nutrient mix first (keeps proportions across channels) */
    if (rc_mix_has_remaining(rc) != 0U) {
        if (rc_next_mix_dose(rc, &idx, &dose_ul) != 0U) {
            if (rc_can_dose_planned(rc, dose_ul) == 0U) {
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
    }

    /* TDS control: build new mix or add dilution water */
    if (rc->targets.enable_tds != 0U) {

        if (tds_ppm < tds_low_thr) {

            /* Prefer per-liter step; fallback to legacy absolute step */
            dose_ul = rc_calc_nutrient_step_ul(rc, main_volume_ul);
            if (dose_ul == 0U) {
                dose_ul = rc->cfg.tds_nutrient_step_ul;
            }

            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            /* Build a proportional mix plan and return first chunk */
            if (rc_build_mix_plan(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc_next_mix_dose(rc, &idx, &dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc_can_dose_planned(rc, dose_ul) == 0U) {
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

        if (tds_ppm > tds_high_thr) {

            dose_ul = rc_calc_water_step_ul(rc, main_volume_ul);
            if (dose_ul == 0U) {
                dose_ul = rc->cfg.tds_water_step_ul;
            }

            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose_planned(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_WATER;
            step.dose_volume_ul = dose_ul;

            rc->total_dosed_ul += dose_ul;
            return step;
        }
    }

    /* pH control after TDS */
    if (rc->targets.enable_ph != 0U) {

        if (ph_x1000 < ph_low_thr) {

            dose_ul = rc->cfg.ph_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose_planned(rc, dose_ul) == 0U) {
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

        if (ph_x1000 > ph_high_thr) {

            dose_ul = rc->cfg.ph_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U) {
                dose_ul = rc_min_u32(dose_ul, rc->cfg.max_single_dose_ul);
            }

            if (rc_can_dose_planned(rc, dose_ul) == 0U) {
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
