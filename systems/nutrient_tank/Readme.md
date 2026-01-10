## How to connect PumpUnit_t to GPIO_switch_t (minimal)
You need to give PumpUnit_SwitchOps_t (2 functions). For example:
```
static uint8_t sw_on(GPIO_switch_t *sw)
{
    return gpio_switch_turn_on(sw);
}

static uint8_t sw_off(GPIO_switch_t *sw)
{
    return gpio_switch_turn_off(sw);
}

/* ... */

PumpUnit_SwitchOps_t ops = { .on = sw_on, .off = sw_off };
pump_unit_set_switch_ops(&pump, &ops);
```
