#ifndef Pins_Arduino_h
#define Pins_Arduino_h
#include <stdint.h>

#include "soc/soc_caps.h"

// Tab5 external interfaces. Display/touch/power are initialized by M5Unified.
static const uint8_t TX = 6;
static const uint8_t RX = 7;
static const uint8_t SDA = 53;
static const uint8_t SCL = 54;
static const uint8_t SS = 42;
static const uint8_t MOSI = 44;
static const uint8_t MISO = 39;
static const uint8_t SCK = 43;

// SD card: on-chip LDO4 for the bus, external IO expander for card power.
// Do not define BOARD_SDMMC_POWER_PIN: GPIO45 is a free M5Bus GPIO on Tab5.
#define BOARD_HAS_SDMMC
#define BOARD_SDMMC_SLOT 0
#define BOARD_SDMMC_POWER_CHANNEL 4

// ESP32-C6 internal SDIO connection (official Tab5 Wi-Fi example).
#define BOARD_HAS_SDIO_ESP_HOSTED
#define BOARD_SDIO_ESP_HOSTED_CLK 12
#define BOARD_SDIO_ESP_HOSTED_CMD 13
#define BOARD_SDIO_ESP_HOSTED_D0 11
#define BOARD_SDIO_ESP_HOSTED_D1 10
#define BOARD_SDIO_ESP_HOSTED_D2 9
#define BOARD_SDIO_ESP_HOSTED_D3 8
#define BOARD_SDIO_ESP_HOSTED_RESET 15
#endif
