#ifndef RECIPE_CONTROLLER_H
#define RECIPE_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RECIPE_NUTRIENT_MAX_PUMPS    4U

typedef enum
{
    RECIPE_STEP_NONE = 0,
    RECIPE_STEP_DOSE,
    RECIPE_STEP_DONE,
    RECIPE_STEP_ERROR
} RecipeStepType_t;

typedef enum
{
    RECIPE_DOSE_NONE = 0,
    RECIPE_DOSE_WATER,
    RECIPE_DOSE_NUTRIENT,
    RECIPE_DOSE_PH_UP,
    RECIPE_DOSE_PH_DOWN
} RecipeDoseKind_t;

typedef struct
{
    RecipeStepType_t type;

    RecipeDoseKind_t dose_kind;

    /* Used only when dose_kind == RECIPE_DOSE_NUTRIENT */
    uint8_t nutrient_index;

    uint32_t dose_volume_ul;
} RecipeStep_t;

typedef struct
{
    /* Step size defaults, limits and nutrient pool definition */
    uint32_t max_total_dose_ul;
    uint32_t max_single_dose_ul;

    /* pH dosing step (uL) */
    uint32_t ph_step_ul;

    /* Legacy absolute steps (uL) */
    uint32_t tds_nutrient_step_ul;
    uint32_t tds_water_step_ul;

    /* Per-liter steps (uL per liter). If non-zero, used with main_volume_ul. */
    uint32_t tds_nutrient_step_ul_per_l;
    uint32_t tds_water_step_ul_per_l;

    /* Nutrient pump count (1..4) */
    uint8_t nutrient_count;

    /* If mask bit i = 1 -> nutrient i is enabled.
       If mask == 0 -> all nutrients [0..count-1] enabled. */
    uint8_t nutrient_enable_mask;

    /* Explicit ratio weights (if sum > 0 they are used) */
    uint16_t nutrient_ratio[RECIPE_NUTRIENT_MAX_PUMPS];

    /* Alternative way: "parts per liter" (used as weights if nutrient_ratio sum == 0) */
    uint16_t nutrient_parts_per_l[RECIPE_NUTRIENT_MAX_PUMPS];

    /* One "part" volume (uL). If set, controller can derive tds_nutrient_step_ul_per_l:
       step_ul_per_l = part_volume_ul * sum(parts_per_l) * step_portion_x1000 / 1000 */
    uint32_t nutrient_part_volume_ul;

    /* Portion scaling for nutrient per-liter step (x1000). Example: 100 = 0.1 portion. */
    uint16_t tds_nutrient_step_portion_x1000;
} RecipeController_Config_t;

typedef struct
{
    /* Dynamic targets (set by NutrientTank control command) */
    uint8_t enable_ph;
    uint8_t enable_tds;

    int32_t target_ph_x1000;
    int32_t ph_tolerance_x1000;

    int32_t target_tds_ppm;
    int32_t tds_tolerance_ppm;
} RecipeController_Targets_t;

typedef struct
{
    RecipeController_Config_t cfg;
    RecipeController_Targets_t targets;

    uint32_t total_dosed_ul;

    uint8_t active;
    uint8_t error;

    /* Pending nutrient-mix plan (one TDS correction step split across channels) */
    uint8_t mix_active;
    uint8_t mix_next_index;
    uint32_t mix_remaining_ul[RECIPE_NUTRIENT_MAX_PUMPS];
} RecipeController_t;

/* Init controller instance */
uint8_t recipe_controller_init(RecipeController_t *rc, const RecipeController_Config_t *cfg);

/* Update dynamic targets */
uint8_t recipe_controller_set_targets(RecipeController_t *rc, const RecipeController_Targets_t *targets);

/* Start/stop closed-loop */
void recipe_controller_start(RecipeController_t *rc);
void recipe_controller_stop(RecipeController_t *rc);

/* main_volume_ul is the current main tank volume in uL (for per-liter scaling).
   If volume is unknown, pass 0 -> controller falls back to absolute steps. */
RecipeStep_t recipe_controller_next_step(RecipeController_t *rc,
                                        int32_t ph_x1000,
                                        int32_t tds_ppm,
                                        uint8_t sensors_fresh,
                                        uint32_t main_volume_ul);

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_CONTROLLER_H */
