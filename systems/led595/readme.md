# LED control module on top of SN74HC595 chain.
Works for both 1 chip and a cascade of N pieces.

sn74hc595_chain.h / sn74hc595_chain.c — low-level driver for N cascaded 595 (bit-bang, MSB-first).
led595.h / led595.c — LED logic control (active-high/active-low, auto refresh).

# How to use

```
#include "sn74hc595_chain.h"
#include "led595.h"

static uint8_t sr_buf[2];                 /* 2 chips => 16 outputs */
static SN74HC595_Chain_t sr;
static LED595_t leds;

void app_init(void)
{
    sn74hc595_chain_init_pins(&sr,
                              DS_GPIO_Port, DS_Pin,
                              CLK_GPIO_Port, CLK_Pin,
                              LATCH_GPIO_Port, LATCH_Pin,
                              sr_buf,
                              (uint16_t)sizeof(sr_buf));

    /* active_low = 1 якщо LED підключені до VCC і вихід “тягне вниз” */
    led595_init(&leds, &sr, 1U, 1U);

    led595_set(&leds, 0);     /* byte0 bit0 */
    led595_set(&leds, 9);     /* byte1 bit1 */
}
```
