#include "hydroponic.h"
#include <stdio.h>
#include <string.h>

/* ===== Power outage compensation tuning ===== */

/*
Heartbeat period (EEPROM write rate).
We write last_alive + deficit once per 5 minutes to keep wear low but accuracy good.
*/
#ifndef HYDROPONIC_HEARTBEAT_PERIOD_MIN
#define HYDROPONIC_HEARTBEAT_PERIOD_MIN     (5u)
#endif

/*
Power loss detection threshold.
If the gap between "now" and stored last_alive is greater than this value,
we assume MCU was not powered for a while (power outage).
*/
#ifndef HYDROPONIC_POWER_LOSS_DETECT_MIN
#define HYDROPONIC_POWER_LOSS_DETECT_MIN    (5u)
#endif

/*
Optional safety cap for accumulated deficit.
This prevents extremely long compensation (for example, if power was absent for weeks).
Override if you want unlimited (or larger) value.
*/
#ifndef HYDROPONIC_MAX_DEFICIT_MINUTES
#define HYDROPONIC_MAX_DEFICIT_MINUTES      (10080u) /* 7 days */
#endif

/* ===== Small helpers ===== */

static inline void set_error_flag(Hydroponic_t *self, uint8_t flag)
{
    self->error_flags |= flag;
}

static inline void clear_error_flag(Hydroponic_t *self, uint8_t flag)
{
    self->error_flags &= (uint8_t)(~flag);
}

static inline void update_error_led(Hydroponic_t *self)
{
    if (self->error_flags != 0u) {
        gpio_switch_on(&self->error_led_sw);
    } else {
        gpio_switch_off(&self->error_led_sw);
    }
}

static inline void log_tm_datetime(const struct tm *t)
{
    if (t == NULL) {
        printf("0000-00-00 00:00:00");
        return;
    }

    printf("%04u-%02u-%02u %02u:%02u:%02u",
           (unsigned)(t->tm_year + 1900),
           (unsigned)(t->tm_mon + 1),
           (unsigned)t->tm_mday,
           (unsigned)t->tm_hour,
           (unsigned)t->tm_min,
           (unsigned)t->tm_sec);
}

static inline bool is_time_in_light_window(const struct tm *t, uint8_t on_hour, uint8_t off_hour)
{
    if (t == NULL) {
        return false;
    }

    /*
    Scheduled light window is [on_hour, off_hour).
    For default schedule: [07:00, 23:00).
    */
    if (on_hour < off_hour) {
        return (t->tm_hour >= on_hour) && (t->tm_hour < off_hour);
    }

    /* Window across midnight (not used by default, but supported). */
    return (t->tm_hour >= on_hour) || (t->tm_hour < off_hour);
}

static inline uint32_t clamp_add_u32(uint32_t a, uint32_t b, uint32_t cap)
{
    uint32_t r = a + b;
    if (r < a) {
        return cap;
    }
    if (r > cap) {
        return cap;
    }
    return r;
}

/* ===== "Minutes since 2000-01-01" conversion ===== */

static inline bool is_leap_year_2000_2099(uint16_t year)
{
    /* DS3231 range is 2000..2099, leap year rule simplifies to (year % 4 == 0). */
    return ((year % 4u) == 0u);
}

static uint16_t days_before_month(uint16_t year, uint8_t month_0_11)
{
    /* Cumulative days before each month, non-leap year. */
    static const uint16_t cum[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    uint16_t d = cum[month_0_11];
    if (month_0_11 >= 2u && is_leap_year_2000_2099(year)) {
        d += 1u;
    }
    return d;
}

/*
Convert struct tm (calendar) to minutes since 2000-01-01 00:00.

Why this approach:
- Avoids dependency on libc epoch conversions.
- Works in DS3231 supported range.
- Perfect for "heartbeat every N minutes" and overlap calculations.
*/
static uint32_t tm_to_min_2000(const struct tm *t)
{
    uint16_t year = (uint16_t)(t->tm_year + 1900);
    uint8_t month = (uint8_t)t->tm_mon;          /* 0..11 */
    uint8_t day = (uint8_t)t->tm_mday;           /* 1..31 */

    if (year < 2000u) {
        return 0u;
    }

    uint32_t days = 0u;

    for (uint16_t y = 2000u; y < year; y++) {
        days += is_leap_year_2000_2099(y) ? 366u : 365u;
    }

    days += (uint32_t)days_before_month(year, month);
    days += (uint32_t)(day - 1u);

    return (days * 1440u) + ((uint32_t)t->tm_hour * 60u) + (uint32_t)t->tm_min;
}

/* ===== Overlap computation for outages ===== */

/*
Compute how many minutes of [start_min, end_min) overlap with the scheduled light window.

- start_min and end_min are "minutes since 2000-01-01 00:00".
- The schedule window can be normal (on<off) or across midnight (on>off).
- The result is exact in minutes (no rounding), because our heartbeat time base is minutes.
*/
static uint32_t compute_light_overlap_minutes(uint32_t start_min,
                                              uint32_t end_min,
                                              uint16_t on_min_of_day,
                                              uint16_t off_min_of_day)
{
    if (end_min <= start_min) {
        return 0u;
    }

    uint32_t total = 0u;

    uint32_t start_day = start_min / 1440u;
    uint32_t end_day = (end_min - 1u) / 1440u;

    for (uint32_t day = start_day; day <= end_day; day++) {
        uint32_t day_start = day * 1440u;
        uint32_t day_end = day_start + 1440u;

        uint32_t seg_start = (start_min > day_start) ? start_min : day_start;
        uint32_t seg_end = (end_min < day_end) ? end_min : day_end;

        if (seg_end <= seg_start) {
            continue;
        }

        if (on_min_of_day < off_min_of_day) {
            /* Single interval in the same day: [on, off) */
            uint32_t w_start = day_start + (uint32_t)on_min_of_day;
            uint32_t w_end = day_start + (uint32_t)off_min_of_day;

            uint32_t o_start = (seg_start > w_start) ? seg_start : w_start;
            uint32_t o_end = (seg_end < w_end) ? seg_end : w_end;

            if (o_end > o_start) {
                total += (o_end - o_start);
            }
        } else if (on_min_of_day > off_min_of_day) {
            /* Across midnight: [on, 24:00) U [00:00, off) */
            uint32_t w1_start = day_start + (uint32_t)on_min_of_day;
            uint32_t w1_end = day_end;

            uint32_t w2_start = day_start;
            uint32_t w2_end = day_start + (uint32_t)off_min_of_day;

            uint32_t o1_start = (seg_start > w1_start) ? seg_start : w1_start;
            uint32_t o1_end = (seg_end < w1_end) ? seg_end : w1_end;
            if (o1_end > o1_start) {
                total += (o1_end - o1_start);
            }

            uint32_t o2_start = (seg_start > w2_start) ? seg_start : w2_start;
            uint32_t o2_end = (seg_end < w2_end) ? seg_end : w2_end;
            if (o2_end > o2_start) {
                total += (o2_end - o2_start);
            }
        } else {
            /* on == off means "always on", treat as full overlap */
            total += (seg_end - seg_start);
        }
    }

    return total;
}

/* ===== EEPROM state writer ===== */

/*
Write persistent state to EEPROM.

We do NOT write every minute to reduce EEPROM wear.
Instead:
- periodic heartbeat once per 5 minutes
- forced write when compensation mode starts/stops or deficit reaches 0

Saved fields:
- last_alive_min_2000 (used to detect power outage at next boot)
- deficit_minutes (accumulated missing light, will survive power loss)
- boot_count (debug/info)
- light_is_on (debug/info)
*/
static int storage_save_state(Hydroponic_t *self, uint32_t now_min_2000)
{
    HydroponicStorageRecord_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.boot_count = self->boot_count;
    rec.last_alive_min_2000 = now_min_2000;
    rec.deficit_minutes = self->deficit_minutes;
    rec.outage_count = self->outage_count;
    rec.light_is_on = self->light_is_on;

    if (hydroponic_storage_save(&self->storage, &rec) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        update_error_led(self);
        return -1;
    }

    clear_error_flag(self, HYDROPONIC_ERROR_EEPROM);
    update_error_led(self);
    return 0;
}

static void maybe_storage_heartbeat(Hydroponic_t *self, uint32_t now_min_2000, bool force)
{
    uint32_t slot = now_min_2000 / HYDROPONIC_HEARTBEAT_PERIOD_MIN;

    if (force || slot != self->heartbeat_slot) {
        self->heartbeat_slot = slot;
        (void)storage_save_state(self, now_min_2000);
    }
}

/* ===== Light control with compensation ===== */

static void apply_light_switch(Hydroponic_t *self, bool on)
{
    self->light_is_on = on ? 1u : 0u;

    if (on) {
        gpio_switch_on(&self->light_sw);
    } else {
        gpio_switch_off(&self->light_sw);
    }
}

/*
Compute desired light state:
- normal photoperiod window: ON
- otherwise, if there is a deficit: ON (night compensation)
- else: OFF

This guarantees:
- At 23:00, the normal window ends, but the light will stay ON
  if deficit_minutes > 0 (compensation).
- At night, once deficit reaches 0, the light will turn OFF.
*/
static bool compute_desired_light_on(const Hydroponic_t *self, const struct tm *now)
{
    bool normal_on = is_time_in_light_window(now, self->cfg.light_on_hour, self->cfg.light_off_hour);

    if (normal_on) {
        return true;
    }

    if (self->deficit_minutes > 0u) {
        return true;
    }

    return false;
}

/*
Update deficit minutes based on elapsed time and previous compensation state.

Rule:
- We decrement deficit ONLY during "extra light" time, i.e. at night when normal_on is false,
  and the system was compensating in the previous interval.

Why use previous compensation_active:
- It represents what we actually did between last tick and current tick.
- If we used current state only, boundary minutes (23:00 / 07:00) could be counted wrong.
*/
static void update_deficit_by_elapsed(Hydroponic_t *self,
                                     uint32_t now_min_2000,
                                     const struct tm *now_tm)
{
    uint32_t elapsed = 0u;

    if (self->last_process_min_2000 != 0u && now_min_2000 > self->last_process_min_2000) {
        elapsed = now_min_2000 - self->last_process_min_2000;
    }

    /*
    Clamp elapsed to avoid huge decrements if time jumped.
    In normal operation elapsed should be 1 minute.
    */
    if (elapsed > 60u) {
        elapsed = 60u;
    }

    if (elapsed == 0u) {
        return;
    }

    bool normal_on = is_time_in_light_window(now_tm, self->cfg.light_on_hour, self->cfg.light_off_hour);

    /*
    We only decrement deficit when:
    - previous interval was compensation_active
    - the current time is still outside the normal window (night)
    */
    if (self->compensation_active != 0u && !normal_on) {
        if (self->deficit_minutes > elapsed) {
            self->deficit_minutes -= elapsed;
        } else {
            self->deficit_minutes = 0u;
        }
    }
}

/* ===== RTC alarms ===== */

static int rtc_configure_alarm1_minute_tick(Hydroponic_t *self)
{
    /* Alarm1: trigger every minute at second = 0 */
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_sec = 0;

    if (ds3231_set_alarm1(self->cfg.rtc, &t, DS3231_A1_MATCH_S) != 0) {
        return -1;
    }

    return 0;
}

static int rtc_configure_alarm2_next_boundary(Hydroponic_t *self, const struct tm *now)
{
    /*
    Alarm2 is used as a "boundary marker" at 07:00 or 23:00.
    IMPORTANT:
    - We do NOT use Alarm2 as a hard OFF at 23:00 anymore.
    - Light ON/OFF is decided by compute_desired_light_on(), which respects deficit_minutes.
    */
    uint8_t target_hour = self->cfg.light_on_hour;

    if (is_time_in_light_window(now, self->cfg.light_on_hour, self->cfg.light_off_hour)) {
        target_hour = self->cfg.light_off_hour;
    } else {
        target_hour = self->cfg.light_on_hour;
    }

    struct tm a2;
    memset(&a2, 0, sizeof(a2));
    a2.tm_hour = target_hour;
    a2.tm_min = 0;

    if (ds3231_set_alarm2(self->cfg.rtc, &a2, DS3231_A2_MATCH_HM) != 0) {
        return -1;
    }

    return 0;
}

static int rtc_enable_irqs_and_clear_flags(Hydroponic_t *self)
{
    if (ds3231_enable_alarm_interrupts(self->cfg.rtc, true, true) != 0) {
        return -1;
    }

    (void)ds3231_clear_alarm_flags(self->cfg.rtc,
                                   (ds3231_alarm_flag_t)(DS3231_ALARM1_FLAG | DS3231_ALARM2_FLAG));
    return 0;
}

static int rtc_setup(Hydroponic_t *self, const struct tm *now)
{
    if (rtc_configure_alarm1_minute_tick(self) != 0) {
        return -1;
    }

    if (rtc_configure_alarm2_next_boundary(self, now) != 0) {
        return -2;
    }

    if (rtc_enable_irqs_and_clear_flags(self) != 0) {
        return -3;
    }

    return 0;
}

/* ===== Sensors logging ===== */

static void print_x10_u16(uint16_t v_x10)
{
    printf("%u.%u", (unsigned)(v_x10 / 10u), (unsigned)(v_x10 % 10u));
}

static void print_x10_s16(int16_t v_x10)
{
    int16_t v = v_x10;

    if (v < 0) {
        printf("-");
        v = (int16_t)(-v);
    }

    printf("%u.%u", (unsigned)(v / 10), (unsigned)(v % 10));
}

static void log_sensors(Hydroponic_t *self, const struct tm *now)
{
    DHT22_Data_t dht = {0};
    DHT22_Status_t st = dht22_read(self->cfg.dht22, &dht);

    float mcu_t = 0.0f;
    bool mcu_ok = false;

    if (st != DHT22_STATUS_OK) {
        set_error_flag(self, HYDROPONIC_ERROR_DHT22);
    } else {
        clear_error_flag(self, HYDROPONIC_ERROR_DHT22);
    }

    if (self->cfg.mcu_temp_read != NULL) {
        if (self->cfg.mcu_temp_read(self->cfg.mcu_temp_ctx, &mcu_t) == 0) {
            clear_error_flag(self, HYDROPONIC_ERROR_MCU_TEMP);
            mcu_ok = true;
        } else {
            set_error_flag(self, HYDROPONIC_ERROR_MCU_TEMP);
        }
    }

    update_error_led(self);

    printf("[hydro] ");
    log_tm_datetime(now);
    printf(" | light=%s", (self->light_is_on != 0u) ? "ON" : "OFF");

    if ((self->error_flags & HYDROPONIC_ERROR_DHT22) == 0u) {
        printf(" | box=");
        print_x10_s16(dht.temperature_x10);
        printf("C ");
        print_x10_u16(dht.humidity_x10);
        printf("%%");
    } else {
        const char *s = dht22_status_str(st);
        printf(" | box=ERR(%d,%s)", (int)st, (s != NULL) ? s : "?");
    }

    if (self->cfg.mcu_temp_read != NULL) {
        if (mcu_ok) {
            printf(" | mcu=%.2fC", (double)mcu_t);
        } else {
            printf(" | mcu=ERR");
        }
    } else {
        printf(" | mcu=N/A");
    }

    printf(" | deficit=%lu min", (unsigned long)self->deficit_minutes);
    printf(" | outages=%lu", (unsigned long)self->outage_count);
    printf("\r\n");
}

/* ===== Public API ===== */

int hydroponic_init(Hydroponic_t *self, const HydroponicConfig_t *cfg)
{
    if (self == NULL || cfg == NULL) {
        return -1;
    }

    memset(self, 0, sizeof(*self));
    self->cfg = *cfg;

    if (self->cfg.rtc == NULL || self->cfg.dht22 == NULL || self->cfg.eeprom == NULL) {
        return -2;
    }

    if (self->cfg.light_on_hour == 0u && self->cfg.light_off_hour == 0u) {
        self->cfg.light_on_hour = 7u;
        self->cfg.light_off_hour = 23u;
    }

    gpio_switch_init(&self->light_sw,
                     self->cfg.light_pin,
                     self->cfg.light_active_level,
                     GPIO_SWITCH_STATE_OFF);

    gpio_switch_init(&self->error_led_sw,
                     self->cfg.error_led_pin,
                     self->cfg.error_led_active_level,
                     GPIO_SWITCH_STATE_OFF);

    hydroponic_storage_init(&self->storage, self->cfg.eeprom, self->cfg.eeprom_base_addr);

    struct tm now_tm;
    memset(&now_tm, 0, sizeof(now_tm));

    if (ds3231_get_time(self->cfg.rtc, &now_tm) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);
        return -3;
    }

    uint32_t now_min_2000 = tm_to_min_2000(&now_tm);

    /*
    ===== Power outage compensation boot logic =====

    We keep last_alive_min_2000 in EEPROM and update it every 5 minutes.
    On boot we compare current time with stored last_alive.
    If the gap is > 5 minutes, we assume the MCU was not powered.

    We then compute how many minutes of that gap overlap with the "normal light window"
    (07:00..23:00). Only those minutes become "deficit_minutes".

    deficit_minutes accumulates across multiple outages.
    */
    HydroponicStorageRecord_t rec;
    if (hydroponic_storage_load(&self->storage, &rec) == 0) {
        self->boot_count = (uint16_t)(rec.boot_count + 1u);
        self->deficit_minutes = rec.deficit_minutes;
        self->outage_count = rec.outage_count;

        if (rec.last_alive_min_2000 != 0u && now_min_2000 > rec.last_alive_min_2000) {
            uint32_t gap_min = now_min_2000 - rec.last_alive_min_2000;

            if (gap_min > HYDROPONIC_POWER_LOSS_DETECT_MIN) {
                /*
                Outage counter:
                We count any "long gap" between heartbeats as an outage.
                It does not necessarily mean missed light (could be night),
                but it's still useful statistics.
                */
                if (self->outage_count != 0xFFFFFFFFu) {
                    self->outage_count++;
                }

                uint16_t on_min = (uint16_t)self->cfg.light_on_hour * 60u;
                uint16_t off_min = (uint16_t)self->cfg.light_off_hour * 60u;

                uint32_t missed = compute_light_overlap_minutes(rec.last_alive_min_2000,
                                                               now_min_2000,
                                                               on_min,
                                                               off_min);

                if (missed > 0u) {
                    self->deficit_minutes = clamp_add_u32(self->deficit_minutes,
                                                          missed,
                                                          HYDROPONIC_MAX_DEFICIT_MINUTES);

                    printf("[hydro] power_outage detected: gap=%lu min, missed_light=%lu min, deficit=%lu min, outages=%lu\r\n",
                           (unsigned long)gap_min,
                           (unsigned long)missed,
                           (unsigned long)self->deficit_minutes,
                           (unsigned long)self->outage_count);
                } else {
                    printf("[hydro] power_outage detected: gap=%lu min (no missed light) | outages=%lu\r\n",
                           (unsigned long)gap_min,
                           (unsigned long)self->outage_count);
                }
            }
        }
    } else {
        /* First run (or incompatible record) */
        self->boot_count = 1u;
        self->deficit_minutes = 0u;
        self->outage_count = 0u;
    }

    /* Initialize internal timing state */
    self->last_process_min_2000 = now_min_2000;
    self->heartbeat_slot = 0xFFFFFFFFu;

    /* Apply light state based on schedule + deficit */
    bool desired_on = compute_desired_light_on(self, &now_tm);
    apply_light_switch(self, desired_on);

    /* Determine whether we are currently compensating (night extra light) */
    self->compensation_active = (uint8_t)((!is_time_in_light_window(&now_tm,
                                                                    self->cfg.light_on_hour,
                                                                    self->cfg.light_off_hour)) &&
                                          (self->deficit_minutes > 0u));

    if (rtc_setup(self, &now_tm) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
    } else {
        clear_error_flag(self, HYDROPONIC_ERROR_RTC);
    }

    update_error_led(self);

    /* Force-save state at boot (stores last_alive + deficit + boot_count) */
    maybe_storage_heartbeat(self, now_min_2000, true);

    printf("[hydro] init: ");
    log_tm_datetime(&now_tm);
    printf(" | light=%s | boot=%u | deficit=%lu min\r\n",
           (self->light_is_on != 0u) ? "ON" : "OFF",
           (unsigned)self->boot_count,
           (unsigned long)self->deficit_minutes);

    printf(" | outages=%lu\r\n", (unsigned long)self->outage_count);

    return 0;
}

void hydroponic_exti_irq_handler(Hydroponic_t *self, uint16_t gpio_pin)
{
    if (self == NULL) {
        return;
    }

    if (gpio_pin == self->cfg.rtc_int_pin) {
        self->rtc_irq_pending = 1u;
    }
}

int hydroponic_process(Hydroponic_t *self)
{
    if (self == NULL) {
        return -1;
    }

    if (self->rtc_irq_pending == 0u) {
        return 0;
    }

    self->rtc_irq_pending = 0u;

    struct tm now_tm;
    memset(&now_tm, 0, sizeof(now_tm));

    if (ds3231_get_time(self->cfg.rtc, &now_tm) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);
        return -2;
    }

    uint32_t now_min_2000 = tm_to_min_2000(&now_tm);

    ds3231_alarm_flag_t flags = DS3231_ALARM_NONE;
    if (ds3231_get_alarm_flags(self->cfg.rtc, &flags) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);
        return -3;
    }

    (void)ds3231_acknowledge_alarms(self->cfg.rtc, flags);
    clear_error_flag(self, HYDROPONIC_ERROR_RTC);

    /*
    ===== Compensation runtime logic =====

    1) If we were compensating in the previous interval (night extra light),
       decrement deficit by elapsed minutes since last processing.

    2) Recompute desired light state:
       - ON during normal window
       - OR ON during night if deficit > 0

    3) If compensation starts/stops or deficit reaches 0, force-save state to EEPROM.
       Otherwise save periodically once per 5 minutes.
    */
    update_deficit_by_elapsed(self, now_min_2000, &now_tm);

    bool normal_on = is_time_in_light_window(&now_tm, self->cfg.light_on_hour, self->cfg.light_off_hour);
    uint8_t new_comp = (uint8_t)((!normal_on) && (self->deficit_minutes > 0u));

    bool desired_on = compute_desired_light_on(self, &now_tm);

    if (desired_on != (self->light_is_on != 0u)) {
        apply_light_switch(self, desired_on);
    }

    /* Re-arm Alarm2 boundary marker */
    if ((flags & DS3231_ALARM2_FLAG) != 0u) {
        (void)rtc_configure_alarm2_next_boundary(self, &now_tm);
    }

    /* Sensor logging on each minute tick */
    if ((flags & DS3231_ALARM1_FLAG) != 0u) {
        log_sensors(self, &now_tm);
    }

    bool force_save = false;

    /* If compensation mode changed, persist immediately */
    if (new_comp != self->compensation_active) {
        force_save = true;
        printf("[hydro] compensation %s | deficit=%lu min\r\n",
               (new_comp != 0u) ? "START" : "STOP",
               (unsigned long)self->deficit_minutes);
    }

    /* If deficit reached zero while in night, persist immediately */
    if (self->compensation_active != 0u && new_comp == 0u) {
        force_save = true;
    }

    self->compensation_active = new_comp;
    self->last_process_min_2000 = now_min_2000;

    maybe_storage_heartbeat(self, now_min_2000, force_save);

    update_error_led(self);
    return 0;
}

uint8_t hydroponic_get_error_flags(const Hydroponic_t *self)
{
    if (self == NULL) {
        return 0u;
    }
    return self->error_flags;
}

uint8_t hydroponic_is_light_on(const Hydroponic_t *self)
{
    if (self == NULL) {
        return 0u;
    }
    return self->light_is_on;
}
