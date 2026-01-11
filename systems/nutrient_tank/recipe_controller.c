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

static uint32_t rc_sum_ratio(const RecipeController_t *rc)
{
    uint32_t sum = 0U;
    uint8_t i;

    if (rc == NULL) {
        return 0U;
    }

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        if (rc_is_nutrient_enabled(rc, i) != 0U) {
            sum += (uint32_t)rc->cfg.nutrient_ratio[i];
        }
    }

    return sum;
}

static uint32_t rc_sum_parts(const RecipeController_t *rc)
{
    uint32_t sum = 0U;
    uint8_t i;

    if (rc == NULL) {
        return 0U;
    }

    for (i = 0U; i < rc->cfg.nutrient_count && i < RECIPE_NUTRIENT_MAX_PUMPS; i++) {
        if (rc_is_nutrient_enabled(rc, i) != 0U) {
            sum += (uint32_t)rc->cfg.nutrient_parts_per_l[i];
        }
    }

    return sum;
}

static uint16_t rc_get_weight(const RecipeController_t *rc, uint8_t idx)
{
    uint32_t sum_ratio;
    uint32_t sum_parts;

    if (rc == NULL || idx >= rc->cfg.nutrient_count) {
        return 0U;
    }

    sum_ratio = rc_sum_ratio(rc);
    sum_parts = rc_sum_parts(rc);

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

static uint16_t rc_clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static uint16_t rc_calc_portion_x1000(uint32_t err_ppm,
                                      uint16_t err_full_ppm,
                                      uint16_t portion_min_x1000,
                                      uint16_t portion_max_x1000)
{
    uint32_t span;
    uint32_t portion;

    if (portion_min_x1000 == 0U && portion_max_x1000 == 0U) {
        return 1000U;
    }

    if (portion_min_x1000 == 0U) {
        portion_min_x1000 = 1U;
    }
    if (portion_max_x1000 == 0U) {
        portion_max_x1000 = 1000U;
    }
    if (portion_min_x1000 > portion_max_x1000) {
        uint16_t t = portion_min_x1000;
        portion_min_x1000 = portion_max_x1000;
        portion_max_x1000 = t;
    }

    if (err_full_ppm == 0U) {
        return portion_max_x1000;
    }

    if (err_ppm >= (uint32_t)err_full_ppm) {
        return portion_max_x1000;
    }

    span = (uint32_t)portion_max_x1000 - (uint32_t)portion_min_x1000;
    portion = (uint32_t)portion_min_x1000 + (err_ppm * span) / (uint32_t)err_full_ppm;

    return rc_clamp_u16((uint16_t)portion, portion_min_x1000, portion_max_x1000);
}

static uint8_t rc_can_commit_total(const RecipeController_t *rc, uint32_t dose_ul)
{
    if (rc == NULL) {
        return 0U;
    }

    if (dose_ul == 0U) {
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

static void rc_clear_inflight(RecipeController_t *rc)
{
    if (rc == NULL) {
        return;
    }

    rc->inflight_active = 0U;
    rc->inflight_is_mix = 0U;
    rc->inflight_kind = RECIPE_DOSE_NONE;
    rc->inflight_nutrient_index = 0U;
    rc->inflight_volume_ul = 0U;
}

/* Get "full dose per liter" from parts if configured */
static uint32_t rc_calc_full_nutrient_per_l_ul_from_parts(const RecipeController_t *rc)
{
    uint32_t sum_parts;

    if (rc == NULL) {
        return 0U;
    }

    if (rc->cfg.nutrient_part_volume_ul == 0U) {
        return 0U;
    }

    sum_parts = rc_sum_parts(rc);
    if (sum_parts == 0U) {
        return 0U;
    }

    if (sum_parts > (0xFFFFFFFFUL / rc->cfg.nutrient_part_volume_ul)) {
        return 0xFFFFFFFFUL;
    }

    return rc->cfg.nutrient_part_volume_ul * sum_parts;
}

static uint32_t rc_scale_ul_per_l(uint32_t base_ul_per_l, uint16_t portion_x1000)
{
    uint64_t v;

    if (base_ul_per_l == 0U) {
        return 0U;
    }
    if (portion_x1000 == 0U) {
        portion_x1000 = 1U;
    }

    v = (uint64_t)base_ul_per_l * (uint64_t)portion_x1000;
    v = (v + 999ULL) / 1000ULL;

    if (v > 0xFFFFFFFFULL) {
        v = 0xFFFFFFFFULL;
    }

    return (uint32_t)v;
}

/* Scale per-liter value to total uL for current volume with ceil rounding */
static uint32_t rc_per_l_to_total_ul(uint32_t ul_per_l, uint32_t main_volume_ul)
{
    uint64_t v;

    if (ul_per_l == 0U || main_volume_ul == 0U) {
        return 0U;
    }

    v = (uint64_t)ul_per_l * (uint64_t)main_volume_ul;
    v = (v + 1000000ULL - 1ULL) / 1000000ULL;

    if (v > 0xFFFFFFFFULL) {
        v = 0xFFFFFFFFULL;
    }

    return (uint32_t)v;
}

static uint32_t rc_calc_nutrient_step_ul(const RecipeController_t *rc,
                                        uint32_t main_volume_ul,
                                        uint16_t portion_x1000)
{
    uint32_t base_per_l_ul;
    uint32_t total_ul;

    if (rc == NULL) {
        return 0U;
    }

    base_per_l_ul = 0U;

    /* Prefer explicit per-liter step, else derive from parts+part_volume */
    if (rc->cfg.tds_nutrient_step_ul_per_l != 0U) {
        base_per_l_ul = rc->cfg.tds_nutrient_step_ul_per_l;
    } else {
        base_per_l_ul = rc_calc_full_nutrient_per_l_ul_from_parts(rc);
    }

    if (base_per_l_ul != 0U && main_volume_ul != 0U) {
        base_per_l_ul = rc_scale_ul_per_l(base_per_l_ul, portion_x1000);
        total_ul = rc_per_l_to_total_ul(base_per_l_ul, main_volume_ul);
        return total_ul;
    }

    /* Fallback to absolute step (also scaled by portion) */
    if (rc->cfg.tds_nutrient_step_ul != 0U) {
        uint64_t v = (uint64_t)rc->cfg.tds_nutrient_step_ul * (uint64_t)portion_x1000;
        v = (v + 999ULL) / 1000ULL;
        if (v > 0xFFFFFFFFULL) {
            v = 0xFFFFFFFFULL;
        }
        return (uint32_t)v;
    }

    return 0U;
}

static uint32_t rc_calc_water_step_ul(const RecipeController_t *rc,
                                     uint32_t main_volume_ul,
                                     uint16_t portion_x1000)
{
    uint32_t base_per_l_ul;
    uint32_t total_ul;

    if (rc == NULL) {
        return 0U;
    }

    base_per_l_ul = rc->cfg.tds_water_step_ul_per_l;

    if (base_per_l_ul != 0U && main_volume_ul != 0U) {
        base_per_l_ul = rc_scale_ul_per_l(base_per_l_ul, portion_x1000);
        total_ul = rc_per_l_to_total_ul(base_per_l_ul, main_volume_ul);
        return total_ul;
    }

    if (rc->cfg.tds_water_step_ul != 0U) {
        uint64_t v = (uint64_t)rc->cfg.tds_water_step_ul * (uint64_t)portion_x1000;
        v = (v + 999ULL) / 1000ULL;
        if (v > 0xFFFFFFFFULL) {
            v = 0xFFFFFFFFULL;
        }
        return (uint32_t)v;
    }

    return 0U;
}

/* Build pending mix plan for ONE correction step (total_ul split by weights) */
static uint8_t rc_build_mix_plan(RecipeController_t *rc, uint32_t total_ul)
{
    uint32_t sum_w;
    uint32_t assigned = 0U;
    uint32_t remainder;
    uint8_t i;

    if (rc == NULL || total_ul == 0U) {
        return 0U;
    }

    rc_clear_mix(rc);

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

/* Peek next mix dose WITHOUT modifying remaining (will be committed on success) */
static uint8_t rc_peek_next_mix_dose(const RecipeController_t *rc, uint8_t *out_idx, uint32_t *out_ul)
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

        return 1U;
    }

    return 0U;
}

static void rc_advance_mix_cursor(RecipeController_t *rc, uint8_t used_idx)
{
    if (rc == NULL) {
        return;
    }

    if (rc->cfg.nutrient_count == 0U) {
        return;
    }

    rc->mix_next_index = (uint8_t)((used_idx + 1U) % rc->cfg.nutrient_count);
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
    rc_clear_inflight(rc);

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
    rc_clear_inflight(rc);
}

void recipe_controller_stop(RecipeController_t *rc)
{
    if (rc == NULL) {
        return;
    }

    rc->active = 0U;
    rc_clear_mix(rc);
    rc_clear_inflight(rc);
}

RecipeStep_t recipe_controller_next_step(RecipeController_t *rc,
                                        int32_t ph_x1000,
                                        int32_t tds_ppm,
                                        uint8_t sensors_fresh,
                                        uint32_t main_volume_ul)
{
    RecipeStep_t step;

    int32_t tds_low_thr;
    int32_t tds_high_thr;
    int32_t ph_low_thr;
    int32_t ph_high_thr;

    uint32_t err_ppm;
    uint16_t portion_x1000;
    uint32_t dose_ul;
    uint8_t idx;

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

    /* Do not generate new steps while previous step isn't acknowledged */
    if (rc->inflight_active != 0U) {
        return step;
    }

    tds_low_thr = rc->targets.target_tds_ppm - rc->targets.tds_tolerance_ppm;
    tds_high_thr = rc->targets.target_tds_ppm + rc->targets.tds_tolerance_ppm;

    ph_low_thr = rc->targets.target_ph_x1000 - rc->targets.ph_tolerance_x1000;
    ph_high_thr = rc->targets.target_ph_x1000 + rc->targets.ph_tolerance_x1000;

    /* If there is a pending mix but TDS already above upper threshold -> cancel remainder */
    if (rc->mix_active != 0U && rc->targets.enable_tds != 0U) {
        if (tds_ppm > tds_high_thr) {
            rc_clear_mix(rc);
        }
    }

    /* Continue pending nutrient mix first (keeps proportions across channels) */
    if (rc_mix_has_remaining(rc) != 0U) {

        if (rc_peek_next_mix_dose(rc, &idx, &dose_ul) != 0U) {

            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
                dose_ul = rc->cfg.max_single_dose_ul;
            }

            if (rc_can_commit_total(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_NUTRIENT;
            step.nutrient_index = idx;
            step.dose_volume_ul = dose_ul;

            rc->inflight_active = 1U;
            rc->inflight_is_mix = 1U;
            rc->inflight_kind = step.dose_kind;
            rc->inflight_nutrient_index = idx;
            rc->inflight_volume_ul = dose_ul;

            return step;
        }

        rc_clear_mix(rc);
    }

    /* TDS control */
    if (rc->targets.enable_tds != 0U) {

        if (tds_ppm < tds_low_thr) {

            err_ppm = (uint32_t)(tds_low_thr - tds_ppm);

            portion_x1000 = rc_calc_portion_x1000(err_ppm,
                                                  rc->cfg.tds_nutrient_err_full_ppm,
                                                  rc->cfg.tds_nutrient_portion_min_x1000,
                                                  rc->cfg.tds_nutrient_portion_max_x1000);

            dose_ul = rc_calc_nutrient_step_ul(rc, main_volume_ul, portion_x1000);
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            /* Plan a proportional mix for this one correction step */
            if (rc_build_mix_plan(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc_peek_next_mix_dose(rc, &idx, &dose_ul) == 0U || dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
                dose_ul = rc->cfg.max_single_dose_ul;
            }

            if (rc_can_commit_total(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_NUTRIENT;
            step.nutrient_index = idx;
            step.dose_volume_ul = dose_ul;

            rc->inflight_active = 1U;
            rc->inflight_is_mix = 1U;
            rc->inflight_kind = step.dose_kind;
            rc->inflight_nutrient_index = idx;
            rc->inflight_volume_ul = dose_ul;

            return step;
        }

        if (tds_ppm > tds_high_thr) {

            err_ppm = (uint32_t)(tds_ppm - tds_high_thr);

            portion_x1000 = rc_calc_portion_x1000(err_ppm,
                                                  rc->cfg.tds_water_err_full_ppm,
                                                  rc->cfg.tds_water_portion_min_x1000,
                                                  rc->cfg.tds_water_portion_max_x1000);

            dose_ul = rc_calc_water_step_ul(rc, main_volume_ul, portion_x1000);
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
                dose_ul = rc->cfg.max_single_dose_ul;
            }

            if (rc_can_commit_total(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_WATER;
            step.dose_volume_ul = dose_ul;

            rc->inflight_active = 1U;
            rc->inflight_is_mix = 0U;
            rc->inflight_kind = step.dose_kind;
            rc->inflight_nutrient_index = 0U;
            rc->inflight_volume_ul = dose_ul;

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

            if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
                dose_ul = rc->cfg.max_single_dose_ul;
            }

            if (rc_can_commit_total(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_PH_UP;
            step.dose_volume_ul = dose_ul;

            rc->inflight_active = 1U;
            rc->inflight_is_mix = 0U;
            rc->inflight_kind = step.dose_kind;
            rc->inflight_nutrient_index = 0U;
            rc->inflight_volume_ul = dose_ul;

            return step;
        }

        if (ph_x1000 > ph_high_thr) {

            dose_ul = rc->cfg.ph_step_ul;
            if (dose_ul == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            if (rc->cfg.max_single_dose_ul != 0U && dose_ul > rc->cfg.max_single_dose_ul) {
                dose_ul = rc->cfg.max_single_dose_ul;
            }

            if (rc_can_commit_total(rc, dose_ul) == 0U) {
                rc->error = 1U;
                step.type = RECIPE_STEP_ERROR;
                return step;
            }

            step.type = RECIPE_STEP_DOSE;
            step.dose_kind = RECIPE_DOSE_PH_DOWN;
            step.dose_volume_ul = dose_ul;

            rc->inflight_active = 1U;
            rc->inflight_is_mix = 0U;
            rc->inflight_kind = step.dose_kind;
            rc->inflight_nutrient_index = 0U;
            rc->inflight_volume_ul = dose_ul;

            return step;
        }
    }

    step.type = RECIPE_STEP_DONE;
    return step;
}

void recipe_controller_on_dose_result(RecipeController_t *rc, uint8_t success)
{
    if (rc == NULL) {
        return;
    }

    if (rc->inflight_active == 0U) {
        return;
    }

    if (success == 0U) {
        rc->error = 1U;
        rc_clear_mix(rc);
        rc_clear_inflight(rc);
        return;
    }

    /* Commit total dose */
    rc->total_dosed_ul += rc->inflight_volume_ul;

    /* Commit pending mix consumption */
    if (rc->inflight_is_mix != 0U && rc->inflight_kind == RECIPE_DOSE_NUTRIENT) {

        uint8_t idx = rc->inflight_nutrient_index;
        uint32_t ul = rc->inflight_volume_ul;

        if (idx < rc->cfg.nutrient_count && idx < RECIPE_NUTRIENT_MAX_PUMPS) {
            if (rc->mix_remaining_ul[idx] >= ul) {
                rc->mix_remaining_ul[idx] -= ul;
            } else {
                rc->mix_remaining_ul[idx] = 0U;
            }

            rc_advance_mix_cursor(rc, idx);

            if (rc_mix_has_remaining(rc) == 0U) {
                rc_clear_mix(rc);
            }
        }
    }

    rc_clear_inflight(rc);
}
