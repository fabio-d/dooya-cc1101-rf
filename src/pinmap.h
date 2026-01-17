#ifndef PINMAP_H
#define PINMAP_H

#include <pico/types.h>

static constexpr uint PIN_MISO = 4;
static constexpr uint PIN_CS = 5;
static constexpr uint PIN_SCK = 6;
static constexpr uint PIN_MOSI = 7;

static constexpr uint PIN_GDO0 = 2;  // MCU TX -> CC1101
static constexpr uint PIN_GDO2 = 3;  // CC1101 -> MCU RX

#endif
