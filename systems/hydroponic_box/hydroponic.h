#ifndef HYDROPONIC_H
#define HYDROPONIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "gpio.h"
#include "gpio_switch.h"

#include "ds3231.h"
#include "dht22.h"
#include "at24c04.h"

#include "hydroponic_storage.h"

typedef enum {
    HYDROPONIC_ERROR_NONE        = 0u,
    HYDROPONIC_ERROR_RTC         = 1u << 0,
    HYDROPONIC_ERROR_DHT22       = 1u << 1,
    HYDROPONIC_ERROR_EEPROM      = 1u << 2,
    HYDROPONIC_ERROR_MCU_TEMP    = 1u << 3
} HydroponicErrorFlags_t;

typedef struct {
    DS3231_t *rtc;
    DHT22_t *dht22;
    AT24C04_t *eeprom;

    uint16_t rtc_int_pin;

    GPIO_t light_pin;
    GPIO_switch_active_level_t light_active_level;

    GPIO_t error_led_pin;
    GPIO_switch_active_level_t error_led_active_level;

    uint16_t eeprom_base_addr;

    uint8_t light_on_hour;
    uint8_t light_off_hour;

    int (*mcu_temp_read)(void *ctx, float *temp_c);
    void *mcu_temp_ctx;
} HydroponicConfig_t;

typedef struct {
    HydroponicConfig_t cfg;

    GPIO_switch_t light_sw;
    GPIO_switch_t error_led_sw;

    HydroponicStorage_t storage;

    volatile uint8_t rtc_irq_pending;

    uint8_t error_flags;
    uint8_t light_is_on;

    uint16_t boot_count;

    /*
    Power outage compensation state (RAM cache).

    deficit_minutes:
        Total missing light minutes accumulated across multiple outages.

    outage_count:
        Number of detected outages (or long resets) based on last_alive gap.
        This is stored in EEPROM and incremented on boot when gap > threshold.

    last_process_min_2000:
        Used to decrement deficit minutes robustly if minute ticks were missed.

    heartbeat_slot:
        Used to write EEPROM once per 5 minutes (slot = now_min / 5).

    compensation_active:
        Indicates we were doing night compensation in the previous interval.
    */
    uint32_t deficit_minutes;
    uint32_t outage_count;
    uint32_t last_process_min_2000;
    uint32_t heartbeat_slot;
    uint8_t compensation_active;
} Hydroponic_t;

int hydroponic_init(Hydroponic_t *self, const HydroponicConfig_t *cfg);

/* Call from main loop after wake-up */
int hydroponic_process(Hydroponic_t *self);

/* Call from HAL_GPIO_EXTI_Callback */
void hydroponic_exti_irq_handler(Hydroponic_t *self, uint16_t gpio_pin);

uint8_t hydroponic_get_error_flags(const Hydroponic_t *self);
uint8_t hydroponic_is_light_on(const Hydroponic_t *self);

#ifdef __cplusplus
}
#endif

#endif
