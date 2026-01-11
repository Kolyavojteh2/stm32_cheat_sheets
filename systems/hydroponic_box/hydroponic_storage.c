#include "hydroponic_storage.h"

#define HYDROPONIC_STORAGE_MAGIC      (0x48594450u) /* 'H''Y''D''P' */
#define HYDROPONIC_STORAGE_VERSION    (1u)

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

int hydroponic_storage_init(HydroponicStorage_t *self, AT24C04_t *eeprom, uint16_t base_addr)
{
    if (self == NULL || eeprom == NULL) {
        return -1;
    }

    self->eeprom = eeprom;
    self->base_addr = base_addr;
    return 0;
}

int hydroponic_storage_load(HydroponicStorage_t *self, HydroponicStorageRecord_t *rec)
{
    if (self == NULL || rec == NULL || self->eeprom == NULL) {
        return -1;
    }

    HydroponicStorageRecord_t tmp;

    if (at24c04_read(self->eeprom, self->base_addr, (uint8_t *)&tmp, (uint16_t)sizeof(tmp)) != AT24C04_OK) {
        return -2;
    }

    if (tmp.magic != HYDROPONIC_STORAGE_MAGIC || tmp.version != HYDROPONIC_STORAGE_VERSION) {
        return -3;
    }

    uint16_t expected_crc = tmp.crc16;
    tmp.crc16 = 0;

    uint16_t calc_crc = crc16_ccitt((const uint8_t *)&tmp, (uint16_t)sizeof(tmp));
    if (calc_crc != expected_crc) {
        return -4;
    }

    tmp.crc16 = expected_crc;
    *rec = tmp;
    return 0;
}

int hydroponic_storage_save(HydroponicStorage_t *self, const HydroponicStorageRecord_t *rec)
{
    if (self == NULL || rec == NULL || self->eeprom == NULL) {
        return -1;
    }

    HydroponicStorageRecord_t tmp = *rec;
    tmp.magic = HYDROPONIC_STORAGE_MAGIC;
    tmp.version = HYDROPONIC_STORAGE_VERSION;

    tmp.crc16 = 0;
    tmp.crc16 = crc16_ccitt((const uint8_t *)&tmp, (uint16_t)sizeof(tmp));

    if (at24c04_write(self->eeprom, self->base_addr, (const uint8_t *)&tmp, (uint16_t)sizeof(tmp)) != AT24C04_OK) {
        return -2;
    }

    return 0;
}
