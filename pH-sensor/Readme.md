### How to connect
Як це підключати

Po → ADC вхід (наприклад PA0 = ADC_CHANNEL_0).
Do → будь-який GPIO input (якщо треба поріг з компаратора).
V+ → 5V або 3.3V (як живиш модуль).
G → GND.

Важливе: багато таких pH-плат живляться від 5V, а Po тоді може доходити близько до 3V+ (залежить від плати). Якщо Po може бути >3.3V — став дільник або живи модуль від 3.3V.

### Usage
```
#include "ph_sensor.h"

extern ADC_HandleTypeDef hadc;

PH_Sensor_t ph1;

void app_init(void)
{
    GPIO_t ph_do = { .port = GPIOA, .pin = GPIO_PIN_1 };

    ph_sensor_init(&ph1,
                   &hadc,
                   ADC_CHANNEL_0,
                   ADC_SAMPLETIME_41CYCLES_5,
                   3.3f,
                   4095);

    ph_sensor_set_do_pin(&ph1, ph_do);

    /* Two-point calibration example:
       Measure voltage in buffer pH 7.00 and pH 4.00 and put values below.
    */
    ph_sensor_calibration_set_two_point(&ph1,
                                        2.050f, 7.00f,
                                        2.330f, 4.00f,
                                        25.0f);
}

void app_loop(void)
{
    float ph;
    float v;
    bool do_state;

    if (ph_sensor_read_voltage(&ph1, &v) == PH_SENSOR_STATUS_OK) {
        /* use v */
    }

    if (ph_sensor_read_ph(&ph1, &ph) == PH_SENSOR_STATUS_OK) {
        /* use ph */
    }

    if (ph_sensor_read_do(&ph1, &do_state)) {
        /* use do_state */
    }
}

```