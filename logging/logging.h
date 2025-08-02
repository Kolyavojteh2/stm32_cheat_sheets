/*
 * logging.h
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#ifndef INC_LOGGING_H_
#define INC_LOGGING_H_

typedef enum {
	LOG_LEVEL_NONE = 0,
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_TRACE,
	LOG_LEVEL_LAST = LOG_LEVEL_TRACE,
} LOG_LEVEL;

void set_logging_level(LOG_LEVEL level);
void _log(LOG_LEVEL level, const char *msg);

#define LOG_FATAL(msg) _log(LOG_LEVEL_FATAL, msg)
#define LOG_ERROR(msg) _log(LOG_LEVEL_ERROR, msg)
#define LOG_WARN(msg)  _log(LOG_LEVEL_WARN,  msg)
#define LOG_INFO(msg)  _log(LOG_LEVEL_INFO,  msg)
#define LOG_DEBUG(msg) _log(LOG_LEVEL_DEBUG, msg)
#define LOG_TRACE(msg) _log(LOG_LEVEL_TRACE, msg)

#endif /* INC_LOGGING_H_ */
