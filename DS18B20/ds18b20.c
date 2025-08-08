/*
 * ds18b20.c
 *
 *  Created on: Aug 8, 2025
 *      Author: vojt
 */

#include <stdbool.h>
#include "ds18b20.h"

/* --- Retry policy --- */
/* How many times to retry full read sequence on CRC failure */
#ifndef DS18B20_MAX_RETRIES
#define DS18B20_MAX_RETRIES (3)
#endif

/* Delay between retries (ms) to let the bus settle */
#ifndef DS18B20_RETRY_DELAY_MS
#define DS18B20_RETRY_DELAY_MS (10)
#endif

/* ---- 1-Wire commands ---- */
#define CMD_SKIP_ROM    (0xCC)
#define CMD_CONVERT_T   (0x44)
#define CMD_READ_SCR    (0xBE)
#define CMD_WRITE_SCR   (0x4E)
#define CMD_COPY_SCR    (0x48)

// Delay times (ms) for resolutions 9..12 bits, indexed by DS18B20_RESOLUTION enum
static uint32_t ds18b20_convertion_delay[] = { 94, 188, 375, 750 };

static void DS18B20_delay_us(DS18B20_t *instance, uint32_t delay_us)
{
    /* Busy-wait using CNT. Works as long as us < timer period. */
    uint32_t start = __HAL_TIM_GET_COUNTER(instance->timer);
    while ((uint32_t)(__HAL_TIM_GET_COUNTER(instance->timer) - start) < delay_us)
    {
    	__NOP();
    }
}

/* Switch pin to output and drive low (simulate open-drain) */
static inline void DS18B20_pin_drive_low(DS18B20_t *instance)
{
    GPIO_InitTypeDef io = {0};
    io.Pin = instance->pin;
    io.Mode = GPIO_MODE_OUTPUT_PP;  /* push-pull to force low */
    io.Pull = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(instance->port, &io);
    HAL_GPIO_WritePin(instance->port, instance->pin, GPIO_PIN_RESET);
}

/* Release line (input, external pull-up keeps it high) */
static inline void DS18B20_pin_release(DS18B20_t *instance)
{
    GPIO_InitTypeDef io = {0};
    io.Pin = instance->pin;
    io.Mode = GPIO_MODE_INPUT;      /* Hi-Z */
    io.Pull = GPIO_NOPULL;          /* external pull-up (typically 4.7kΩ, up to 10kΩ for short lines) */
    HAL_GPIO_Init(instance->port, &io);
}

/* Read current level */
static inline uint8_t DS18B20_read(DS18B20_t *instance)
{
    return (uint8_t)HAL_GPIO_ReadPin(instance->port, instance->pin);
}

/* 1-Wire reset + presence detect */
static bool DS18B20_reset(DS18B20_t *instance)
{
	DS18B20_pin_drive_low(instance);
	DS18B20_delay_us(instance, 500);  /* reset low ~480-500 us */
	DS18B20_pin_release(instance);
	DS18B20_delay_us(instance, 70);   /* wait before sampling presence */
    bool presence = (DS18B20_read(instance) == 0);
    DS18B20_delay_us(instance, 410);  /* finish presence window */
    return presence;
}

static void DS18B20_write_bit(DS18B20_t *instance, uint8_t bit)
{
    if (bit)
    {
        /* Write '1': pull low ~6-10us, then release to end of slot (~60us) */
    	DS18B20_pin_drive_low(instance);
    	DS18B20_delay_us(instance, 6);
    	DS18B20_pin_release(instance);
    	DS18B20_delay_us(instance, 64);
    }
    else
    {
        /* Write '0': keep low ~60us */
    	DS18B20_pin_drive_low(instance);
    	DS18B20_delay_us(instance, 60);
    	DS18B20_pin_release(instance);
    	DS18B20_delay_us(instance, 10);
    }
}

static void DS18B20_write_byte(DS18B20_t *instance, uint8_t data)
{
    for (int i = 0; i < 8; i++)
    {
    	DS18B20_write_bit(instance, data & 0x01);
        data >>= 1;
    }
}

static uint8_t DS18B20_read_bit(DS18B20_t *instance)
{
    uint8_t bit;
    /* Read slot: pull low ~2us, release, sample ~15us from start */
    DS18B20_pin_drive_low(instance);
    DS18B20_delay_us(instance, 2);
    DS18B20_pin_release(instance);
    DS18B20_delay_us(instance, 13);
    bit = DS18B20_read(instance);
    /* complete slot to ~60us */
    DS18B20_delay_us(instance, 45);
    return bit;
}

static uint8_t DS18B20_read_byte(DS18B20_t *instance)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++)
    {
    	data >>= 1;
        if (DS18B20_read_bit(instance))
        	data |= 0x80;
    }
    return data;
}

/* Dallas/Maxim CRC8 */
static uint8_t crc8_maxim(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    while (len--)
    {
        uint8_t inbyte = *data++;
        for (uint8_t i = 0; i < 8; i++)
        {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

/* Start temperature conversion */
static bool DS18B20_start_conversion(DS18B20_t *instance)
{
    if (!DS18B20_reset(instance))
    	return false;

    DS18B20_write_byte(instance, CMD_SKIP_ROM);
    DS18B20_write_byte(instance, CMD_CONVERT_T);

    return true;
}

/* Read 9-byte scratchpad, return true if CRC OK */
static bool DS18B20_read_scratchpad(DS18B20_t *instance, uint8_t sp[9])
{
    if (!DS18B20_reset(instance))
    	return false;
    DS18B20_write_byte(instance, CMD_SKIP_ROM);
    DS18B20_write_byte(instance, CMD_READ_SCR);
    for (int i = 0; i < 9; i++)
    	sp[i] = DS18B20_read_byte(instance);

    return (crc8_maxim(sp, 9) == 0);
}

int DS18B20_init(DS18B20_t *instance,
                 TIM_HandleTypeDef *timer_handle,
                 GPIO_TypeDef *data_port,
                 uint16_t data_pin)
{
    if (!instance || !timer_handle || !data_port)
    	return -1;

    instance->port  = data_port;
    instance->pin   = data_pin;
    instance->timer = timer_handle;
    instance->resolution = DS18B20_RESOLUTION_12_BIT;

    /* Start the timer in free-run mode if not already running */
    if (HAL_TIM_Base_GetState(timer_handle) == HAL_TIM_STATE_RESET)
    {
        return -2; /* timer not inited via HAL_TIM_Base_Init */
    }
    if (HAL_TIM_Base_Start(timer_handle) != HAL_OK)
    {
        /* It is safe to call Start() even if already started; handle errors anyway */
        return -3;
    }

    /* Release the line (input) initially */
    DS18B20_pin_release(instance);

    /* Optional: presence check right away */
    if (!DS18B20_reset(instance))
        return -4;

    return 0;
}

int DS18B20_get_data(DS18B20_t *instance, float *temperature)
{
    if (!instance || !temperature)
    	return -1;

    if (instance->resolution < DS18B20_RESOLUTION_9_BIT || instance->resolution > DS18B20_RESOLUTION_12_BIT)
    	return -2;

    for (int attempt = 0; attempt < DS18B20_MAX_RETRIES; ++attempt)
    {
        /* Start conversion */
        if (!DS18B20_start_conversion(instance))
        {
            /* If reset/presence failed, try next attempt */
            HAL_Delay(DS18B20_RETRY_DELAY_MS);
            continue;
        }

        /* Max conversion time at 12-bit is 750 ms */
        HAL_Delay(ds18b20_convertion_delay[instance->resolution]);

        /* Read scratchpad and check CRC */
        uint8_t sp[9];
        if (DS18B20_read_scratchpad(instance, sp))
        {
            /* sp[0]=LSB, sp[1]=MSB -> 1/16°C, two's complement */
            int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
            *temperature = (float)raw / 16.0f;
            return 0; /* success */
        }

        /* CRC failed: short settle time and retry whole sequence */
        HAL_Delay(DS18B20_RETRY_DELAY_MS);
    }

    /* All attempts failed */
    return -3; /* CRC / read failure */
}

int DS18B20_set_resolution(DS18B20_t *instance, DS18B20_RESOLUTION resolution)
{
    if (!instance)
    	return -1;

    if (resolution < DS18B20_RESOLUTION_9_BIT || resolution > DS18B20_RESOLUTION_12_BIT)
    	return -2;

    if (!DS18B20_reset(instance))
    	return -3;

    DS18B20_write_byte(instance, CMD_SKIP_ROM);
    DS18B20_write_byte(instance, CMD_WRITE_SCR);

    /* Write TH and TL user bytes (leave defaults 0x4B, 0x46) */
    DS18B20_write_byte(instance, 0x4B);  // TH
    DS18B20_write_byte(instance, 0x46);  // TL

    /* Config register: only R1:R0 (bits 6:5) matter */
    uint8_t cfg = 0x1F; /* 00011111 */
    cfg |= (resolution << 5);
    DS18B20_write_byte(instance, cfg);

    /* Copy scratchpad to EEPROM (t_WR up to 10 ms) */
    if (!DS18B20_reset(instance))
    	return -4;

    DS18B20_write_byte(instance, CMD_SKIP_ROM);
    DS18B20_write_byte(instance, CMD_COPY_SCR);
    HAL_Delay(15);

    instance->resolution = resolution;

    return 0;
}

