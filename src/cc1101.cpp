#include "cc1101.h"

#include "pinmap.h"

#include <hardware/spi.h>
#include <pico/binary_info.h>
#include <pico/stdlib.h>

static spi_inst_t *SPI_PORT = spi0;
bi_decl(bi_3pins_with_func(PIN_MISO, PIN_MOSI, PIN_SCK, GPIO_FUNC_SPI));
bi_decl(bi_1pin_with_name(PIN_CS, "CS"));

static inline void cs_select() { gpio_put(PIN_CS, false); }

static inline void cs_deselect() { gpio_put(PIN_CS, true); }

static void wait_miso_low() {
  while (gpio_get(PIN_MISO) != false) {
    tight_loop_contents();
  }
}

void cc1101_init() {
  gpio_init(PIN_CS);
  gpio_init(PIN_SCK);
  gpio_init(PIN_MOSI);
  gpio_init(PIN_MISO);

  // Configure output pins' initial state.
  gpio_put(PIN_CS, true);
  gpio_put(PIN_SCK, true);
  gpio_put(PIN_MOSI, false);
  gpio_set_dir(PIN_CS, GPIO_OUT);
  gpio_set_dir(PIN_SCK, GPIO_OUT);
  gpio_set_dir(PIN_MOSI, GPIO_OUT);

  // Enable pull-up on MISO so we can sense it in wait_miso_low.
  gpio_set_pulls(PIN_MISO, true, false);

  // Pulse the CS line. This is the initial part of the "Manual Reset" sequence,
  // which must be followed by a RESET strobe.
  sleep_us(5);
  cs_select();
  sleep_us(5);
  cs_deselect();
  sleep_us(50);

  // Let the SPI module take over.
  spi_init(SPI_PORT, 1000000);
  spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
}

void cc1101_exec(cc1101_strobe strobe) {
  uint8_t tx[1] = {
      (uint8_t)strobe,
  };

  cs_select();
  wait_miso_low();
  spi_write_blocking(SPI_PORT, tx, sizeof(tx));
  wait_miso_low();
  cs_deselect();
}

uint8_t cc1101_read(cc1101_reg reg) {
  uint8_t tx[1] = {
      (uint8_t)((uint8_t)reg | 0xc0),  // read+burst
  };
  uint8_t rx[1];

  cs_select();
  wait_miso_low();
  spi_write_blocking(SPI_PORT, tx, sizeof(tx));
  spi_read_blocking(SPI_PORT, 0xFF, rx, sizeof(rx));
  cs_deselect();

  return rx[0];
}

void cc1101_write(cc1101_reg reg, uint8_t value) {
  uint8_t tx[2] = {
      (uint8_t)((uint8_t)reg | 0x40),  // write+burst
      value,
  };

  cs_select();
  wait_miso_low();
  spi_write_blocking(SPI_PORT, tx, sizeof(tx));
  cs_deselect();
}

void cc1101_write_burst(cc1101_reg reg, const uint8_t *values, size_t len) {
  uint8_t tx_header[1] = {
      (uint8_t)((uint8_t)reg | 0x40),  // write+burst
  };

  cs_select();
  wait_miso_low();
  spi_write_blocking(SPI_PORT, tx_header, sizeof(tx_header));
  spi_write_blocking(SPI_PORT, values, len);
  cs_deselect();
}

void cc1101_write_freq012(double freq_mhz) {
  uint32_t value = (uint32_t)(freq_mhz * 0x10000 / 26 + 0.5);
  cc1101_write(cc1101_reg::FREQ2, (uint8_t)(value >> 16));
  cc1101_write(cc1101_reg::FREQ1, (uint8_t)(value >> 8));
  cc1101_write(cc1101_reg::FREQ0, (uint8_t)(value >> 0));
}
