/*
 * ds3231.h
 *
 * Instance-based DS3231 RTC driver (STM32 HAL).
 *
 * Notes:
 * - This module uses struct tm for date/time.
 * - The DS3231 calendar range is 2000..2099 (century bit is ignored).
 * - Day-of-week register is written as 1..7 where 1=Sunday (DS3231 convention).
 */

#ifndef DS3231_H_
#define DS3231_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Default I2C timeout (ms) used by the driver. Can be overridden at compile time. */
#ifndef DS3231_I2C_TIMEOUT_MS
#define DS3231_I2C_TIMEOUT_MS         1000U
#endif

/* Typical 7-bit address of DS3231 is 0x68. */
#define DS3231_ADDR_7BIT              (0x68U)
#define DS3231_ADDR_8BIT              ((uint16_t)(DS3231_ADDR_7BIT << 1))

/* ===== Device registers ===== */
#define DS3231_REG_SECONDS            0x00U
#define DS3231_REG_MINUTES            0x01U
#define DS3231_REG_HOURS              0x02U
#define DS3231_REG_DAY                0x03U
#define DS3231_REG_DATE               0x04U
#define DS3231_REG_MONTH              0x05U
#define DS3231_REG_YEAR               0x06U
#define DS3231_REG_A1_SECONDS         0x07U
#define DS3231_REG_A1_MINUTES         0x08U
#define DS3231_REG_A1_HOURS           0x09U
#define DS3231_REG_A1_DAY_DATE        0x0AU
#define DS3231_REG_A2_MINUTES         0x0BU
#define DS3231_REG_A2_HOURS           0x0CU
#define DS3231_REG_A2_DAY_DATE        0x0DU
#define DS3231_REG_CONTROL            0x0EU
#define DS3231_REG_STATUS             0x0FU
#define DS3231_REG_AGING_OFFSET       0x10U
#define DS3231_REG_TEMP_MSB           0x11U
#define DS3231_REG_TEMP_LSB           0x12U

/* ===== Control register bits ===== */
#define DS3231_CTRL_A1IE              (1U << 0)
#define DS3231_CTRL_A2IE              (1U << 1)
#define DS3231_CTRL_INTCN             (1U << 2)
#define DS3231_CTRL_RS1               (1U << 3)
#define DS3231_CTRL_RS2               (1U << 4)
#define DS3231_CTRL_CONV              (1U << 5)
#define DS3231_CTRL_BBSQW             (1U << 6)
#define DS3231_CTRL_EOSC              (1U << 7)

/* ===== Status register bits ===== */
#define DS3231_STAT_A1F               (1U << 0)
#define DS3231_STAT_A2F               (1U << 1)
#define DS3231_STAT_BSY               (1U << 2)
#define DS3231_STAT_EN32KHZ           (1U << 3)
#define DS3231_STAT_OSF               (1U << 7)

/* ===== Instance/handle ===== */
typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint16_t dev_addr;
    uint32_t timeout_ms;
} DS3231_t;

/* Legacy alias (optional). */
#ifndef DS3231_NO_LEGACY_TYPEDEF
typedef DS3231_t DS3231;
#endif


/* Alarm1 match modes (A1M1..A1M4). */
typedef enum
{
    DS3231_A1_EVERY_SECOND = 0,
    DS3231_A1_MATCH_S,
    DS3231_A1_MATCH_MS,
    DS3231_A1_MATCH_HMS,
    DS3231_A1_MATCH_DATE_HMS,
    DS3231_A1_MATCH_DOW_HMS
} ds3231_a1_mode_t;

/* Alarm2 match modes (A2M1..A2M3). */
typedef enum
{
    DS3231_A2_EVERY_MINUTE = 0,
    DS3231_A2_MATCH_M,
    DS3231_A2_MATCH_HM,
    DS3231_A2_MATCH_DATE_HM,
    DS3231_A2_MATCH_DOW_HM
} ds3231_a2_mode_t;

/* Alarm flags bitmask for querying which alarm fired. */
typedef enum
{
    DS3231_ALARM_NONE = 0,
    DS3231_ALARM1_FLAG = (1U << 0),
    DS3231_ALARM2_FLAG = (1U << 1)
} ds3231_alarm_flag_t;

/* ===== API ===== */

/* Initialize instance.
 * addr can be 0x68 (7-bit) or 0xD0/0xD1 (8-bit). The driver will normalize it.
 */
int ds3231_init(DS3231_t *rtc, I2C_HandleTypeDef *hi2c, uint8_t addr);

/* Set per-instance I2C timeout. */
static inline void ds3231_set_timeout(DS3231_t *rtc, uint32_t timeout_ms)
{
    if (rtc != NULL)
    {
        rtc->timeout_ms = timeout_ms;
    }
}

/* Read current time into struct tm.
 * Output:
 * - tm_year is years since 1900 (e.g. 2025 -> 125).
 * - tm_mon is 0..11.
 * - tm_wday is 0..6 (0=Sunday).
 */
int ds3231_get_time(DS3231_t *rtc, struct tm *out);

/* Set RTC time from struct tm.
 * The driver uses 24-hour mode.
 * Supported year range is 2000..2099 (tm_year 100..199).
 */
int ds3231_set_time(DS3231_t *rtc, const struct tm *in);

/* Read die temperature in °C. */
int ds3231_get_temperature(DS3231_t *rtc, float *out_c);

/* Enable/disable alarm interrupts. INTCN=1 is set when any alarm interrupt is enabled. */
int ds3231_enable_alarm_interrupts(DS3231_t *rtc, bool a1_enable, bool a2_enable);

/* Configure Alarm1 using struct tm fields depending on the mode. */
int ds3231_set_alarm1(DS3231_t *rtc, const struct tm *t, ds3231_a1_mode_t mode);

/* Configure Alarm2 using struct tm fields depending on the mode. */
int ds3231_set_alarm2(DS3231_t *rtc, const struct tm *t, ds3231_a2_mode_t mode);

/* Read which alarm(s) fired via status register. Returns bitmask in *flags. */
int ds3231_get_alarm_flags(DS3231_t *rtc, ds3231_alarm_flag_t *flags);

/* Clear alarm flags (any combination of DS3231_ALARM1_FLAG | DS3231_ALARM2_FLAG). */
int ds3231_clear_alarm_flags(DS3231_t *rtc, ds3231_alarm_flag_t flags);

/* Convenience: clear alarm flags (typical after handling an IRQ). */
int ds3231_acknowledge_alarms(DS3231_t *rtc, ds3231_alarm_flag_t flags);

#ifdef __cplusplus
}
#endif

#endif /* DS3231_H_ */
