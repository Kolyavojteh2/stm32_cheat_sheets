/*
 * ds3231.h (instance-based, struct tm, alarm support)
 *
 * 2025-09-02
 *
 * This module provides a lightweight DS3231 driver for STM32 HAL.
 * It uses `struct tm` instead of custom datetime types and supports
 * Alarm1 and Alarm2 configuration with multiple match modes.
 *
 * All comments are in English by request.
 */

#ifndef DS3231_H_
#define DS3231_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/* Typical 7-bit address of DS3231 is 0x68. STM32 HAL expects left-shifted (8-bit).
 * The driver will normalize either form automatically in ds3231_init(). */
#define DS3231_ADDR_7BIT   (0x68U)

/* ===== Device registers (fixed) ===== */
#define DS3231_REG_SECONDS         0x00
#define DS3231_REG_MINUTES         0x01
#define DS3231_REG_HOURS           0x02
#define DS3231_REG_DAY             0x03  /* 1..7 */
#define DS3231_REG_DATE            0x04  /* 1..31 */
#define DS3231_REG_MONTH           0x05  /* bit7 = Century */
#define DS3231_REG_YEAR            0x06  /* 00..99 (offset 2000 in this driver) */
#define DS3231_REG_A1_SECONDS      0x07
#define DS3231_REG_A1_MINUTES      0x08
#define DS3231_REG_A1_HOURS        0x09
#define DS3231_REG_A1_DAYDATE      0x0A
#define DS3231_REG_A2_MINUTES      0x0B
#define DS3231_REG_A2_HOURS        0x0C
#define DS3231_REG_A2_DAYDATE      0x0D
#define DS3231_REG_CONTROL         0x0E
#define DS3231_REG_STATUS          0x0F
#define DS3231_REG_TEMP_MSB        0x11 /* MSB, 2's complement, LSB at 0x12 (bits 7:6) */

/* ===== Control/Status bits ===== */
#define DS3231_CTRL_A1IE      (1U << 0)
#define DS3231_CTRL_A2IE      (1U << 1)
#define DS3231_CTRL_INTCN     (1U << 2)
#define DS3231_CTRL_RS1       (1U << 3)
#define DS3231_CTRL_RS2       (1U << 4)
#define DS3231_CTRL_CONV      (1U << 5)
#define DS3231_CTRL_BBSQW     (1U << 6)
#define DS3231_CTRL_EOSC      (1U << 7)

#define DS3231_STAT_A1F       (1U << 0)
#define DS3231_STAT_A2F       (1U << 1)
#define DS3231_STAT_BSY       (1U << 2)
#define DS3231_STAT_EN32KHZ   (1U << 3)
#define DS3231_STAT_OSF       (1U << 7)

/* Instance/handle */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t dev_addr; /* STM32 HAL expects 8-bit address (shifted). */
} DS3231;

/* Alarm1 match modes (A1M1..A1M4) */
typedef enum {
    DS3231_A1_EVERY_SECOND = 0,          /* A1M1..A1M4 = 1 */
    DS3231_A1_MATCH_S,                   /* match seconds; others "don't care" */
    DS3231_A1_MATCH_MS,                  /* match minutes & seconds */
    DS3231_A1_MATCH_HMS,                 /* match hours, minutes & seconds */
    DS3231_A1_MATCH_DATE_HMS,            /* match date (day-of-month), hours, minutes, seconds */
    DS3231_A1_MATCH_DOW_HMS              /* match day-of-week (1..7), hours, minutes, seconds */
} ds3231_a1_mode_t;

/* Alarm2 match modes (A2M1..A2M3) */
typedef enum {
    DS3231_A2_EVERY_MINUTE = 0,          /* A2M1..A2M3 = 1 */
    DS3231_A2_MATCH_M,                   /* match minutes */
    DS3231_A2_MATCH_HM,                  /* match hours & minutes */
    DS3231_A2_MATCH_DATE_HM,             /* match date (day-of-month), hours & minutes */
    DS3231_A2_MATCH_DOW_HM               /* match day-of-week (1..7), hours & minutes */
} ds3231_a2_mode_t;

/* Alarm flags bitmask for querying which alarm fired */
typedef enum {
    DS3231_ALARM_NONE = 0,
    DS3231_ALARM1_FLAG = 1 << 0,
    DS3231_ALARM2_FLAG = 1 << 1
} ds3231_alarm_flag_t;

/* ========= API ========= */

/* Initialize instance. addr can be 0x68 (7-bit) or 0xD0/0xD1 (8-bit R/W).
 * Returns 0 on success. */
int ds3231_init(DS3231 *rtc, I2C_HandleTypeDef *hi2c, uint8_t addr);

/* Read current time into `tm`. Years are interpreted as 2000+tm_year (tm_year = years since 1900).
 * On input: tm_year is ignored. On output: tm_year is full years since 1900. */
int ds3231_get_time(DS3231 *rtc, struct tm *out);

/* Set RTC time from `tm`. Uses 24h mode. Year range 2000..2099. */
int ds3231_set_time(DS3231 *rtc, const struct tm *in);

/* Read die temperature in °C. */
int ds3231_get_temperature(DS3231 *rtc, float *out_c);

/* Enable/disable alarm interrupts. INTCN=1 is set when any alarm interrupt is enabled. */
int ds3231_enable_alarm_interrupts(DS3231 *rtc, bool a1_enable, bool a2_enable);

/* Configure Alarm1 according to mode and provided time fields (in `tm`). 
 * For date-based modes, tm_mday is used. For weekday-based modes, tm_wday (0=Sunday) is used. */
int ds3231_set_alarm1(DS3231 *rtc, const struct tm *t, ds3231_a1_mode_t mode);

/* Configure Alarm2 according to mode and provided time fields. */
int ds3231_set_alarm2(DS3231 *rtc, const struct tm *t, ds3231_a2_mode_t mode);

/* Read which alarm(s) fired via status register. Returns bitmask in *flags. */
int ds3231_get_alarm_flags(DS3231 *rtc, ds3231_alarm_flag_t *flags);

/* Clear alarm flags (any combination of DS3231_ALARM1_FLAG | DS3231_ALARM2_FLAG). */
int ds3231_clear_alarm_flags(DS3231 *rtc, ds3231_alarm_flag_t flags);

/* Convenience: clear then re-enable selected alarms (typical after handling an IRQ). */
int ds3231_acknowledge_alarms(DS3231 *rtc, ds3231_alarm_flag_t flags);

#ifdef __cplusplus
}
#endif

#endif /* DS3231_H_ */
