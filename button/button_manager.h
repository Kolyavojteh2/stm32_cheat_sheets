#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "button.h"

/* Simple manager over multiple button instances */
typedef struct
{
    Button_t **buttons;
    size_t count;
} Button_manager_t;

/* Initialize manager with user-provided array of pointers */
void button_manager_init(Button_manager_t *mgr, Button_t **buttons, size_t count);

/* Call from HAL_GPIO_EXTI_Callback(GPIO_Pin) or from EXTI IRQ handlers
   Note: HAL callback provides only pin mask, not port. Ensure each EXTI line is unique in your design. */
void button_manager_irq_handler(Button_manager_t *mgr, uint16_t gpio_pin, uint32_t tick_now);

/* Call periodically (main loop / 1ms tick) */
void button_manager_process(Button_manager_t *mgr, uint32_t tick_now);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_MANAGER_H */
