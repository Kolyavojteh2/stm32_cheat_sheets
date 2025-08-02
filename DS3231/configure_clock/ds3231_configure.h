/*
 * ds3231_configure.h
 *
 *  Created on: Aug 2, 2025
 *      Author: vojt
 */

#ifndef INC_DS3231_CONFIGURE_H_
#define INC_DS3231_CONFIGURE_H_

#include "ds3231.h"

void ds3231_set_uart_handle(UART_HandleTypeDef *handle);
int  ds3231_configure_loop();

#endif /* INC_DS3231_CONFIGURE_H_ */
