#ifndef TANK_SENSORS_H
#define TANK_SENSORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* pH: pH_x1000 (e.g. 6.250 -> 6250)
   Temperature: milli-Celsius
   TDS: ppm (integer) */
typedef struct
{
    uint8_t valid;
    uint32_t updated_at_ms;

    int32_t value;
} TankSensorValue_t;

typedef struct
{
    TankSensorValue_t temperature_mC;
    TankSensorValue_t ph_x1000;
    TankSensorValue_t tds_ppm;

    uint32_t stale_timeout_ms;
} TankSensors_t;

uint8_t tank_sensors_init(TankSensors_t *s, uint32_t stale_timeout_ms);

void tank_sensors_update_temperature_mC(TankSensors_t *s, uint32_t now_ms, int32_t temperature_mC);
void tank_sensors_update_ph_x1000(TankSensors_t *s, uint32_t now_ms, int32_t ph_x1000);
void tank_sensors_update_tds_ppm(TankSensors_t *s, uint32_t now_ms, int32_t tds_ppm);

uint8_t tank_sensors_is_fresh(const TankSensors_t *s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* TANK_SENSORS_H */
