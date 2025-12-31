#include "button.h"

static GPIO_PinState button_read_raw(const Button_t *btn)
{
    if (btn == NULL)
    {
        return GPIO_PIN_RESET;
    }

    return HAL_GPIO_ReadPin(btn->gpio.port, btn->gpio.pin);
}

static uint8_t button_raw_is_active(const Button_t *btn)
{
    GPIO_PinState raw;

    raw = button_read_raw(btn);
    return (raw == btn->active_state) ? 1U : 0U;
}

static void button_emit(Button_t *btn, Button_event_t event)
{
    if (btn == NULL)
    {
        return;
    }

    if (btn->cb != NULL)
    {
        btn->cb(btn, event, btn->user_ctx);
    }
}

void button_init(Button_t *btn, GPIO_t gpio, GPIO_PinState active_state)
{
    if (btn == NULL)
    {
        return;
    }

    btn->gpio = gpio;
    btn->active_state = active_state;

    /* Reasonable defaults */
    btn->debounce_ms = 30U;
    btn->long_press_ms = 800U;
    btn->click_max_ms = 500U;

    btn->state = BUTTON_STATE_RELEASED;

    btn->pending = 0U;
    btn->pending_tick = 0U;
    btn->press_tick = 0U;
    btn->long_sent = 0U;

    btn->cb = NULL;
    btn->user_ctx = NULL;

    /* Initialize stable state from current pin level */
    if (button_raw_is_active(btn) != 0U)
    {
        btn->state = BUTTON_STATE_PRESSED;
        btn->press_tick = HAL_GetTick();
    }
}

void button_set_debounce(Button_t *btn, uint32_t debounce_ms)
{
    if (btn == NULL)
    {
        return;
    }

    btn->debounce_ms = debounce_ms;
}

void button_set_long_press(Button_t *btn, uint32_t long_press_ms)
{
    if (btn == NULL)
    {
        return;
    }

    btn->long_press_ms = long_press_ms;
}

void button_set_click(Button_t *btn, uint32_t click_max_ms)
{
    if (btn == NULL)
    {
        return;
    }

    btn->click_max_ms = click_max_ms;
}

void button_set_callback(Button_t *btn, button_event_callback_t cb, void *user_ctx)
{
    if (btn == NULL)
    {
        return;
    }

    btn->cb = cb;
    btn->user_ctx = user_ctx;
}

void button_irq_handler(Button_t *btn, uint32_t tick_now)
{
    if (btn == NULL)
    {
        return;
    }

    /* ISR: mark pending and timestamp. Do not read GPIO here. */
    btn->pending = 1U;
    btn->pending_tick = tick_now;
}

void button_process(Button_t *btn, uint32_t tick_now)
{
    uint8_t active_now;
    Button_state_t new_state;

    if (btn == NULL)
    {
        return;
    }

    /* Long press detection (stable pressed state) */
    if (btn->state == BUTTON_STATE_PRESSED)
    {
        if (btn->long_sent == 0U && btn->long_press_ms > 0U)
        {
            if ((tick_now - btn->press_tick) >= btn->long_press_ms)
            {
                btn->long_sent = 1U;
                button_emit(btn, BUTTON_EVENT_LONG_PRESS);
            }
        }
    }

    /* No pending edge -> nothing to debounce */
    if (btn->pending == 0U)
    {
        return;
    }

    /* Wait debounce time from last IRQ edge */
    if ((tick_now - btn->pending_tick) < btn->debounce_ms)
    {
        return;
    }

    /* Debounce window passed: sample GPIO and update stable state if changed */
    active_now = button_raw_is_active(btn);
    new_state = (active_now != 0U) ? BUTTON_STATE_PRESSED : BUTTON_STATE_RELEASED;

    btn->pending = 0U;

    if (new_state == btn->state)
    {
        return;
    }

    btn->state = new_state;

    if (new_state == BUTTON_STATE_PRESSED)
    {
        btn->press_tick = tick_now;
        btn->long_sent = 0U;
        button_emit(btn, BUTTON_EVENT_PRESSED);
    }
    else
    {
        uint32_t press_time;

        button_emit(btn, BUTTON_EVENT_RELEASED);

        press_time = tick_now - btn->press_tick;

        /* Click event if enabled, and long press wasn't already emitted */
        if (btn->click_max_ms > 0U && btn->long_sent == 0U)
        {
            if (press_time <= btn->click_max_ms)
            {
                button_emit(btn, BUTTON_EVENT_CLICK);
            }
        }
    }
}
