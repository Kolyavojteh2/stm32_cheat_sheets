#include "gpio_switch.h"

/* Select the proper HAL header depending on the MCU family */
#if defined(STM32F0xx)
#include "stm32f0xx_hal.h"
#elif defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#else
#error "Unsupported STM32 family. Define STM32F0xx or STM32F4xx (HAL)."
#endif

/* Converts logical state (ON/OFF) to physical pin level (SET/RESET) */
static GPIO_PinState gpio_switch_state_to_pin_state(const GPIO_switch_t *sw, GPIO_switch_state_t state)
{
    if (sw == NULL) {
        return GPIO_PIN_RESET;
    }

    if (sw->active_level == GPIO_SWITCH_ACTIVE_HIGH) {
        return (state == GPIO_SWITCH_STATE_ON) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    } else {
        return (state == GPIO_SWITCH_STATE_ON) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
}

/* Writes to GPIO and updates stored state */
static void gpio_switch_apply(GPIO_switch_t *sw, GPIO_switch_state_t state)
{
    GPIO_PinState pin_state;

    if (sw == NULL) {
        return;
    }

    pin_state = gpio_switch_state_to_pin_state(sw, state);

    HAL_GPIO_WritePin(sw->pin.port, sw->pin.pin, pin_state);
    sw->state = state;
}

void gpio_switch_init(GPIO_switch_t *sw,
                      GPIO_t pin,
                      GPIO_switch_active_level_t active_level,
                      GPIO_switch_state_t initial_state)
{
    if (sw == NULL) {
        return;
    }

    sw->pin = pin;
    sw->active_level = active_level;
    sw->state = GPIO_SWITCH_STATE_OFF;

    gpio_switch_apply(sw, initial_state);
}

void gpio_switch_set(GPIO_switch_t *sw, GPIO_switch_state_t state)
{
    gpio_switch_apply(sw, state);
}

void gpio_switch_on(GPIO_switch_t *sw)
{
    gpio_switch_apply(sw, GPIO_SWITCH_STATE_ON);
}

void gpio_switch_off(GPIO_switch_t *sw)
{
    gpio_switch_apply(sw, GPIO_SWITCH_STATE_OFF);
}

void gpio_switch_toggle(GPIO_switch_t *sw)
{
    if (sw == NULL) {
        return;
    }

    if (sw->state == GPIO_SWITCH_STATE_ON) {
        gpio_switch_apply(sw, GPIO_SWITCH_STATE_OFF);
    } else {
        gpio_switch_apply(sw, GPIO_SWITCH_STATE_ON);
    }
}

GPIO_switch_state_t gpio_switch_get(const GPIO_switch_t *sw)
{
    if (sw == NULL) {
        return GPIO_SWITCH_STATE_OFF;
    }

    return sw->state;
}
