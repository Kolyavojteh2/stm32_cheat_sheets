/*
 * ds3231.c
 *
 * Instance-based DS3231 RTC driver (STM32 HAL).
 */

#include "ds3231.h"
#include <string.h>

/* ===== Internal helpers ===== */

static inline uint16_t ds3231_normalize_addr(uint8_t addr)
{
    /* Accept 7-bit (0x68) or 8-bit (0xD0/0xD1). Ensure HAL-style 8-bit address with R/W bit cleared. */
    if (addr < 0x80U)
    {
        return (uint16_t)((uint16_t)addr << 1);
    }

    return (uint16_t)((uint16_t)addr & 0xFEU);
}

static inline uint8_t ds3231_bcd2bin(uint8_t val)
{
    return (uint8_t)((val & 0x0FU) + 10U * ((val >> 4) & 0x0FU));
}

static inline uint8_t ds3231_bin2bcd(uint8_t val)
{
    return (uint8_t)(((val / 10U) << 4) | (val % 10U));
}

static uint8_t ds3231_calc_wday_0_sun(int year, int month_1_12, int day_1_31)
{
    /* Sakamoto algorithm: returns 0=Sunday..6=Saturday. */
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = year;
    int m = month_1_12;

    if (m < 3)
    {
        y -= 1;
    }

    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + day_1_31) % 7);
}

/* ===== HAL wrappers ===== */

static inline HAL_StatusTypeDef ds3231_i2c_read(DS3231_t *rtc, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(rtc->hi2c, rtc->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, len, rtc->timeout_ms);
}

static inline HAL_StatusTypeDef ds3231_i2c_write(DS3231_t *rtc, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(rtc->hi2c, rtc->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, rtc->timeout_ms);
}

static int ds3231_validate_tm_for_set(const struct tm *in)
{
    if (in == NULL)
    {
        return -1;
    }

    if (in->tm_year < 100 || in->tm_year > 199)
    {
        return -2;
    }

    if (in->tm_mon < 0 || in->tm_mon > 11)
    {
        return -3;
    }

    if (in->tm_mday < 1 || in->tm_mday > 31)
    {
        return -4;
    }

    if (in->tm_hour < 0 || in->tm_hour > 23)
    {
        return -5;
    }

    if (in->tm_min < 0 || in->tm_min > 59)
    {
        return -6;
    }

    if (in->tm_sec < 0 || in->tm_sec > 59)
    {
        return -7;
    }

    return 0;
}

/* ===== Public API ===== */

int ds3231_init(DS3231_t *rtc, I2C_HandleTypeDef *hi2c, uint8_t addr)
{
    if (rtc == NULL || hi2c == NULL)
    {
        return -1;
    }

    memset(rtc, 0, sizeof(*rtc));
    rtc->hi2c = hi2c;
    rtc->dev_addr = ds3231_normalize_addr(addr);
    rtc->timeout_ms = DS3231_I2C_TIMEOUT_MS;

    /* Sanity read from status register. */
    uint8_t stat = 0U;
    if (ds3231_i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK)
    {
        return -2;
    }

    return 0;
}

int ds3231_get_time(DS3231_t *rtc, struct tm *out)
{
    if (rtc == NULL || out == NULL)
    {
        return -1;
    }

    uint8_t buf[7];
    if (ds3231_i2c_read(rtc, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK)
    {
        return -2;
    }

    uint8_t sec = ds3231_bcd2bin(buf[0] & 0x7FU);
    uint8_t min = ds3231_bcd2bin(buf[1] & 0x7FU);

    uint8_t hr_raw = buf[2];
    uint8_t hrs = 0U;
    if ((hr_raw & 0x40U) != 0U)
    {
        /* 12-hour mode. */
        hrs = ds3231_bcd2bin(hr_raw & 0x1FU);
        bool pm = ((hr_raw & 0x20U) != 0U);
        if (pm && hrs < 12U)
        {
            hrs = (uint8_t)(hrs + 12U);
        }
        if (!pm && hrs == 12U)
        {
            hrs = 0U;
        }
    }
    else
    {
        /* 24-hour mode. */
        hrs = ds3231_bcd2bin(hr_raw & 0x3FU);
    }

    uint8_t wday_ds = (uint8_t)(buf[3] & 0x07U);       /* Expected: 1..7 (1=Sunday). */
    uint8_t mday = ds3231_bcd2bin(buf[4] & 0x3FU);
    uint8_t month = ds3231_bcd2bin(buf[5] & 0x1FU);    /* 1..12 (century bit ignored). */
    uint8_t year = ds3231_bcd2bin(buf[6]);             /* 00..99 -> 2000..2099 */

    if (sec > 59U || min > 59U || hrs > 23U || mday < 1U || mday > 31U || month < 1U || month > 12U)
    {
        return -3;
    }

    memset(out, 0, sizeof(*out));
    out->tm_sec = (int)sec;
    out->tm_min = (int)min;
    out->tm_hour = (int)hrs;
    out->tm_mday = (int)mday;
    out->tm_mon = (int)month - 1;
    out->tm_year = 100 + (int)year;

    if (wday_ds >= 1U && wday_ds <= 7U)
    {
        out->tm_wday = (int)(wday_ds - 1U); /* 0=Sunday..6=Saturday */
    }
    else
    {
        int full_year = out->tm_year + 1900;
        uint8_t w = ds3231_calc_wday_0_sun(full_year, (int)month, (int)mday);
        out->tm_wday = (int)w;
    }

    return 0;
}

int ds3231_set_time(DS3231_t *rtc, const struct tm *in)
{
    if (rtc == NULL || in == NULL)
    {
        return -1;
    }

    int vr = ds3231_validate_tm_for_set(in);
    if (vr != 0)
    {
        return vr;
    }

    int year_full = in->tm_year + 1900;
    uint8_t year_00_99 = (uint8_t)(in->tm_year - 100);
    uint8_t month_1_12 = (uint8_t)(in->tm_mon + 1);

    uint8_t wday_ds = 0U;
    if (in->tm_wday >= 0 && in->tm_wday <= 6)
    {
        wday_ds = (uint8_t)(in->tm_wday + 1); /* DS: 1=Sunday..7=Saturday */
    }
    else
    {
        uint8_t w = ds3231_calc_wday_0_sun(year_full, (int)month_1_12, in->tm_mday);
        wday_ds = (uint8_t)(w + 1U);
    }

    uint8_t buf[7];
    buf[0] = (uint8_t)(ds3231_bin2bcd((uint8_t)in->tm_sec) & 0x7FU);
    buf[1] = (uint8_t)(ds3231_bin2bcd((uint8_t)in->tm_min) & 0x7FU);
    buf[2] = (uint8_t)(ds3231_bin2bcd((uint8_t)in->tm_hour) & 0x3FU); /* Force 24-hour mode. */
    buf[3] = (uint8_t)(wday_ds & 0x07U);
    buf[4] = (uint8_t)(ds3231_bin2bcd((uint8_t)in->tm_mday) & 0x3FU);
    buf[5] = (uint8_t)(ds3231_bin2bcd(month_1_12) & 0x1FU);           /* Ignore century bit. */
    buf[6] = ds3231_bin2bcd(year_00_99);

    if (ds3231_i2c_write(rtc, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK)
    {
        return -8;
    }

    /* Clear OSF (Oscillator Stop Flag) if set. */
    uint8_t stat = 0U;
    if (ds3231_i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) == HAL_OK)
    {
        if ((stat & DS3231_STAT_OSF) != 0U)
        {
            stat = (uint8_t)(stat & (uint8_t)~DS3231_STAT_OSF);
            (void)ds3231_i2c_write(rtc, DS3231_REG_STATUS, &stat, 1);
        }
    }

    return 0;
}

int ds3231_get_temperature(DS3231_t *rtc, float *out_c)
{
    if (rtc == NULL || out_c == NULL)
    {
        return -1;
    }

    uint8_t t[2];
    if (ds3231_i2c_read(rtc, DS3231_REG_TEMP_MSB, t, 2) != HAL_OK)
    {
        return -2;
    }

    /* Temperature is a signed 10-bit two's complement value: MSB is integer, LSB[7:6] are quarters. */
    int8_t msb = (int8_t)t[0];
    float frac = (float)((t[1] >> 6) & 0x03U) * 0.25f;
    *out_c = (float)msb + frac;

    return 0;
}

int ds3231_enable_alarm_interrupts(DS3231_t *rtc, bool a1_enable, bool a2_enable)
{
    if (rtc == NULL)
    {
        return -1;
    }

    uint8_t ctrl = 0U;
    if (ds3231_i2c_read(rtc, DS3231_REG_CONTROL, &ctrl, 1) != HAL_OK)
    {
        return -2;
    }

    if (a1_enable)
    {
        ctrl = (uint8_t)(ctrl | DS3231_CTRL_A1IE);
    }
    else
    {
        ctrl = (uint8_t)(ctrl & (uint8_t)~DS3231_CTRL_A1IE);
    }

    if (a2_enable)
    {
        ctrl = (uint8_t)(ctrl | DS3231_CTRL_A2IE);
    }
    else
    {
        ctrl = (uint8_t)(ctrl & (uint8_t)~DS3231_CTRL_A2IE);
    }

    if (a1_enable || a2_enable)
    {
        /* Ensure INT/SQW pin works as interrupt. */
        ctrl = (uint8_t)(ctrl | DS3231_CTRL_INTCN);
    }

    if (ds3231_i2c_write(rtc, DS3231_REG_CONTROL, &ctrl, 1) != HAL_OK)
    {
        return -3;
    }

    return 0;
}

static int ds3231_write_alarm1_regs(DS3231_t *rtc, uint8_t a1s, uint8_t a1m, uint8_t a1h, uint8_t a1dd)
{
    uint8_t buf[4] = { a1s, a1m, a1h, a1dd };
    return (ds3231_i2c_write(rtc, DS3231_REG_A1_SECONDS, buf, sizeof(buf)) == HAL_OK) ? 0 : -1;
}

static int ds3231_write_alarm2_regs(DS3231_t *rtc, uint8_t a2m, uint8_t a2h, uint8_t a2dd)
{
    uint8_t buf[3] = { a2m, a2h, a2dd };
    return (ds3231_i2c_write(rtc, DS3231_REG_A2_MINUTES, buf, sizeof(buf)) == HAL_OK) ? 0 : -1;
}

int ds3231_set_alarm1(DS3231_t *rtc, const struct tm *t, ds3231_a1_mode_t mode)
{
    if (rtc == NULL || t == NULL)
    {
        return -1;
    }

    if (t->tm_sec < 0 || t->tm_sec > 59 || t->tm_min < 0 || t->tm_min > 59 || t->tm_hour < 0 || t->tm_hour > 23)
    {
        return -2;
    }

    uint8_t sec = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_sec) & 0x7FU);
    uint8_t min = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_min) & 0x7FU);
    uint8_t hrs = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_hour) & 0x3FU);
    uint8_t daydate = 0U;

    /* A1M1..A1M4 are bit7 of each byte. DY/DT is bit6 of day/date byte. */
    switch (mode)
    {
        case DS3231_A1_EVERY_SECOND:
            sec = (uint8_t)(sec | 0x80U);
            min = (uint8_t)(min | 0x80U);
            hrs = (uint8_t)(hrs | 0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A1_MATCH_S:
            sec = (uint8_t)(sec & (uint8_t)~0x80U);
            min = (uint8_t)(min | 0x80U);
            hrs = (uint8_t)(hrs | 0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A1_MATCH_MS:
            sec = (uint8_t)(sec & (uint8_t)~0x80U);
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs | 0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A1_MATCH_HMS:
            sec = (uint8_t)(sec & (uint8_t)~0x80U);
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A1_MATCH_DATE_HMS:
            if (t->tm_mday < 1 || t->tm_mday > 31)
            {
                return -3;
            }
            sec = (uint8_t)(sec & (uint8_t)~0x80U);
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_mday) & 0x3FU); /* DY/DT=0 (date) */
            break;

        case DS3231_A1_MATCH_DOW_HMS:
            if (t->tm_wday < 0 || t->tm_wday > 6)
            {
                return -4;
            }
            sec = (uint8_t)(sec & (uint8_t)~0x80U);
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = (uint8_t)(0x40U | (uint8_t)((t->tm_wday + 1) & 0x07U)); /* DY/DT=1 (day-of-week) */
            break;

        default:
            return -5;
    }

    return ds3231_write_alarm1_regs(rtc, sec, min, hrs, daydate);
}

int ds3231_set_alarm2(DS3231_t *rtc, const struct tm *t, ds3231_a2_mode_t mode)
{
    if (rtc == NULL || t == NULL)
    {
        return -1;
    }

    if (t->tm_min < 0 || t->tm_min > 59 || t->tm_hour < 0 || t->tm_hour > 23)
    {
        return -2;
    }

    uint8_t min = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_min) & 0x7FU);
    uint8_t hrs = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_hour) & 0x3FU);
    uint8_t daydate = 0U;

    /* A2M1..A2M3 are bit7 of each byte. DY/DT is bit6 of day/date byte. */
    switch (mode)
    {
        case DS3231_A2_EVERY_MINUTE:
            min = (uint8_t)(min | 0x80U);
            hrs = (uint8_t)(hrs | 0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A2_MATCH_M:
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs | 0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A2_MATCH_HM:
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = 0x80U;
            break;

        case DS3231_A2_MATCH_DATE_HM:
            if (t->tm_mday < 1 || t->tm_mday > 31)
            {
                return -3;
            }
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = (uint8_t)(ds3231_bin2bcd((uint8_t)t->tm_mday) & 0x3FU); /* DY/DT=0 (date) */
            break;

        case DS3231_A2_MATCH_DOW_HM:
            if (t->tm_wday < 0 || t->tm_wday > 6)
            {
                return -4;
            }
            min = (uint8_t)(min & (uint8_t)~0x80U);
            hrs = (uint8_t)(hrs & (uint8_t)~0x80U);
            daydate = (uint8_t)(0x40U | (uint8_t)((t->tm_wday + 1) & 0x07U)); /* DY/DT=1 (day-of-week) */
            break;

        default:
            return -5;
    }

    return ds3231_write_alarm2_regs(rtc, min, hrs, daydate);
}

int ds3231_get_alarm_flags(DS3231_t *rtc, ds3231_alarm_flag_t *flags)
{
    if (rtc == NULL || flags == NULL)
    {
        return -1;
    }

    uint8_t stat = 0U;
    if (ds3231_i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK)
    {
        return -2;
    }

    uint8_t f = 0U;
    if ((stat & DS3231_STAT_A1F) != 0U)
    {
        f = (uint8_t)(f | (uint8_t)DS3231_ALARM1_FLAG);
    }
    if ((stat & DS3231_STAT_A2F) != 0U)
    {
        f = (uint8_t)(f | (uint8_t)DS3231_ALARM2_FLAG);
    }

    *flags = (ds3231_alarm_flag_t)f;
    return 0;
}

int ds3231_clear_alarm_flags(DS3231_t *rtc, ds3231_alarm_flag_t flags)
{
    if (rtc == NULL)
    {
        return -1;
    }

    uint8_t stat = 0U;
    if (ds3231_i2c_read(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK)
    {
        return -2;
    }

    if ((flags & DS3231_ALARM1_FLAG) != 0)
    {
        stat = (uint8_t)(stat & (uint8_t)~DS3231_STAT_A1F);
    }
    if ((flags & DS3231_ALARM2_FLAG) != 0)
    {
        stat = (uint8_t)(stat & (uint8_t)~DS3231_STAT_A2F);
    }

    if (ds3231_i2c_write(rtc, DS3231_REG_STATUS, &stat, 1) != HAL_OK)
    {
        return -3;
    }

    return 0;
}

int ds3231_acknowledge_alarms(DS3231_t *rtc, ds3231_alarm_flag_t flags)
{
    return ds3231_clear_alarm_flags(rtc, flags);
}
