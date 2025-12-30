/*
 * ds3231_time_bridge.h
 *
 * 2025-09-02
 *
 * Provide simple overrides for time()/gettimeofday()/settimeofday so that
 * standard C/POSIX time functions read/write the DS3231 via your instance.
 * Assumes the RTC keeps UTC.
 */
#ifndef DS3231_TIME_BRIDGE_H_
#define DS3231_TIME_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ds3231.h"
#include <time.h>
#include <stdint.h>

/* Attach an initialized DS3231 instance to the time bridge. 
 * Call this once, after ds3231_init(). */
void ds3231_time_bridge_attach(DS3231 *rtc);

/* Manually get/set UNIX epoch via DS3231 (UTC). */
int ds3231_time_get_epoch(time_t *out_epoch);
int ds3231_time_set_epoch(time_t epoch);

/* Optional helpers to sync between DS3231 and system (if you also keep a software clock). */
int ds3231_sync_system_from_rtc(void); /* read DS3231 -> settimeofday() */
int ds3231_sync_rtc_from_system(void); /* read time() -> write DS3231    */

#ifdef __cplusplus
}
#endif

#endif /* DS3231_TIME_BRIDGE_H_ */
