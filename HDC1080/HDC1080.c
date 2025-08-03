/*
 * HDC1080.c
 *
 *  Created on: Aug 3, 2025
 *      Author: vojt
 */

#include "HDC1080.h"

#define HDC1080_ADDRESS (0x40 << 1)

#define HDC1080_REG_TEMPERATURE (0x00)
#define HDC1080_REG_CONFIGURATION (0x02)

#define HDC1080_ACQUISION_MODE_BIT (4)
#define HDC1080_TEMP_RESOLUTION_BIT (2)
#define HDC1080_HUM_RESOLUTION_BIT (0)

#define HDC1080_DEFAULT_DELAY (15)

static uint8_t HDC1080_buffer[4];
static uint8_t HDC1080_trigger_measure[1] = { HDC1080_REG_TEMPERATURE };

static I2C_HandleTypeDef *HDC1080_i2c_handle = NULL;
static uint32_t HDC1080_delay_ms = HDC1080_DEFAULT_DELAY;

static uint8_t HDC1080_delay_ms_temp[] = { 7, 4 };
static uint8_t HDC1080_delay_ms_hum[]  = { 7, 4, 3 };

int HDC1080_init(I2C_HandleTypeDef *handle, HDC1080_TEMP_RESOLUTION t_res, HDC1080_HUM_RESOLUTION h_res)
{
	HDC1080_i2c_handle = handle;

	if (t_res < 0 || t_res > HDC1080_TEMP_RESOLUTION_LAST)
		return -1;

	if (h_res < 0 || h_res > HDC1080_HUM_RESOLUTION_LAST)
		return -2;

	HDC1080_delay_ms = HDC1080_delay_ms_temp[t_res] + HDC1080_delay_ms_hum[h_res];

	uint8_t config[3] = { HDC1080_REG_CONFIGURATION, 0x00, 0x00};
	config[1] = (1 << HDC1080_ACQUISION_MODE_BIT) |
			(t_res << HDC1080_TEMP_RESOLUTION_BIT) |
			(h_res << HDC1080_HUM_RESOLUTION_BIT);

	HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(HDC1080_i2c_handle, HDC1080_ADDRESS, config, 3, HAL_MAX_DELAY);
	if (status != HAL_OK)
		return -3;

	return 0;
}

int HDC1080_get_data(float *temperature, float *humidity)
{
	if (HDC1080_i2c_handle == NULL)
		return -1;

	if (temperature == NULL || humidity == NULL)
		return -2;

	HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(HDC1080_i2c_handle, HDC1080_ADDRESS, HDC1080_trigger_measure, 1, HAL_MAX_DELAY);
	if (status != HAL_OK)
		return -3;

	HAL_Delay(HDC1080_delay_ms);
	status = HAL_I2C_Master_Receive(HDC1080_i2c_handle, HDC1080_ADDRESS, HDC1080_buffer, 4, HAL_MAX_DELAY);
	if (status != HAL_OK)
		return -4;

	// Calculate values
	uint16_t value = (HDC1080_buffer[0] << 8) | HDC1080_buffer[1];
	*temperature = ((float)value / UINT16_MAX) * 165 - 40;

	value = (HDC1080_buffer[2] << 8) | HDC1080_buffer[3];
	*humidity = ((float)value / UINT16_MAX) * 100;

	return 0;
}
