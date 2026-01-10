#include "recipe_controller.h"
#include <string.h>

uint8_t recipe_controller_init(RecipeController_t *rc, const RecipeController_Config_t *cfg)
{
    if (rc == NULL || cfg == NULL) {
        return 0U;
    }

    memset(rc, 0, sizeof(*rc));
    rc->cfg = *cfg;

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
    (void)ph_x1000;
    (void)tds_ppm;
    (void)sensors_fresh;

    RecipeStep_t step;
    memset(&step, 0, sizeof(step));

    if (rc == NULL || rc->active == 0U) {
        step.type = RECIPE_STEP_NONE;
        return step;
    }

    /* Skeleton */
    step.type = RECIPE_STEP_NONE;
    return step;
}
