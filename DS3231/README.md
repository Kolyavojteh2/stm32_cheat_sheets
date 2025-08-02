# DS3231 RTC

## How to use
If your RTC already has the current date and time set, just use the API from the `ds3231.*` files.
### Initialization
```
ds3231_set_i2c_handle(&hi2c1);
```

### Usage
```
datetime_t current_time;
ds3231_get_time(&current_time);

float temperature = ds3231_get_temperature();
```

## How to set the current time in RTC
To do this, you need to additionally copy the ds3231_configure.* API, but this is only needed to load the time to the RTC once.
### Steps to set the current time using UART
#### MCU side:
1. Copy the `ds3231.*` and `ds3231_configure.*` files
2. Add UART initialization for the RTC in the microcontroller code
    ```
    ds3231_set_uart_handle(&huart2);
    ```
3. Add the `ds3231_configure_loop()` function in the microcontroller code in an eternal loop.
    ```
    while (1)
    {
        ds3231_configure_loop();
    }
    ```
4. Upload the firmware to the device.

#### Host side:
1. Build `./configure_clock/PC_utils/CMakeLists.txt`
2. Run the `update_DS3231_time` program.