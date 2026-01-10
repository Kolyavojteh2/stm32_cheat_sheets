#ifndef RECIPE_CONTROLLER_H
#define RECIPE_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RECIPE_NUTRIENT_MAX_PUMPS    4U

/* What kind of step controller requests */
typedef enum
{
    RECIPE_STEP_NONE = 0,
    RECIPE_STEP_DOSE,
    RECIPE_STEP_DONE,
    RECIPE_STEP_ERROR
} RecipeStepType_t;

/* Dosing target */
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
    uint8_t enable_ph;
    uint8_t enable_tds;

    /* pH in x1000 (e.g. 6.250 -> 6250) */
    int32_t target_ph_x1000;
    int32_t ph_tolerance_x1000;

    /* TDS in ppm */
    int32_t target_tds_ppm;
    int32_t tds_tolerance_ppm;

    /* Safety limits */
    uint32_t max_total_dose_ul;
    uint32_t max_single_dose_ul;

    /* Step sizes (closed-loop increments) */
    uint32_t ph_step_ul;

    /* TDS control: prefer nutrients, can dilute with water */
    uint32_t tds_nutrient_step_ul;
    uint32_t tds_water_step_ul;

    /* Nutrient pump count (1..4) */
    uint8_t nutrient_count;

    /* Optional: per-nutrient enable mask */
    uint8_t nutrient_enable_mask;
} RecipeController_Config_t;

typedef struct
{
    RecipeController_Config_t cfg;

    uint32_t total_dosed_ul;
    uint8_t active;
    uint8_t error;
} RecipeController_t;

/* Init controller instance */
uint8_t recipe_controller_init(RecipeController_t *rc, const RecipeController_Config_t *cfg);

/* Start/stop closed-loop */
void recipe_controller_start(RecipeController_t *rc);
void recipe_controller_stop(RecipeController_t *rc);

/* Produce next requested step based on latest measurements */
RecipeStep_t recipe_controller_next_step(RecipeController_t *rc,
                                        int32_t ph_x1000,
                                        int32_t tds_ppm,
                                        uint8_t sensors_fresh);

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_CONTROLLER_H */
