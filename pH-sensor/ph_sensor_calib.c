#include "ph_sensor_calib.h"

static bool ph_sensor_isfinite_float(float x)
{
    /* Minimal check without <math.h> dependency.
       This rejects NaN by comparing with itself.
    */
    if (x != x) {
        return false;
    }
    return true;
}

void ph_sensor_calib_reset(PH_Sensor_Calib_t *calib)
{
    if (calib == NULL) {
        return;
    }

    calib->slope = 0.0f;
    calib->offset = 0.0f;
    calib->calibration_temp_c = 25.0f;
    calib->valid = false;
}

bool ph_sensor_calib_set_two_point(PH_Sensor_Calib_t *calib,
                                   float v1, float ph1,
                                   float v2, float ph2,
                                   float calibration_temp_c)
{
    float dv;

    if (calib == NULL) {
        return false;
    }

    if (!ph_sensor_isfinite_float(v1) || !ph_sensor_isfinite_float(v2) ||
        !ph_sensor_isfinite_float(ph1) || !ph_sensor_isfinite_float(ph2) ||
        !ph_sensor_isfinite_float(calibration_temp_c)) {
        return false;
    }

    dv = v2 - v1;
    if (dv == 0.0f) {
        return false;
    }

    calib->slope = (ph2 - ph1) / dv;
    calib->offset = ph1 - (calib->slope * v1);
    calib->calibration_temp_c = calibration_temp_c;
    calib->valid = true;

    return true;
}

bool ph_sensor_calib_voltage_to_ph(const PH_Sensor_Calib_t *calib,
                                   float voltage,
                                   float *ph_out)
{
    if (calib == NULL || ph_out == NULL) {
        return false;
    }

    if (!calib->valid) {
        return false;
    }

    *ph_out = (calib->slope * voltage) + calib->offset;
    return true;
}

bool ph_sensor_calib_voltage_to_ph_tc(const PH_Sensor_Calib_t *calib,
                                      float voltage,
                                      float temperature_c,
                                      float *ph_out)
{
    float t_k;
    float tcal_k;
    float slope_tc;

    if (calib == NULL || ph_out == NULL) {
        return false;
    }

    if (!calib->valid) {
        return false;
    }

    if (!ph_sensor_isfinite_float(temperature_c)) {
        return false;
    }

    /* Kelvin conversion */
    t_k = temperature_c + 273.15f;
    tcal_k = calib->calibration_temp_c + 273.15f;

    if (t_k <= 0.0f || tcal_k <= 0.0f) {
        return false;
    }

    slope_tc = calib->slope * (t_k / tcal_k);
    *ph_out = (slope_tc * voltage) + calib->offset;

    return true;
}
