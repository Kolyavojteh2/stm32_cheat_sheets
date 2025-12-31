## How to integrate into the project

### Create instances:
```
#include "button.h"
#include "button_manager.h"

static Button_t btn_ok;
static Button_t btn_back;
static Button_t btn_up;

static Button_t *btn_list[] =
{
    &btn_ok,
    &btn_back,
    &btn_up
};

static Button_manager_t btn_mgr;

static void on_button(Button_t *btn, Button_event_t event, void *user_ctx)
{
    (void)user_ctx;

    /* TODO: add handle logic */
    (void)btn;
    (void)event;
}

void buttons_init(void)
{
    GPIO_t ok_gpio   = { .port = GPIOA, .pin = GPIO_PIN_0 };
    GPIO_t back_gpio = { .port = GPIOA, .pin = GPIO_PIN_1 };
    GPIO_t up_gpio   = { .port = GPIOB, .pin = GPIO_PIN_2 };

    button_init(&btn_ok, ok_gpio, GPIO_PIN_RESET);
    button_init(&btn_back, back_gpio, GPIO_PIN_RESET);
    button_init(&btn_up, up_gpio, GPIO_PIN_RESET);

    button_set_debounce(&btn_ok, 30U);
    button_set_long_press(&btn_ok, 800U);
    button_set_click(&btn_ok, 400U);

    button_set_callback(&btn_ok, on_button, NULL);
    button_set_callback(&btn_back, on_button, NULL);
    button_set_callback(&btn_up, on_button, NULL);

    button_manager_init(&btn_mgr, btn_list, sizeof(btn_list) / sizeof(btn_list[0]));
}
```

### Insert EXTI processing into the callback
```
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    button_manager_irq_handler(&btn_mgr, GPIO_Pin, HAL_GetTick());
}
```

### Insert loop cycle function:
```
void buttons_process(void)
{
    button_manager_process(&btn_mgr, HAL_GetTick());
}
```
