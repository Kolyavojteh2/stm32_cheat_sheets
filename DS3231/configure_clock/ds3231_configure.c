/*
 * ds3231_configure.c
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#include "ds3231_configure.h"
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef *ds3231_uart_handle = NULL;

static char ds3231_uart_rx_buffer[128];
static char ds3231_uart_tx_buffer[128];

void ds3231_set_uart_handle(UART_HandleTypeDef *handle)
{
	ds3231_uart_handle = handle;
}

static void ds3231_update_time(char *cmd)
{
	datetime_t current_time;
    int ss, mm, hh, dow, dd, mo, yyyy;
    if (sscanf(cmd + 4, "%2d:%2d:%2d:%1d:%2d:%2d:%4d", &ss, &mm, &hh, &dow, &dd, &mo, &yyyy) == 7)
    {
        current_time.seconds     = ss;
        current_time.minutes     = mm;
        current_time.hour        = hh;
        current_time.dayofweek   = dow;
        current_time.dayofmonth  = dd;
        current_time.month       = mo;
        current_time.year        = yyyy;

        ds3231_set_time(&current_time);

        volatile datetime_t updated;
        ds3231_get_time(&updated);

        HAL_UART_Transmit(ds3231_uart_handle, (uint8_t*)"OK\r\n", 4, HAL_MAX_DELAY);
    }
}

static void ds3231_sent_time_to_host()
{
	volatile datetime_t current_time;
	ds3231_get_time(&current_time);

    snprintf(ds3231_uart_tx_buffer, sizeof(ds3231_uart_tx_buffer), "TIME:%02d:%02d:%02d:%d:%02d:%02d:%04d\r\n",
        current_time.seconds, current_time.minutes, current_time.hour,
        current_time.dayofweek, current_time.dayofmonth, current_time.month, current_time.year);
    HAL_UART_Transmit(ds3231_uart_handle, (uint8_t*)ds3231_uart_tx_buffer, strlen(ds3231_uart_tx_buffer), HAL_MAX_DELAY);
}


static void ds3231_process_uart_command(char *cmd)
{
    if (strncmp(cmd, "SET:", 4) == 0)
    {
    	ds3231_update_time(cmd);
    }
    else if (strncmp(cmd, "GET", 3) == 0)
    {
    	ds3231_sent_time_to_host();
    }
}

int ds3231_configure_loop()
{
    static uint16_t idx = 0;
    uint8_t ch;
    if (HAL_UART_Receive(ds3231_uart_handle, &ch, 1, 10) == HAL_OK)
    {
        if (ch == '\n' || idx >= sizeof(ds3231_uart_rx_buffer) - 1)
        {
        	ds3231_uart_rx_buffer[idx] = '\0';
            ds3231_process_uart_command(ds3231_uart_rx_buffer);
            idx = 0;
            return 0;
        }
        else
        {
        	ds3231_uart_rx_buffer[idx++] = ch;
        }
    }

    return 1;
}
