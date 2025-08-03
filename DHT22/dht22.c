/*
 * dht22.c
 *
 *  Created on: Aug 3, 2025
 *      Author: vojt
 */

#include "dht22.h"

static TIM_HandleTypeDef *DHT22_timer_handle = NULL;
static GPIO_TypeDef *DHT22_port = NULL;
static uint16_t DHT22_pin = -1;

void DHT22_init(TIM_HandleTypeDef *timer_handle, GPIO_TypeDef *data_port, uint16_t data_pin)
{
	DHT22_timer_handle = timer_handle;

	DHT22_port = data_port;
	DHT22_pin = data_pin;

	HAL_TIM_Base_Start(DHT22_timer_handle);
}

static void DHT22_delay(uint16_t delay_us)
{
	__HAL_TIM_SET_COUNTER(DHT22_timer_handle, 0);
	while ((__HAL_TIM_GET_COUNTER(DHT22_timer_handle)) < delay_us);
}

static void DHT22_set_pin_input()
{
	  GPIO_InitTypeDef GPIO_InitStruct = {0};
	  GPIO_InitStruct.Pin = DHT22_pin;
	  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  HAL_GPIO_Init(DHT22_port, &GPIO_InitStruct);
}


static void DHT22_set_pin_output()
{
	  GPIO_InitTypeDef GPIO_InitStruct = {0};
	  GPIO_InitStruct.Pin = DHT22_pin;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(DHT22_port, &GPIO_InitStruct);
}

static void DHT22_start()
{
	DHT22_set_pin_output(DHT22_port, DHT22_pin);               // set the pin as output
	HAL_GPIO_WritePin(DHT22_port, DHT22_pin, GPIO_PIN_RESET); // pull the pin low
	DHT22_delay(1200);                                         // wait for > 1ms

	HAL_GPIO_WritePin(DHT22_port, DHT22_pin, GPIO_PIN_SET);   // pull the pin high
	DHT22_delay (20);                                          // wait for 30us
//	DHT22_set_pin_input(DHT22_port, DHT22_pin);                // set as input
}

static uint8_t DHT22_check_response()
{
	DHT22_set_pin_input(DHT22_port, DHT22_pin);         // set as input
	uint8_t ready = 0;
	DHT22_delay(40);                                    // wait for 40us

	GPIO_PinState dht22_state = HAL_GPIO_ReadPin(DHT22_port, DHT22_pin);
	if (dht22_state == GPIO_PIN_RESET)                  // if the pin is low
	{
		DHT22_delay(80);                                // wait for 80us

		dht22_state = HAL_GPIO_ReadPin(DHT22_port, DHT22_pin);

		ready = (dht22_state == GPIO_PIN_SET);          // if the pin is high, response is ok
	}

	while ((HAL_GPIO_ReadPin(DHT22_port, DHT22_pin)) == GPIO_PIN_SET); // wait for the pin to go low
	return ready;
}

static uint8_t DHT22_read_byte()
{
	uint8_t result;
	uint8_t data_bit;
	for (data_bit = 0; data_bit < 8; data_bit++)
	{
		while ((HAL_GPIO_ReadPin(DHT22_port, DHT22_pin)) == GPIO_PIN_RESET); // wait for the pin to go high
		DHT22_delay(40);                                                     // wait for 40 us

		if ((HAL_GPIO_ReadPin(DHT22_port, DHT22_pin)) == GPIO_PIN_RESET)     // if the pin is low
		{
			result &= ~(1 << (7 - data_bit)); // write 0
		}
		else
		{
			result |= (1 << (7 - data_bit));  // write 1
		}

		while ((HAL_GPIO_ReadPin(DHT22_port, DHT22_pin)) == GPIO_PIN_SET);    // wait for the pin to go low
	}

	return result;
}

int DHT22_get_data(float *temperature, float *humidity)
{
	if (DHT22_timer_handle == NULL)
		return -1;

	if (DHT22_port == NULL || DHT22_pin > 16)
		return -2;

	DHT22_start();
	uint8_t ready = DHT22_check_response();
	if (!ready)
		return -3;

	uint8_t raw_humidity_high = DHT22_read_byte();
	uint8_t raw_humidity_low  = DHT22_read_byte();

	uint8_t raw_temperature_high = DHT22_read_byte();
	uint8_t raw_temperature_low  = DHT22_read_byte();

	DHT22_read_byte(); // ignore check sum

	uint16_t raw_humidity = (raw_humidity_high << 8) | raw_humidity_low;
	uint16_t raw_temperature = (raw_temperature_high << 8) | raw_temperature_low;

	*humidity = (float)(raw_humidity / 10.0);
	*temperature = (float)(raw_temperature / 10.0);

	return 0;
}

