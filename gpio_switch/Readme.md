## How to use
```
GPIO_switch_t pump;

GPIO_t pump_pin = {
    .port = GPIOA,
    .pin  = GPIO_PIN_5
};

/* If transistor driver turns pump ON when GPIO is high -> ACTIVE_HIGH */
gpio_switch_init(&pump, pump_pin, GPIO_SWITCH_ACTIVE_HIGH, GPIO_SWITCH_STATE_OFF);

gpio_switch_on(&pump);
gpio_switch_off(&pump);
gpio_switch_toggle(&pump);

```