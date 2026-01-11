### How to integrate it:
```
#include "main.h"
#include <stdio.h>

#include "gpio.h"
#include "gpio_switch.h"
#include "printf_uart.h"

#include "ds3231.h"
#include "ds3231_time_bridge.h"
#include "at24c04.h"
#include "dht22.h"

#include "hydroponic.h"

/* CubeMX handles */
extern UART_HandleTypeDef huart1;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim3;

/* Instances */
static DS3231_t g_rtc;
static AT24C04_t g_eeprom;
static DHT22_t g_dht22;
static Hydroponic_t g_hydro;

static int mcu_temp_read_stub(void *ctx, float *temp_c)
{
    (void)ctx;
    (void)temp_c;
    return -1;
}

static void app_components_init(void)
{
    /* Retarget printf -> UART */
    set_stdout_uart_handle(&huart1);

    /* ==== DS3231 ==== */
    if (ds3231_init(&g_rtc, &hi2c1, DS3231_ADDR_7BIT) != 0) {
        printf("[app] ds3231_init failed\r\n");
    }

    ds3231_time_bridge_attach(&g_rtc);

    /* ==== AT24C04 ==== */
    if (at24c04_init(&g_eeprom, &hi2c1, 0, 0) != AT24C04_OK) {
        printf("[app] at24c04_init failed\r\n");
    }

    /* ==== DHT22 ==== */
    DHT22_Config_t dcfg;
    dht22_default_config(&dcfg);

    // If there is no pull-up on the board
    dcfg.use_internal_pullup = 1;

    GPIO_t dht_pin;
    dht_pin.port = GPIOA;
    dht_pin.pin = GPIO_PIN_9;

    if (dht22_init(&g_dht22, dht_pin, &htim3, &dcfg) != DHT22_STATUS_OK) {
        printf("[app] dht22_init failed\r\n");
    }

    /* ==== Hydroponic ==== */
    HydroponicConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.rtc = &g_rtc;
    cfg.dht22 = &g_dht22;
    cfg.eeprom = &g_eeprom;

    /* DS3231 INT pin -> PB2 */
    cfg.rtc_int_pin = GPIO_PIN_2;

    /* Light control -> PA15 */
    cfg.light_pin.port = GPIOA;
    cfg.light_pin.pin = GPIO_PIN_15;
    cfg.light_active_level = GPIO_SWITCH_ACTIVE_HIGH;

    /* Error LED -> PB1 (ACTIVE_HIGH) */
    cfg.error_led_pin.port = GPIOB;
    cfg.error_led_pin.pin = GPIO_PIN_1;
    cfg.error_led_active_level = GPIO_SWITCH_ACTIVE_HIGH;

    /* EEPROM base addr */
    cfg.eeprom_base_addr = 0;

    /* Schedule */
    cfg.light_on_hour = 7;
    cfg.light_off_hour = 23;

    /* Optional internal MCU temp */
    cfg.mcu_temp_read = mcu_temp_read_stub; /* or NULL */
    cfg.mcu_temp_ctx = NULL;

    if (hydroponic_init(&g_hydro, &cfg) != 0) {
        printf("[app] hydroponic_init failed\r\n");
    }
}

/* EXTI callback from HAL */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    hydroponic_exti_irq_handler(&g_hydro, GPIO_Pin);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_I2C1_Init();
    MX_TIM3_Init();

    app_components_init();

    while (1)
    {
        /*
         * Sleep until DS3231 pulls INT low (PB2 EXTI).
         * On STM32G0 after STOP you typically need to restore clocks.
         */

        /* Enter STOP mode */
#if defined(STM32G0xx)
        HAL_PWREx_EnterSTOP0Mode(PWR_STOPENTRY_WFI);
#else
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
#endif

        /* Restore clocks after wake-up */
        SystemClock_Config();

        /* Process hydroponic events (does work only if rtc_irq_pending set) */
        (void)hydroponic_process(&g_hydro);
    }
}
```