#include "button_manager.h"

void button_manager_init(Button_manager_t *mgr, Button_t **buttons, size_t count)
{
    if (mgr == NULL)
    {
        return;
    }

    mgr->buttons = buttons;
    mgr->count = count;
}

void button_manager_irq_handler(Button_manager_t *mgr, uint16_t gpio_pin, uint32_t tick_now)
{
    size_t i;

    if (mgr == NULL || mgr->buttons == NULL)
    {
        return;
    }

    /* Match by pin mask (HAL callback gives only GPIO_Pin) */
    for (i = 0U; i < mgr->count; i++)
    {
        Button_t *btn;

        btn = mgr->buttons[i];
        if (btn == NULL)
        {
            continue;
        }

        if (btn->gpio.pin == gpio_pin)
        {
            button_irq_handler(btn, tick_now);
        }
    }
}

void button_manager_process(Button_manager_t *mgr, uint32_t tick_now)
{
    size_t i;

    if (mgr == NULL || mgr->buttons == NULL)
    {
        return;
    }

    for (i = 0U; i < mgr->count; i++)
    {
        if (mgr->buttons[i] != NULL)
        {
            button_process(mgr->buttons[i], tick_now);
        }
    }
}
