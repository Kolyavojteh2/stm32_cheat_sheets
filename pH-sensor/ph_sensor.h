#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#include "main.h"
#include "gpio.h"
#include "ph_sensor_calib.h"

/* Status codes for pH sensor operations */
typedef enum {
    PH_SENSOR_STATUS_OK = 0,
    PH_SENSOR_STATUS_INVALID_PARAM,
    PH_SENSOR_STATUS_HAL_ERROR,
    PH_SENSOR_STATUS_NOT_CALIBRATED
} PH_Sensor_Status_t;

/* pH sensor instance */
typedef struct {
    ADC_HandleTypeDef *hadc;

    uint32_t ph_adc_channel;
    uint32_t ph_adc_sampling_time;

    bool     has_temp_channel;
    uint32_t temp_adc_channel;
    uint32_t temp_adc_sampling_time;

    bool     has_do_pin;
    GPIO_t   do_pin;

    uint32_t adc_max;
    float    vref;

    PH_Sensor_Calib_t calib;
} PH_Sensor_t;

/* Initialize an instance with required parameters.
   - hadc: ADC handle
   - ph_adc_channel: ADC_CHANNEL_x used for Po output
   - ph_adc_sampling_time: ADC_SAMPLETIME_x
   - vref: ADC reference voltage in volts (e.g. 3.3f)
   - adc_max: max ADC code (4095 for 12-bit)
*/
PH_Sensor_Status_t ph_sensor_init(PH_Sensor_t *self,
                                  ADC_HandleTypeDef *hadc,
                                  uint32_t ph_adc_channel,
                                  uint32_t ph_adc_sampling_time,
                                  float vref,
                                  uint32_t adc_max);

/* Configure optional digital output pin (Do) */
PH_Sensor_Status_t ph_sensor_set_do_pin(PH_Sensor_t *self, GPIO_t do_pin);

/* Disable digital output pin support */
PH_Sensor_Status_t ph_sensor_disable_do_pin(PH_Sensor_t *self);

/* Configure optional temperature ADC channel (To) if you actually use it */
PH_Sensor_Status_t ph_sensor_set_temp_channel(PH_Sensor_t *self,
                                              uint32_t temp_adc_channel,
                                              uint32_t temp_adc_sampling_time);

/* Disable temperature channel support */
PH_Sensor_Status_t ph_sensor_disable_temp_channel(PH_Sensor_t *self);

/* Calibration helpers */
void ph_sensor_calibration_reset(PH_Sensor_t *self);

bool ph_sensor_calibration_set_two_point(PH_Sensor_t *self,
                                         float v1, float ph1,
                                         float v2, float ph2,
                                         float calibration_temp_c);

/* Read raw ADC code from Po channel */
PH_Sensor_Status_t ph_sensor_read_raw(PH_Sensor_t *self, uint16_t *raw_out);

/* Read Po voltage in volts */
PH_Sensor_Status_t ph_sensor_read_voltage(PH_Sensor_t *self, float *voltage_out);

/* Read pH using stored calibration */
PH_Sensor_Status_t ph_sensor_read_ph(PH_Sensor_t *self, float *ph_out);

/* Read pH with optional temperature compensation.
   This uses the simplistic slope scaling in ph_sensor_calib_voltage_to_ph_tc.
*/
PH_Sensor_Status_t ph_sensor_read_ph_tc(PH_Sensor_t *self,
                                        float temperature_c,
                                        float *ph_out);

/* Read digital output (Do) if configured.
   Returns false if Do is not configured.
*/
bool ph_sensor_read_do(PH_Sensor_t *self, bool *state_out);

/* If you use To channel: read raw and voltage.
   This library does not convert To voltage to temperature because boards differ.
*/
PH_Sensor_Status_t ph_sensor_read_temp_raw(PH_Sensor_t *self, uint16_t *raw_out);
PH_Sensor_Status_t ph_sensor_read_temp_voltage(PH_Sensor_t *self, float *voltage_out);

#endif
