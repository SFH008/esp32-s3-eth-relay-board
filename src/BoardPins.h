#pragma once
#include <Arduino.h>
#include "esp_mac.h"

namespace BoardPins {
  // W5500 SPI
  constexpr int W5500_CS   = 16;
  constexpr int W5500_INT  = 12;
  constexpr int W5500_RST  = 39;
  constexpr int SPI_SCLK   = 15;
  constexpr int SPI_MISO   = 14;
  constexpr int SPI_MOSI   = 13;

  // I2C
  constexpr int     I2C_SDA     = 42;
  constexpr int     I2C_SCL     = 41;
  constexpr uint8_t TCA9554_ADDR = 0x20;

  // Digital inputs (optocoupler-isolated)
  constexpr uint8_t DI_COUNT = 8;
  constexpr uint8_t DI_PINS[DI_COUNT] = { 4, 5, 6, 7, 8, 9, 10, 11 };

  // Relays (via TCA9554)
  constexpr uint8_t RELAY_COUNT = 8;

  // Misc
  constexpr int BOOT_BTN = 0;
  constexpr int BUZZER   = 46;
  constexpr int RGB_LED  = 38;

  // RS485
  constexpr int RS485_TX = 17;
  constexpr int RS485_RX = 18;

  // SD card
  constexpr int SD_D0  = 45;
  constexpr int SD_CMD = 47;
  constexpr int SD_CLK = 48;

  inline void loadEfuseMac(uint8_t mac[6]) {
    esp_efuse_mac_get_default(mac);
  }

  inline void configInputs() {
    for (uint8_t i = 0; i < DI_COUNT; ++i)
      // Optocouplers pull the GPIO high when active.
      // PULLDOWN keeps the pin stable at LOW when the optocoupler is off.
      // If inputs read inverted, change to INPUT_PULLUP.
      pinMode(DI_PINS[i], INPUT_PULLDOWN);
  }
}

// Flat aliases for code that predates the namespace
constexpr uint8_t  DI_COUNT   = BoardPins::DI_COUNT;
constexpr uint8_t  INPUT_PINS[BoardPins::DI_COUNT] = { 4, 5, 6, 7, 8, 9, 10, 11 };