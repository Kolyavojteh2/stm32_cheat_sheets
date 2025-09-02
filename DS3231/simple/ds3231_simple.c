/*
 * ds3231.c
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#include "ds3231.h"
#include <time.h>

static I2C_HandleTypeDef *ds3231_i2c_handle = NULL;

#define DS3231_REG_DATETIME    (0x00)
#define DS3231_REG_TEMPERATURE (0x11)

void ds3231_set_i2c_handle(I2C_HandleTypeDef *handle)
{
	ds3231_i2c_handle = handle;
}

I2C_HandleTypeDef *ds3231_get_i2c_handle()
{
	return ds3231_i2c_handle;
}

static uint8_t ds3231_dec_to_bcd(int val)
{
	return (uint8_t)((val / 10 * 16) + (val % 10));
}

static int ds3231_bcd_to_dec(uint8_t val)
{
	return (int)((val / 16 * 10) + (val % 16));
}

int ds3231_set_time(datetime_t *datetime)
{
	if (datetime == NULL)
		return -1;

	uint8_t set_time[7];
	set_time[0] = ds3231_dec_to_bcd(datetime->seconds);
	set_time[1] = ds3231_dec_to_bcd(datetime->minutes);
	set_time[2] = ds3231_dec_to_bcd(datetime->hour);
	set_time[3] = ds3231_dec_to_bcd(datetime->dayofweek);
	set_time[4] = ds3231_dec_to_bcd(datetime->dayofmonth);
	set_time[5] = ds3231_dec_to_bcd(datetime->month) + (1 << 8);
	set_time[6] = ds3231_dec_to_bcd(datetime->year % 100);

	HAL_I2C_Mem_Write(ds3231_i2c_handle, DS3231_ADDRESS, DS3231_REG_DATETIME, 1, set_time, 7, 1000);

	return 0;
}

int ds3231_get_time(datetime_t *datetime)
{
	if (datetime == NULL)
		return -1;

	uint8_t get_time[7];
	HAL_I2C_Mem_Read(ds3231_i2c_handle, DS3231_ADDRESS, DS3231_REG_DATETIME, 1, get_time, 7, 1000);
	datetime->seconds    = ds3231_bcd_to_dec(get_time[0]);
	datetime->minutes    = ds3231_bcd_to_dec(get_time[1]);
	datetime->hour       = ds3231_bcd_to_dec(get_time[2]);
	datetime->dayofweek  = ds3231_bcd_to_dec(get_time[3]);
	datetime->dayofmonth = ds3231_bcd_to_dec(get_time[4]);
	datetime->month      = ds3231_bcd_to_dec(get_time[5]);
	datetime->year       = 2000 + ds3231_bcd_to_dec(get_time[6]);

	return 0;
}

float ds3231_get_temperature(void)
{
	uint8_t temp[2];
	HAL_StatusTypeDef ret = HAL_I2C_Mem_Read(ds3231_i2c_handle, DS3231_ADDRESS, DS3231_REG_TEMPERATURE, 1, temp, 2, 1000);
	return ((temp[0]) + (temp[1] >> 6) / 4.0);
}
