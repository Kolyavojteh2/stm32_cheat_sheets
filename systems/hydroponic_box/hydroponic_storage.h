#ifndef HYDROPONIC_STORAGE_H
#define HYDROPONIC_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "at24c04.h"

/*
Power compensation storage record.

Fields:
- last_alive_min_2000:
    "Minutes since 2000-01-01 00:00".
    This is written periodically (heartbeat) while MCU is alive.
    On boot we compare current time to last_alive to detect a power outage.

- deficit_minutes:
    Accumulated missing light minutes caused by power outages during
    the scheduled light window (07:00..23:00).
    During night compensation (23:00..07:00) this value is decreased.

- outage_count:
    Counter of detected outages (or long resets) based on the last_alive gap.
    Incremented on boot when gap > HYDROPONIC_POWER_LOSS_DETECT_MIN.
*/
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t version;

    uint16_t boot_count;

    uint32_t last_alive_min_2000;
    uint32_t deficit_minutes;
    uint32_t outage_count;

    uint8_t light_is_on;

    uint16_t crc16;
} HydroponicStorageRecord_t;

typedef struct
{
    AT24C04_t *eeprom;
    uint16_t base_addr;
} HydroponicStorage_t;

int hydroponic_storage_init(HydroponicStorage_t *self, AT24C04_t *eeprom, uint16_t base_addr);
int hydroponic_storage_load(HydroponicStorage_t *self, HydroponicStorageRecord_t *rec);
int hydroponic_storage_save(HydroponicStorage_t *self, const HydroponicStorageRecord_t *rec);

#ifdef __cplusplus
}
#endif

#endif
