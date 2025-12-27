#include "ph_sensor.h"
#include "ph_adc_hal.h"

PH_Sensor_Status_t ph_sensor_init(PH_Sensor_t *self,
                                  ADC_HandleTypeDef *hadc,
                                  uint32_t ph_adc_channel,
                                  uint32_t ph_adc_sampling_time,
                                  float vref,
                                  uint32_t adc_max)
{
    if (self == NULL || hadc == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (adc_max == 0U) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (vref <= 0.0f) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->hadc = hadc;

    self->ph_adc_channel = ph_adc_channel;
    self->ph_adc_sampling_time = ph_adc_sampling_time;

    self->has_temp_channel = false;
    self->temp_adc_channel = 0U;
    self->temp_adc_sampling_time = 0U;

    self->has_do_pin = false;

    self->adc_max = adc_max;
    self->vref = vref;

    ph_sensor_calib_reset(&self->calib);

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_set_do_pin(PH_Sensor_t *self, GPIO_t do_pin)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->do_pin = do_pin;
    self->has_do_pin = true;

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_disable_do_pin(PH_Sensor_t *self)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_do_pin = false;
    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_set_temp_channel(PH_Sensor_t *self,
                                              uint32_t temp_adc_channel,
                                              uint32_t temp_adc_sampling_time)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_temp_channel = true;
    self->temp_adc_channel = temp_adc_channel;
    self->temp_adc_sampling_time = temp_adc_sampling_time;

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_disable_temp_channel(PH_Sensor_t *self)
{
    if (self == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    self->has_temp_channel = false;
    return PH_SENSOR_STATUS_OK;
}

void ph_sensor_calibration_reset(PH_Sensor_t *self)
{
    if (self == NULL) {
        return;
    }

    ph_sensor_calib_reset(&self->calib);
}

bool ph_sensor_calibration_set_two_point(PH_Sensor_t *self,
                                         float v1, float ph1,
                                         float v2, float ph2,
                                         float calibration_temp_c)
{
    if (self == NULL) {
        return false;
    }

    return ph_sensor_calib_set_two_point(&self->calib,
                                         v1, ph1,
                                         v2, ph2,
                                         calibration_temp_c);
}

PH_Sensor_Status_t ph_sensor_read_raw(PH_Sensor_t *self, uint16_t *raw_out)
{
    if (self == NULL || raw_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (ph_adc_hal_read(self->hadc,
                        self->ph_adc_channel,
                        self->ph_adc_sampling_time,
                        raw_out) != HAL_OK) {
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_voltage(PH_Sensor_t *self, float *voltage_out)
{
    uint16_t raw;
    PH_Sensor_Status_t st;

    if (self == NULL || voltage_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    st = ph_sensor_read_raw(self, &raw);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    *voltage_out = ((float)raw * self->vref) / (float)self->adc_max;
    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_ph(PH_Sensor_t *self, float *ph_out)
{
    float voltage;
    PH_Sensor_Status_t st;

    if (self == NULL || ph_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->calib.valid) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    st = ph_sensor_read_voltage(self, &voltage);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    if (!ph_sensor_calib_voltage_to_ph(&self->calib, voltage, ph_out)) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_ph_tc(PH_Sensor_t *self,
                                        float temperature_c,
                                        float *ph_out)
{
    float voltage;
    PH_Sensor_Status_t st;

    if (self == NULL || ph_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->calib.valid) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    st = ph_sensor_read_voltage(self, &voltage);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    if (!ph_sensor_calib_voltage_to_ph_tc(&self->calib, voltage, temperature_c, ph_out)) {
        return PH_SENSOR_STATUS_NOT_CALIBRATED;
    }

    return PH_SENSOR_STATUS_OK;
}

bool ph_sensor_read_do(PH_Sensor_t *self, bool *state_out)
{
    GPIO_PinState s;

    if (self == NULL || state_out == NULL) {
        return false;
    }

    if (!self->has_do_pin) {
        return false;
    }

    s = HAL_GPIO_ReadPin(self->do_pin.port, self->do_pin.pin);
    *state_out = (s == GPIO_PIN_SET);

    return true;
}

PH_Sensor_Status_t ph_sensor_read_temp_raw(PH_Sensor_t *self, uint16_t *raw_out)
{
    if (self == NULL || raw_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (!self->has_temp_channel) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    if (ph_adc_hal_read(self->hadc,
                        self->temp_adc_channel,
                        self->temp_adc_sampling_time,
                        raw_out) != HAL_OK) {
        return PH_SENSOR_STATUS_HAL_ERROR;
    }

    return PH_SENSOR_STATUS_OK;
}

PH_Sensor_Status_t ph_sensor_read_temp_voltage(PH_Sensor_t *self, float *voltage_out)
{
    uint16_t raw;
    PH_Sensor_Status_t st;

    if (self == NULL || voltage_out == NULL) {
        return PH_SENSOR_STATUS_INVALID_PARAM;
    }

    st = ph_sensor_read_temp_raw(self, &raw);
    if (st != PH_SENSOR_STATUS_OK) {
        return st;
    }

    *voltage_out = ((float)raw * self->vref) / (float)self->adc_max;
    return PH_SENSOR_STATUS_OK;
}
