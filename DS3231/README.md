# DS3231 RTC

## How to use
If your RTC already has the current date and time set, just use the API from the `ds3231.*` files.
### Initialization
```
DS3231 rtc;

void rtc_init(void) {
    // If you used 0xD0 before, you may pass 0x68 or 0xD0 here; driver normalizes.
    if (ds3231_init(&rtc, &hi2c1, 0x68) != 0) {
        // handle error
    }

    // Enable only Alarm1 interrupt, for example:
    ds3231_enable_alarm_interrupts(&rtc, true, false);
}
```

### Usage
```
void rtc_set_time_example(void) {
    struct tm t = {0};
    // tm_year: years since 1900; e.g., 2025 -> 125
    t.tm_year = 125;   // 2025
    t.tm_mon  = 8;     // September (0-based)
    t.tm_mday = 2;
    t.tm_hour = 14;
    t.tm_min  = 30;
    t.tm_sec  = 0;
    t.tm_wday = 2;     // Tuesday (0=Sun..6=Sat), optional

    ds3231_set_time(&rtc, &t);
}

void rtc_set_alarm1_exact(void) {
    struct tm a = {0};
    a.tm_hour = 14;
    a.tm_min  = 31;
    a.tm_sec  = 0;
    a.tm_mday = 2;  // if using DATE mode
    a.tm_wday = 2;  // if using DOW mode (0=Sun..6)
    // Match at exact H:M:S on a specific date:
    ds3231_set_alarm1(&rtc, &a, DS3231_A1_MATCH_DATE_HMS);
}

void rtc_irq_handler_like(void) {
    // Call this from your EXTI/IRQ code when the INT/SQW pin asserts:
    ds3231_alarm_flag_t f = DS3231_ALARM_NONE;
    if (ds3231_get_alarm_flags(&rtc, &f) == 0 && f != DS3231_ALARM_NONE) {
        // Decide what to do by which alarm fired:
        if (f & DS3231_ALARM1_FLAG) {
            // handle A1
        }
        if (f & DS3231_ALARM2_FLAG) {
            // handle A2
        }
        // Clear flags so INT pin deasserts:
        ds3231_acknowledge_alarms(&rtc, f);
    }
}
```

## Standard time.h functions hook
Instead of calling driver functions directly to get the time, we can override standard functions that will internally use an external RTC.
### Initialization

```
DS3231 rtc;

void app_time_init(void) {
    ds3231_time_bridge_attach(&rtc);

    // Optionally sync system time base from RTC
    ds3231_sync_system_from_rtc();
}
```

### Usage
```
void example(void) {
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t); // UTC broken-down time

    // Set time via settimeofday()
    struct timeval tv = {.tv_sec = 1730563200, .tv_usec = 0}; // e.g., 2024-11-03 00:00:00 UTC
    settimeofday(&tv, NULL);
}
```

```
#include "ds3231.h"
#include "ds3231_time_bridge.h"

DS3231_t rtc;

ds3231_init(&rtc, &hi2c1, 0x68);
ds3231_time_bridge_attach(&rtc);

```

## How to set the current time in RTC
To do this, you need to additionally copy the ds3231_configure.* API, but this is only needed to load the time to the RTC once.
### Steps to set the current time using UART
#### MCU side:
1. Copy the `ds3231.*` and `ds3231_configure.*` files
2. Add UART initialization for the RTC in the microcontroller code
    ```
    ds3231_set_uart_handle(&huart2);
    ```
3. Add the `ds3231_configure_loop()` function in the microcontroller code in an eternal loop.
    ```
    while (1)
    {
        ds3231_configure_loop();
    }
    ```
4. Upload the firmware to the device.

#### Host side:
1. Build `./configure_clock/PC_utils/CMakeLists.txt`
2. Run the `update_DS3231_time` program.