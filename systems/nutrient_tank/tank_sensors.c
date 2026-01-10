#include "tank_sensors.h"
#include <string.h>

uint8_t tank_sensors_init(TankSensors_t *s, uint32_t stale_timeout_ms)
{
    if (s == NULL) {
        return 0U;
    }

    memset(s, 0, sizeof(*s));
    s->stale_timeout_ms = stale_timeout_ms;

    return 1U;
}

void tank_sensors_update_temperature_mC(TankSensors_t *s, uint32_t now_ms, int32_t temperature_mC)
{
    if (s == NULL) {
        return;
    }

    s->temperature_mC.valid = 1U;
    s->temperature_mC.updated_at_ms = now_ms;
    s->temperature_mC.value = temperature_mC;
}

void tank_sensors_update_ph_x1000(TankSensors_t *s, uint32_t now_ms, int32_t ph_x1000)
{
    if (s == NULL) {
        return;
    }

    s->ph_x1000.valid = 1U;
    s->ph_x1000.updated_at_ms = now_ms;
    s->ph_x1000.value = ph_x1000;
}

void tank_sensors_update_tds_ppm(TankSensors_t *s, uint32_t now_ms, int32_t tds_ppm)
{
    if (s == NULL) {
        return;
    }

    s->tds_ppm.valid = 1U;
    s->tds_ppm.updated_at_ms = now_ms;
    s->tds_ppm.value = tds_ppm;
}

uint8_t tank_sensors_is_fresh(const TankSensors_t *s, uint32_t now_ms)
{
    (void)now_ms;

    if (s == NULL) {
        return 0U;
    }

    /* Skeleton: do not enforce freshness yet */
    return 1U;
}
