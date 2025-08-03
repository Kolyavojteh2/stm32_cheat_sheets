/*
 * dht22.h
 *
 *  Created on: Aug 3, 2025
 *      Author: vojt
 */

#ifndef INC_DHT22_H_
#define INC_DHT22_H_

#include "main.h"

void DHT22_init(TIM_HandleTypeDef *timer_handle, GPIO_TypeDef *data_port, uint16_t data_pin);
int  DHT22_get_data(float *temperature, float *humidity);

#endif /* INC_DHT22_H_ */
