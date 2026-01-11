#include "hydroponic.h"
#include <stdio.h>
#include <string.h>

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

    if (on_hour < off_hour) {
        return (t->tm_hour >= on_hour) && (t->tm_hour < off_hour);
    }

    return (t->tm_hour >= on_hour) || (t->tm_hour < off_hour);
}

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

static int persist_light_state(Hydroponic_t *self)
{
    HydroponicStorageRecord_t rec;

    /* If load fails, still attempt to write a fresh record */
    if (hydroponic_storage_load(&self->storage, &rec) != 0) {
        memset(&rec, 0, sizeof(rec));
        rec.boot_count = self->boot_count;
    }

    rec.light_on = self->light_is_on;

    if (hydroponic_storage_save(&self->storage, &rec) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        update_error_led(self);
        return -1;
    }

    clear_error_flag(self, HYDROPONIC_ERROR_EEPROM);
    update_error_led(self);
    return 0;
}

static int apply_light_state(Hydroponic_t *self, bool on, bool persist)
{
    self->light_is_on = on ? 1u : 0u;

    if (on) {
        gpio_switch_on(&self->light_sw);
    } else {
        gpio_switch_off(&self->light_sw);
    }

    if (!persist) {
        return 0;
    }

    return persist_light_state(self);
}

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

static int rtc_configure_alarm2_next_light_event(Hydroponic_t *self, const struct tm *now)
{
    uint8_t target_hour = self->cfg.light_on_hour;

    if (is_time_in_light_window(now, self->cfg.light_on_hour, self->cfg.light_off_hour)) {
        target_hour = self->cfg.light_off_hour;
    } else {
        target_hour = self->cfg.light_on_hour;
    }

    /* Alarm2: match hour:minute (minute=0) once per day */
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

    if (rtc_configure_alarm2_next_light_event(self, now) != 0) {
        return -2;
    }

    if (rtc_enable_irqs_and_clear_flags(self) != 0) {
        return -3;
    }

    return 0;
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

    printf("\r\n");
}

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

    struct tm now;
    memset(&now, 0, sizeof(now));

    if (ds3231_get_time(self->cfg.rtc, &now) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);
        return -3;
    }

    /* Load persistent state */
    HydroponicStorageRecord_t rec;
    if (hydroponic_storage_load(&self->storage, &rec) == 0) {
        self->boot_count = (uint16_t)(rec.boot_count + 1u);
        self->light_is_on = (rec.light_on != 0u) ? 1u : 0u;

        rec.boot_count = self->boot_count;
        rec.light_on = self->light_is_on;

        if (hydroponic_storage_save(&self->storage, &rec) != 0) {
            set_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        } else {
            clear_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        }
    } else {
        self->boot_count = 1u;
        self->light_is_on = is_time_in_light_window(&now, self->cfg.light_on_hour, self->cfg.light_off_hour) ? 1u : 0u;

        rec.magic = 0;
        rec.version = 0;
        rec.boot_count = self->boot_count;
        rec.light_on = self->light_is_on;
        rec.crc16 = 0;

        if (hydroponic_storage_save(&self->storage, &rec) != 0) {
            set_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        } else {
            clear_error_flag(self, HYDROPONIC_ERROR_EEPROM);
        }
    }

    (void)apply_light_state(self, (self->light_is_on != 0u), false);

    if (rtc_setup(self, &now) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
    } else {
        clear_error_flag(self, HYDROPONIC_ERROR_RTC);
    }

    update_error_led(self);

    printf("[hydro] init: ");
    log_tm_datetime(&now);
    printf(" | light=%s | boot=%u\r\n",
           (self->light_is_on != 0u) ? "ON" : "OFF",
           (unsigned)self->boot_count);

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

    struct tm now;
    memset(&now, 0, sizeof(now));

    if (ds3231_get_time(self->cfg.rtc, &now) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);
        return -2;
    }

    ds3231_alarm_flag_t flags = DS3231_ALARM_NONE;

    if (ds3231_get_alarm_flags(self->cfg.rtc, &flags) != 0) {
        set_error_flag(self, HYDROPONIC_ERROR_RTC);
        update_error_led(self);

        /* If flags can't be read, still do minute logging to not "go silent" */
        log_sensors(self, &now);
        return -3;
    }

    (void)ds3231_acknowledge_alarms(self->cfg.rtc, flags);

    clear_error_flag(self, HYDROPONIC_ERROR_RTC);

    /* Alarm2: light schedule event */
    if ((flags & DS3231_ALARM2_FLAG) != 0u) {
        bool should_be_on = is_time_in_light_window(&now, self->cfg.light_on_hour, self->cfg.light_off_hour);
        (void)apply_light_state(self, should_be_on, true);

        /* Re-arm next daily event */
        if (rtc_configure_alarm2_next_light_event(self, &now) != 0) {
            set_error_flag(self, HYDROPONIC_ERROR_RTC);
        }

        printf("[hydro] light_update: ");
        log_tm_datetime(&now);
        printf(" | light=%s\r\n", (self->light_is_on != 0u) ? "ON" : "OFF");
    }

    /* Alarm1: periodic minute tick */
    if ((flags & DS3231_ALARM1_FLAG) != 0u) {
        log_sensors(self, &now);
    }

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
