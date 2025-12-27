#ifndef PH_SENSOR_CALIB_H
#define PH_SENSOR_CALIB_H

#include <stdint.h>
#include <stdbool.h>

/* Calibration model:
   pH = slope * voltage + offset
*/
typedef struct {
    float slope;
    float offset;
    float calibration_temp_c;
    bool  valid;
} PH_Sensor_Calib_t;

/* Reset calibration to invalid state */
void ph_sensor_calib_reset(PH_Sensor_Calib_t *calib);

/* Two-point calibration using voltage measurements:
   (v1, ph1) and (v2, ph2)

   Returns false if parameters are invalid (e.g. v1 == v2).
*/
bool ph_sensor_calib_set_two_point(PH_Sensor_Calib_t *calib,
                                   float v1, float ph1,
                                   float v2, float ph2,
                                   float calibration_temp_c);

/* Convert voltage (V) to pH using stored calibration.
   Returns false if calib is not valid.
*/
bool ph_sensor_calib_voltage_to_ph(const PH_Sensor_Calib_t *calib,
                                   float voltage,
                                   float *ph_out);

/* Optional temperature compensation:
   This scales the slope by (T(K) / Tcal(K)).
   It is a simplistic compensation and may not match your analog board behavior.
   If you do not know what you are doing, do not use it.

   Returns false if calib invalid or temperatures invalid.
*/
bool ph_sensor_calib_voltage_to_ph_tc(const PH_Sensor_Calib_t *calib,
                                      float voltage,
                                      float temperature_c,
                                      float *ph_out);

#endif
