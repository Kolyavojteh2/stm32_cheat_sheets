/*
 * ds3231_time_bridge.h
 *
 * DS3231 <-> UNIX epoch helpers.
 *
 * What this module does:
 * - Converts DS3231 calendar time (struct tm) to/from UNIX epoch (UTC).
 * - Provides an optional integration point for libc time()/gettimeofday()
 *   via newlib syscalls (_gettimeofday/_settimeofday).
 *
 * By default, this file does NOT define syscalls to avoid link conflicts with
 * CubeIDE-generated syscalls.c. To enable syscalls from this module, define:
 *   DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS=1
 * and ensure your project does not provide a competing strong definition of
 * _gettimeofday/_settimeofday.
 */

#ifndef DS3231_TIME_BRIDGE_H_
#define DS3231_TIME_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ds3231.h"
#include <stdint.h>
#include <time.h>

#ifndef DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS
#define DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS     0U
#endif

/* Attach an initialized DS3231 instance to the time bridge. */
void ds3231_time_bridge_attach(DS3231_t *rtc);

/* Read DS3231 time and return UNIX epoch seconds (UTC). */
int ds3231_time_get_epoch(time_t *out_epoch);

/* Convert UNIX epoch seconds (UTC) to DS3231 calendar time and write it. */
int ds3231_time_set_epoch(time_t epoch);

#ifdef __cplusplus
}
#endif

#endif /* DS3231_TIME_BRIDGE_H_ */
