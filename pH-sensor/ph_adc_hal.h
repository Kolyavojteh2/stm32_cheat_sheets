#ifndef PH_ADC_HAL_H
#define PH_ADC_HAL_H

#include <stdint.h>
#include "main.h"

/* Configure ADC channel and read one conversion.
   sampling_time must be a valid HAL constant for your MCU family.
*/
HAL_StatusTypeDef ph_adc_hal_read(ADC_HandleTypeDef *hadc,
                                  uint32_t channel,
                                  uint32_t sampling_time,
                                  uint16_t *raw_out);

#endif
