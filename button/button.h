#ifndef BUTTON_H
#define BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#if defined(STM32F0xx) || defined(STM32F030x8) || defined(STM32F030x6) || defined(STM32F030xB)
    #include "stm32f0xx_hal.h"
#elif defined(STM32F4xx) || defined(STM32F401xE) || defined(STM32F446xx)
    #include "stm32f4xx_hal.h"
#else
    #error "Unsupported STM32 family. Add proper HAL include mapping in button.h."
#endif

#include "gpio.h"

/* Button stable state */
typedef enum
{
    BUTTON_STATE_RELEASED = 0,
    BUTTON_STATE_PRESSED  = 1
} Button_state_t;

/* Button events emitted by state machine */
typedef enum
{
    BUTTON_EVENT_PRESSED = 0,
    BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_CLICK,
    BUTTON_EVENT_LONG_PRESS
} Button_event_t;

typedef struct Button_t Button_t;

/* User callback for button events */
typedef void (*button_event_callback_t)(Button_t *btn, Button_event_t event, void *user_ctx);

struct Button_t
{
    GPIO_t gpio;

    /* Which GPIO state means "pressed" */
    GPIO_PinState active_state;

    /* Debounce time after IRQ edge, ms */
    uint32_t debounce_ms;

    /* If > 0, long press event after this time, ms */
    uint32_t long_press_ms;

    /* If > 0, click event will be emitted on release if press time <= click_max_ms
       If 0, click event is disabled */
    uint32_t click_max_ms;

    /* Current stable state */
    Button_state_t state;

    /* Internal timing/state */
    uint8_t pending;
    uint32_t pending_tick;
    uint32_t press_tick;
    uint8_t long_sent;

    /* Optional callback */
    button_event_callback_t cb;
    void *user_ctx;
};

/* Initialize button instance (default timings can be overridden later) */
void button_init(Button_t *btn, GPIO_t gpio, GPIO_PinState active_state);

/* Configure timings (can be called any time) */
void button_set_debounce(Button_t *btn, uint32_t debounce_ms);
void button_set_long_press(Button_t *btn, uint32_t long_press_ms);
void button_set_click(Button_t *btn, uint32_t click_max_ms);

/* Attach callback (optional) */
void button_set_callback(Button_t *btn, button_event_callback_t cb, void *user_ctx);

/* Call this from EXTI context for this specific button (fast, ISR-safe) */
void button_irq_handler(Button_t *btn, uint32_t tick_now);

/* Call periodically from main loop or 1ms tick */
void button_process(Button_t *btn, uint32_t tick_now);

/* Helpers */
static inline Button_state_t button_get_state(const Button_t *btn)
{
    return (btn != NULL) ? btn->state : BUTTON_STATE_RELEASED;
}

static inline uint8_t button_is_pressed(const Button_t *btn)
{
    return (btn != NULL && btn->state == BUTTON_STATE_PRESSED) ? 1U : 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */
