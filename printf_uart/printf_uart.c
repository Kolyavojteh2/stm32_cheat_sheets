/*
 * printf_uart.c
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */


#include <stdio.h>
#include "printf_uart.h"

static UART_HandleTypeDef *stdout_uart_handle = NULL;

void set_stdout_uart_handle(UART_HandleTypeDef *handle)
{
	stdout_uart_handle = handle;
}

int __io_putchar(int ch)
{
	if (stdout_uart_handle == NULL)
		return -1;

	HAL_UART_Transmit(stdout_uart_handle, (uint8_t *)&ch, 1, 0xFFFF);

	return ch;
}
