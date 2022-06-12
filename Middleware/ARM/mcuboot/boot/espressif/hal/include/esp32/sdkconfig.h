/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define BOOTLOADER_BUILD 1
#define CONFIG_IDF_FIRMWARE_CHIP_ID 0x0000
#define CONFIG_IDF_TARGET_ESP32 1
#define CONFIG_ESP32_REV_MIN_3 1
#define CONFIG_ESP32_REV_MIN 3
#define CONFIG_SPI_FLASH_ROM_DRIVER_PATCH 1
#define CONFIG_ESP32_XTAL_FREQ 40
#define CONFIG_MCUBOOT 1
#define NDEBUG 1
#define CONFIG_BOOTLOADER_WDT_TIME_MS 9000
#define CONFIG_ESP_CONSOLE_UART_BAUDRATE 115200
#define CONFIG_BOOTLOADER_OFFSET_IN_FLASH 0x1000
#define CONFIG_PARTITION_TABLE_OFFSET 0x10000
#define CONFIG_EFUSE_VIRTUAL_OFFSET 0x250000
#define CONFIG_EFUSE_VIRTUAL_SIZE 0x2000
#define CONFIG_EFUSE_MAX_BLK_LEN 192
