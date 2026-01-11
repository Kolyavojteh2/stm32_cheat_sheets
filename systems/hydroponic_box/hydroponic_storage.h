#ifndef HYDROPONIC_STORAGE_H
#define HYDROPONIC_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "at24c04.h"

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t version;

    uint8_t light_on;
    uint16_t boot_count;

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
