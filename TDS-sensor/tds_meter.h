#ifndef TDS_METER_H
#define TDS_METER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HAL include selection:
 * - You can override by defining TDS_METER_HAL_HEADER (string), e.g.:
 *   #define TDS_METER_HAL_HEADER "stm32f4xx_hal.h"
 */
#ifndef TDS_METER_HAL_HEADER
    #if defined(STM32F0xx) || defined(STM32F030x8) || defined(STM32F030xC)
        #define TDS_METER_HAL_HEADER "stm32f0xx_hal.h"
    #elif defined(STM32F4xx) || defined(STM32F401xE) || defined(STM32F446xx)
        #define TDS_METER_HAL_HEADER "stm32f4xx_hal.h"
    #else
        #error "tds_meter: unsupported MCU family. Define TDS_METER_HAL_HEADER to your HAL header."
    #endif
#endif

#include TDS_METER_HAL_HEADER

#include "gpio.h"
#include "tds_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ADC_HandleTypeDef *hadc;

    /* ADC reference voltage (Volts). Usually 3.3 on STM32 boards. */
    float vref_v;

    /*
     * ADC range used as denominator (like DFRobot example):
     *  - 1024 for 10-bit
     *  - 4096 for 12-bit
     */
    uint32_t adc_range;

    /* User-provided sample buffers (len = sample_count) */
    uint16_t *sample_buf;
    uint16_t *work_buf;
    uint16_t sample_count;
    uint16_t sample_index;

    /* Compensation and calibration */
    float temperature_c;         /* Default 25C */
    float temp_comp_coeff;       /* Default 0.02 per C */
    float tds_factor;            /* Default 0.5 (TDS ~= EC/2) */
    float calibration_factor;    /* Default 1.0 */

    /* Cached last computed values */
    float last_voltage_v;
    float last_tds_ppm;
    bool  last_valid;

    /* Optional: power enable pin for sensor board */
    GPIO_t power_pin;
    bool   power_pin_used;
    bool   power_pin_active_high;
} TDS_Meter_t;

/* Initialization */
void tds_meter_init(TDS_Meter_t *m,
                    ADC_HandleTypeDef *hadc,
                    float vref_v,
                    uint32_t adc_range,
                    uint16_t *sample_buf,
                    uint16_t *work_buf,
                    uint16_t sample_count);

/* Optional power control */
void tds_meter_set_power_pin(TDS_Meter_t *m, GPIO_t pin, bool active_high);
void tds_meter_power_on(TDS_Meter_t *m);
void tds_meter_power_off(TDS_Meter_t *m);

/* Configuration */
void tds_meter_set_temperature_c(TDS_Meter_t *m, float temperature_c);
void tds_meter_set_temp_comp_coeff(TDS_Meter_t *m, float coeff_per_c);
void tds_meter_set_tds_factor(TDS_Meter_t *m, float tds_factor);
void tds_meter_set_calibration_factor(TDS_Meter_t *m, float factor);

/* Sampling */
HAL_StatusTypeDef tds_meter_sample(TDS_Meter_t *m, uint32_t timeout_ms);
void tds_meter_push_raw(TDS_Meter_t *m, uint16_t raw);

/* Reading (uses median filter across sample_buf) */
float tds_meter_get_voltage_v(TDS_Meter_t *m);
float tds_meter_get_tds_ppm(TDS_Meter_t *m);

/*
 * Simple calibration helper:
 * - Put probe into known solution at 25C (e.g., 707ppm from 1413us/cm),
 * - Set temperature to 25,
 * - Call this function once after readings stabilize.
 */
bool tds_meter_calibrate_at_25c(TDS_Meter_t *m, float known_tds_ppm);

#ifdef __cplusplus
}
#endif

#endif /* TDS_METER_H */
