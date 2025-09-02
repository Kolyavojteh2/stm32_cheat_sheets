# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(DS3231_UART_TOOLS C)

set(CMAKE_C_STANDARD 11)

# Додати першу програму: update_DS3231_time
add_executable(update_DS3231_time update_DS3231_time.c)

# Додати другу програму: get_DS3231_time
add_executable(get_DS3231_time get_DS3231_time.c)
