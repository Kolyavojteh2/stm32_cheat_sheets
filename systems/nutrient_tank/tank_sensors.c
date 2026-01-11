#include "tank_sensors.h"
#include <string.h>

/* Wrap-safe time compare (valid if deadlines within +/- 2^31 ms). */
static uint8_t ts_is_fresh_value(const TankSensorValue_t *v, uint32_t now_ms, uint32_t stale_timeout_ms)
{
    uint32_t age_ms;

    if (v == NULL) {
        return 0U;
    }

    if (v->valid == 0U) {
        return 0U;
    }

    if (stale_timeout_ms == 0U) {
        return 1U;
    }

    age_ms = (uint32_t)(now_ms - v->updated_at_ms);
    return (age_ms <= stale_timeout_ms) ? 1U : 0U;
}

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

uint8_t tank_sensors_is_temperature_fresh(const TankSensors_t *s, uint32_t now_ms)
{
    if (s == NULL) {
        return 0U;
    }

    return ts_is_fresh_value(&s->temperature_mC, now_ms, s->stale_timeout_ms);
}

uint8_t tank_sensors_is_ph_fresh(const TankSensors_t *s, uint32_t now_ms)
{
    if (s == NULL) {
        return 0U;
    }

    return ts_is_fresh_value(&s->ph_x1000, now_ms, s->stale_timeout_ms);
}

uint8_t tank_sensors_is_tds_fresh(const TankSensors_t *s, uint32_t now_ms)
{
    if (s == NULL) {
        return 0U;
    }

    return ts_is_fresh_value(&s->tds_ppm, now_ms, s->stale_timeout_ms);
}

uint8_t tank_sensors_are_fresh(const TankSensors_t *s,
                               uint32_t now_ms,
                               uint8_t need_temperature,
                               uint8_t need_ph,
                               uint8_t need_tds)
{
    if (s == NULL) {
        return 0U;
    }

    if (need_temperature != 0U && tank_sensors_is_temperature_fresh(s, now_ms) == 0U) {
        return 0U;
    }
    if (need_ph != 0U && tank_sensors_is_ph_fresh(s, now_ms) == 0U) {
        return 0U;
    }
    if (need_tds != 0U && tank_sensors_is_tds_fresh(s, now_ms) == 0U) {
        return 0U;
    }

    return 1U;
}

static uint8_t ts_is_newer_or_equal(uint32_t updated_at_ms, uint32_t after_ms)
{
    /* Wrap-safe comparison for monotonic ticks */
    return (((int32_t)(updated_at_ms - after_ms)) >= 0) ? 1U : 0U;
}

uint8_t tank_sensors_are_newer_than(const TankSensors_t *s,
                                    uint32_t after_ms,
                                    uint8_t need_temperature,
                                    uint8_t need_ph,
                                    uint8_t need_tds)
{
    if (s == NULL) {
        return 0U;
    }

    if (need_temperature != 0U) {
        if (s->temperature_mC.valid == 0U) {
            return 0U;
        }
        if (ts_is_newer_or_equal(s->temperature_mC.updated_at_ms, after_ms) == 0U) {
            return 0U;
        }
    }

    if (need_ph != 0U) {
        if (s->ph_x1000.valid == 0U) {
            return 0U;
        }
        if (ts_is_newer_or_equal(s->ph_x1000.updated_at_ms, after_ms) == 0U) {
            return 0U;
        }
    }

    if (need_tds != 0U) {
        if (s->tds_ppm.valid == 0U) {
            return 0U;
        }
        if (ts_is_newer_or_equal(s->tds_ppm.updated_at_ms, after_ms) == 0U) {
            return 0U;
        }
    }

    return 1U;
}
