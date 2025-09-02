/*
 * ds3231.h
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#ifndef INC_DS3231_H_
#define INC_DS3231_H_

#include "main.h"

#define DS3231_ADDRESS (0xD0)

typedef struct {
	unsigned int seconds;
	unsigned int minutes;
	unsigned int hour;
	unsigned int dayofweek;
	unsigned int dayofmonth;
	unsigned int month;
	unsigned int year;
} datetime_t;

void ds3231_set_i2c_handle(I2C_HandleTypeDef *handle);
I2C_HandleTypeDef *ds3231_get_i2c_handle();

int  ds3231_set_time(datetime_t *datetime);
int  ds3231_get_time(datetime_t *datetime);

float ds3231_get_temperature(void);

#endif /* INC_DS3231_H_ */
