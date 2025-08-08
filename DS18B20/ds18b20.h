/*
 * ds18b20.h
 *
 *  Created on: Aug 8, 2025
 *      Author: vojt
 */

#ifndef INC_DS18B20_H_
#define INC_DS18B20_H_

#include "main.h"

typedef enum {
	DS18B20_RESOLUTION_9_BIT = 0,
	DS18B20_RESOLUTION_10_BIT,
	DS18B20_RESOLUTION_11_BIT,
	DS18B20_RESOLUTION_12_BIT,
} DS18B20_RESOLUTION;

typedef struct {
	GPIO_TypeDef *port;
	uint16_t pin;
	TIM_HandleTypeDef *timer;
	DS18B20_RESOLUTION resolution;
} DS18B20_t;

int DS18B20_init(DS18B20_t *instance, TIM_HandleTypeDef *timer_handle, GPIO_TypeDef *data_port, uint16_t data_pin);
int DS18B20_get_data(DS18B20_t *instance, float *temperature);
int DS18B20_set_resolution(DS18B20_t *instance, DS18B20_RESOLUTION resolution);

#endif /* INC_DS18B20_H_ */
