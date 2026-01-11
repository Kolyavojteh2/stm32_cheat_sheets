## How to use it:
```
#include "at24c04.h"

extern I2C_HandleTypeDef hi2c1;

static AT24C04_t eeprom;

void eeprom_demo(void)
{
    /* For AT24C04C device address: 1010 A2 A1 A8 R/W.
       Set A2/A1 according to your wiring (often both 0). */
    at24c04_init(&eeprom, &hi2c1, 0U, 0U);

    uint8_t tx[20] = {0};
    uint8_t rx[20] = {0};

    for (uint8_t i = 0; i < sizeof(tx); i++)
    {
        tx[i] = (uint8_t)(i + 1U);
    }

    /* Writes will be split across pages/blocks automatically. */
    (void)at24c04_write(&eeprom, 0x00F8U, tx, (uint16_t)sizeof(tx));
    (void)at24c04_read(&eeprom,  0x00F8U, rx, (uint16_t)sizeof(rx));
}

```