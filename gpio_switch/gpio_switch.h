#ifndef GPIO_SWITCH_H
#define GPIO_SWITCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gpio.h"

/* Selects which logic level means "ON" for the controlled load.
   - ACTIVE_HIGH: GPIO high -> load ON
   - ACTIVE_LOW:  GPIO low  -> load ON (inverted logic, common with some drivers/relays) */
typedef enum {
    GPIO_SWITCH_ACTIVE_HIGH = 0,
    GPIO_SWITCH_ACTIVE_LOW  = 1
} GPIO_switch_active_level_t;

/* Logical output state (independent from physical pin level) */
typedef enum {
    GPIO_SWITCH_STATE_OFF = 0,
    GPIO_SWITCH_STATE_ON  = 1
} GPIO_switch_state_t;

/* Instance object */
typedef struct {
    GPIO_t                       pin;
    GPIO_switch_active_level_t   active_level;
    GPIO_switch_state_t          state;
} GPIO_switch_t;

/* Initializes the switch instance and applies initial state to the pin.
   The GPIO pin must already be configured as output by the user project. */
void gpio_switch_init(GPIO_switch_t *sw,
                      GPIO_t pin,
                      GPIO_switch_active_level_t active_level,
                      GPIO_switch_state_t initial_state);

/* Sets logical state (ON/OFF) */
void gpio_switch_set(GPIO_switch_t *sw, GPIO_switch_state_t state);

/* Convenience functions */
void gpio_switch_on(GPIO_switch_t *sw);
void gpio_switch_off(GPIO_switch_t *sw);
void gpio_switch_toggle(GPIO_switch_t *sw);

/* Returns last logical state stored in the instance */
GPIO_switch_state_t gpio_switch_get(const GPIO_switch_t *sw);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_SWITCH_H */
