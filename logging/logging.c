/*
 * logging.c
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#include <stdio.h>
#include "logging.h"

static const char *log_level_str[] = {
		"",
		"FATAL",
		"ERROR",
		"WARN",
		"INFO",
		"DEBUG",
		"TRACE"
};

static LOG_LEVEL allowed_log_level = LOG_LEVEL_INFO;

void set_logging_level(LOG_LEVEL level)
{
	if (level < LOG_LEVEL_NONE || level > LOG_LEVEL_LAST)
	{
		return;
	}

	allowed_log_level = level;
}

void _log(LOG_LEVEL level, const char *msg)
{
	if (level > allowed_log_level || level == LOG_LEVEL_NONE)
	{
		return;
	}

	if (level < LOG_LEVEL_NONE || level > LOG_LEVEL_LAST)
	{
		return;
	}

	printf("%s: %s\r\n", log_level_str[level], msg);
}
