#ifndef _GPIO_H_
#define _GPIO_H_

#include "main.h"

typedef struct {
    GPIO_TypeDef *port; /* Data GPIO port */
    uint16_t pin; /* Data GPIO pin */
} GPIO_t;

#endif // _GPIO_H_