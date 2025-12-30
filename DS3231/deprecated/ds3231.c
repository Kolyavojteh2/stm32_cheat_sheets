/*
 * ds3231.c (instance-based, struct tm, alarm support)
 */

#include "ds3231.h"
#include <string.h>

/* ===== Internal helpers ===== */

static inline uint16_t normalize_addr(uint8_t addr)
{
    /* If it's a 7-bit address (<0x80), convert to 8-bit by shifting left. Otherwise assume already 8-bit. */
    return (addr < 0x80U) ? ((uint16_t)addr << 1) : (uint16_t)addr;
}

/* Convert 8-bit binary-coded decimal to uint8 */
static inline uint8_t bcd2bin(uint8_t val)
{
    return (uint8_t)((val & 0x0FU) + 10U * ((val >> 4) & 0x0FU));
}

/* Convert 0..99 to BCD */
static inline uint8_t bin2bcd(uint8_t val)
{
    return (uint8_t)(((val / 10U) << 4) | (val % 10U));
}

/* HAL wrappers */
static inline HAL_StatusTypeDef i2c_read(DS3231 *rtc, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(rtc->hi2c, rtc->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 1000);
}

static inline HAL_StatusTypeDef i2c_write(DS3231 *rtc, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(rtc->hi2c, rtc->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t*)buf, len, 1000);
}

int ds3231_init(DS3231 *rtc, I2C_HandleTypeDef *hi2c, uint8_t addr)
{
    if (!rtc || !hi2c) return -1;
    memset(rtc, 0, sizeof(*rtc));
    rtc->hi2c = hi2c;
    rtc->dev_addr = normalize_addr(addr);
    /* Optional: sanity read from status register */
    uint8_t stat = 0;
    if (i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK) return -2;
    return 0;
}

int ds3231_get_time(DS3231 *rtc, struct tm *out)
{
    if (!rtc || !out) return -1;
    uint8_t buf[7];
    if (i2c_read(rtc, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK) return -2;

    /* Decode */
    uint8_t sec = bcd2bin(buf[0] & 0x7F);
    uint8_t min = bcd2bin(buf[1] & 0x7F);
    uint8_t hr_raw = buf[2];
    uint8_t hrs;
    if (hr_raw & 0x40) {
        /* 12-hour mode */
        hrs = bcd2bin(hr_raw & 0x1F);
        bool pm = (hr_raw & 0x20) != 0;
        if (pm && hrs < 12) hrs += 12;
        if (!pm && hrs == 12) hrs = 0;
    } else {
        /* 24-hour mode */
        hrs = bcd2bin(hr_raw & 0x3F);
    }
    uint8_t wday_ds = buf[3] & 0x07; /* 1..7 (1 = Sunday on DS3231) */
    uint8_t mday = bcd2bin(buf[4] & 0x3F);
    uint8_t month = bcd2bin(buf[5] & 0x1F);
    uint8_t year = bcd2bin(buf[6]); /* 00..99 */

    memset(out, 0, sizeof(*out));
    out->tm_sec  = sec;
    out->tm_min  = min;
    out->tm_hour = hrs;
    out->tm_mday = mday;
    out->tm_mon  = (int)month - 1;       /* struct tm: 0..11 */
    out->tm_year = 100 + year;           /* struct tm: years since 1900 -> 2000+year */
    /* Convert DS day (1..7, 1=Sunday) to tm_wday (0=Sunday..6) */
    out->tm_wday = (wday_ds % 7);

    return 0;
}

int ds3231_set_time(DS3231 *rtc, const struct tm *in)
{
    if (!rtc || !in) return -1;
    if (in->tm_year < 100 || in->tm_year > 199) return -2; /* allow 2000..2099 */
    uint8_t year = (uint8_t)((in->tm_year - 100) & 0xFF);
    uint8_t month = (uint8_t)(in->tm_mon + 1);
    uint8_t wday_ds = (uint8_t)((in->tm_wday % 7) == 0 ? 1 : (in->tm_wday % 7)); /* Map 0..6 to 1..7 with 0->1 */

    uint8_t buf[7];
    buf[0] = bin2bcd((uint8_t)in->tm_sec) & 0x7F;
    buf[1] = bin2bcd((uint8_t)in->tm_min) & 0x7F;
    buf[2] = bin2bcd((uint8_t)in->tm_hour) & 0x3F; /* 24-hour mode */
    buf[3] = (uint8_t)(wday_ds & 0x07);
    buf[4] = bin2bcd((uint8_t)in->tm_mday) & 0x3F;
    buf[5] = bin2bcd(month) & 0x1F; /* ignore century bit */
    buf[6] = bin2bcd(year);

    if (i2c_write(rtc, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK) return -3;

    /* Clear OSF (Oscillator Stop Flag) if set */
    uint8_t stat;
    if (i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) == HAL_OK) {
        if (stat & DS3231_STAT_OSF) {
            stat &= ~DS3231_STAT_OSF;
            (void)i2c_write(rtc, DS3231_REG_STATUS, &stat, 1);
        }
    }
    return 0;
}

int ds3231_get_temperature(DS3231 *rtc, float *out_c)
{
    if (!rtc || !out_c) return -1;
    uint8_t t[2];
    if (i2c_read(rtc, DS3231_REG_TEMP_MSB, t, 2) != HAL_OK) return -2;
    /* MSB is integer, LSB bits 7:6 are quarters */
    int8_t msb = (int8_t)t[0];
    float frac = (float)((t[1] >> 6) & 0x03) * 0.25f;
    *out_c = (float)msb + (msb >= 0 ? frac : -frac);
    return 0;
}

int ds3231_enable_alarm_interrupts(DS3231 *rtc, bool a1_enable, bool a2_enable)
{
    if (!rtc) return -1;
    uint8_t ctrl;
    if (i2c_read(rtc, DS3231_REG_CONTROL, &ctrl, 1) != HAL_OK) return -2;

    if (a1_enable) ctrl |= DS3231_CTRL_A1IE; else ctrl &= ~DS3231_CTRL_A1IE;
    if (a2_enable) ctrl |= DS3231_CTRL_A2IE; else ctrl &= ~DS3231_CTRL_A2IE;

    if (a1_enable || a2_enable) ctrl |= DS3231_CTRL_INTCN; /* ensure INT/SQW is interrupt */
    /* else leave INTCN as-is (caller may prefer square wave) */

    if (i2c_write(rtc, DS3231_REG_CONTROL, &ctrl, 1) != HAL_OK) return -3;
    return 0;
}

static int write_alarm1_regs(DS3231 *rtc, uint8_t a1s, uint8_t a1m, uint8_t a1h, uint8_t a1dd)
{
    uint8_t buf[4] = { a1s, a1m, a1h, a1dd };
    return (i2c_write(rtc, DS3231_REG_A1_SECONDS, buf, sizeof(buf)) == HAL_OK) ? 0 : -1;
}

static int write_alarm2_regs(DS3231 *rtc, uint8_t a2m, uint8_t a2h, uint8_t a2dd)
{
    uint8_t buf[3] = { a2m, a2h, a2dd };
    return (i2c_write(rtc, DS3231_REG_A2_MINUTES, buf, sizeof(buf)) == HAL_OK) ? 0 : -1;
}

int ds3231_set_alarm1(DS3231 *rtc, const struct tm *t, ds3231_a1_mode_t mode)
{
    if (!rtc || !t) return -1;

    /* Prepare fields with A1M bits. A1M1..A1M4 are bit7 of each byte. DY/DT is bit6 of day/date byte. */
    uint8_t sec = bin2bcd((uint8_t)t->tm_sec) & 0x7F;
    uint8_t min = bin2bcd((uint8_t)t->tm_min) & 0x7F;
    uint8_t hrs = bin2bcd((uint8_t)t->tm_hour) & 0x3F; /* 24h */
    uint8_t daydate = 0;
    bool use_dow = false;

    switch (mode) {
        case DS3231_A1_EVERY_SECOND:
            sec |= 0x80; min |= 0x80; hrs |= 0x80;
            daydate = 0x80; /* A1M4=1, DY/DT don't care */
            break;
        case DS3231_A1_MATCH_S:
            sec &= ~0x80; min |= 0x80; hrs |= 0x80;
            daydate = 0x80;
            break;
        case DS3231_A1_MATCH_MS:
            sec &= ~0x80; min &= ~0x80; hrs |= 0x80;
            daydate = 0x80;
            break;
        case DS3231_A1_MATCH_HMS:
            sec &= ~0x80; min &= ~0x80; hrs &= ~0x80;
            daydate = 0x80;
            break;
        case DS3231_A1_MATCH_DATE_HMS:
            sec &= ~0x80; min &= ~0x80; hrs &= ~0x80;
            daydate = bin2bcd((uint8_t)t->tm_mday) & 0x3F; /* DY/DT=0 -> date */
            /* A1M4=0 -> bit7 clear */
            break;
        case DS3231_A1_MATCH_DOW_HMS:
            sec &= ~0x80; min &= ~0x80; hrs &= ~0x80;
            use_dow = true;
            {
                uint8_t ds_dow = (t->tm_wday % 7) ? (uint8_t)(t->tm_wday % 7) : 1; /* 1..7 */
                daydate = (uint8_t)(0x40 | (ds_dow & 0x07)); /* DY/DT=1 -> day-of-week */
            }
            break;
        default:
            return -2;
    }

    return write_alarm1_regs(rtc, sec, min, hrs, daydate);
}

int ds3231_set_alarm2(DS3231 *rtc, const struct tm *t, ds3231_a2_mode_t mode)
{
    if (!rtc || !t) return -1;

    uint8_t min = bin2bcd((uint8_t)t->tm_min) & 0x7F;
    uint8_t hrs = bin2bcd((uint8_t)t->tm_hour) & 0x3F;
    uint8_t daydate = 0;

    switch (mode) {
        case DS3231_A2_EVERY_MINUTE:
            min |= 0x80; hrs |= 0x80; daydate = 0x80; /* A2M1..A2M3=1 */
            break;
        case DS3231_A2_MATCH_M:
            min &= ~0x80; hrs |= 0x80; daydate = 0x80;
            break;
        case DS3231_A2_MATCH_HM:
            min &= ~0x80; hrs &= ~0x80; daydate = 0x80;
            break;
        case DS3231_A2_MATCH_DATE_HM:
            min &= ~0x80; hrs &= ~0x80;
            daydate = bin2bcd((uint8_t)t->tm_mday) & 0x3F; /* DY/DT=0 date */
            break;
        case DS3231_A2_MATCH_DOW_HM: {
            min &= ~0x80; hrs &= ~0x80;
            uint8_t ds_dow = (t->tm_wday % 7) ? (uint8_t)(t->tm_wday % 7) : 1;
            daydate = (uint8_t)(0x40 | (ds_dow & 0x07)); /* DY/DT=1 dow */
            break;
        }
        default:
            return -2;
    }

    return write_alarm2_regs(rtc, min, hrs, daydate);
}

int ds3231_get_alarm_flags(DS3231 *rtc, ds3231_alarm_flag_t *flags)
{
    if (!rtc || !flags) return -1;
    uint8_t stat;
    if (i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK) return -2;
    uint8_t f = 0;
    if (stat & DS3231_STAT_A1F) f |= DS3231_ALARM1_FLAG;
    if (stat & DS3231_STAT_A2F) f |= DS3231_ALARM2_FLAG;
    *flags = (ds3231_alarm_flag_t)f;
    return 0;
}

int ds3231_clear_alarm_flags(DS3231 *rtc, ds3231_alarm_flag_t flags)
{
    if (!rtc) return -1;
    uint8_t stat;
    if (i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK) return -2;
    if (flags & DS3231_ALARM1_FLAG) stat &= (uint8_t)~DS3231_STAT_A1F;
    if (flags & DS3231_ALARM2_FLAG) stat &= (uint8_t)~DS3231_STAT_A2F;
    if (i2c_write(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK) return -3;
    return 0;
}

int ds3231_acknowledge_alarms(DS3231 *rtc, ds3231_alarm_flag_t flags)
{
    int r = ds3231_clear_alarm_flags(rtc, flags);
    if (r != 0) return r;
    /* No re-enable needed here; enable state is in CONTROL. Keep helper for symmetry. */
    return 0;
}
