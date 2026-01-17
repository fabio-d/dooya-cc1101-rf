// Adapted from flipperzero-firmware/lib/subghz/protocols/dooya.c
#include "cc1101.h"
#include "console.h"
#include "pinmap.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <pico/binary_info.h>
#include <pico/stdio.h>
#include <pico/stdlib.h>

bi_decl(bi_1pin_with_name(PIN_GDO0, "GDO0"));
bi_decl(bi_1pin_with_name(PIN_GDO2, "GDO2"));

static constexpr uint64_t te_short = 366;
static constexpr uint64_t te_long = 733;
static constexpr uint64_t te_delta = 120;

static std::optional<uint32_t> selected_serial;
static uint8_t selected_channel = 0x05;  // single channel, unless overridden

static bool was_return_pressed() {
  int ch = getchar_timeout_us(0);
  return ch == '\r';
}

// Expects and parses only one hexadecimal argument with an exact number of
// digits.
static std::optional<uint32_t> parse_hex_argument(int argc, char *argv[],
                                                  size_t ndigits) {
  unsigned long value;
  char *endp;
  if (argc != 2 ||
      (value = strtoul(argv[1], &endp, 16), endp != argv[1] + ndigits) ||
      *endp != '\0') {
    printf("Usage: %s <hex-value-with-%zu-digits>\n", argv[0], ndigits);
    return std::nullopt;
  }
  return value;
}

static uint64_t duration_diff(uint64_t a, uint64_t b) {
  return a > b ? (a - b) : (b - a);
}

static const char *get_command_name(uint8_t val) {
  switch (val) {
    case 0x11:
      return "up_pressed";
    case 0x1E:
      return "up_depressed";
    case 0x33:
      return "down_pressed";
    case 0x3C:
      return "down_depressed";
    case 0x55:
      return "stop";
    default:
      return "<unknown>";
  }
}

static void print_packet(uint64_t buf) {
  printf("%010llX: serial=%06X channel=%02X command=%s(%02X)",
         static_cast<unsigned long long>(buf),               // raw
         static_cast<unsigned int>((buf >> 16) & 0xFFFFFF),  // serial
         static_cast<unsigned int>((buf >> 8) & 0xFF),       // channel
         get_command_name(buf & 0xFF),                       // command name
         static_cast<unsigned int>(buf & 0xFF)               // command value
  );
}

static void pulse_us(bool value, uint64_t us) {
  gpio_put(PIN_GDO0, value);
  busy_wait_us(us);
}

static void send_packet(uint64_t buf) {
  print_packet(buf);
  printf(" [tx timestamp=%llu us]\n",
         static_cast<unsigned long long>(time_us_64()));

  // Header.
  pulse_us(false, te_long * 12);

  // Start bit.
  pulse_us(true, te_short * 13);
  pulse_us(false, te_long * 2);

  // Data bits.
  for (uint i = 0; i < 40; i++) {
    if (buf & (uint64_t{1} << (39 - i))) {
      pulse_us(true, te_long);
      pulse_us(false, te_short);
    } else {
      pulse_us(true, te_short);
      pulse_us(false, te_long);
    }
  }
}

static int do_sniff(int argc, char *argv[]) {
  cc1101_exec(cc1101_strobe::SRX);
  puts(
      "Sniffer started, press RETURN to stop. "
      "Keep the remote close to the antenna!");

  enum decode_state_t : int {
    DECODE_INVALID,
    DECODE_START,
    DECODE_DATA0,
    DECODE_DATA39 = DECODE_DATA0 + 39,
  } decode_state = DECODE_INVALID;
  uint64_t prev_ts = time_us_64();
  bool prev_val = false;

  uint64_t buf_ts;
  uint64_t buf;

  while (!was_return_pressed()) {
    bool curr_val = gpio_get(PIN_GDO2);
    if (curr_val == prev_val) {
      continue;
    }

    uint64_t curr_ts = time_us_64();
    uint64_t delta_ts = curr_ts - prev_ts;

    switch (decode_state) {
      case DECODE_INVALID: {
        if (!prev_val) {
          decode_state = DECODE_START;
        }
        break;
      }
      case DECODE_START: {
        if (prev_val) {
          if (duration_diff(delta_ts, te_short * 13) >= te_delta * 5) {
            decode_state = DECODE_INVALID;
          }
        } else {
          if (duration_diff(delta_ts, te_long * 2) >= te_delta * 3) {
            decode_state = DECODE_INVALID;
          } else {
            decode_state = DECODE_DATA0;

            // Start receiving the packet.
            buf_ts = curr_ts;
            buf = 0;
          }
        }
        break;
      }
      default: {
        if (prev_val) {
          if (duration_diff(delta_ts, te_long) < te_delta) {
            buf = (buf << 1) | 1;
          } else if (duration_diff(delta_ts, te_short) < te_delta) {
            buf = (buf << 1) | 0;
          } else {
            decode_state = DECODE_INVALID;
          }

          if (decode_state == DECODE_DATA39) {
            // We have a valid packet! Let's print it.
            print_packet(buf);
            printf(" [rx timestamp=%llu us]\n",
                   static_cast<unsigned long long>(buf_ts));

            // Reset the state machine, to be ready for the next packet.
            decode_state = DECODE_INVALID;
          }
        } else {
          uint64_t complementary_duration = (buf & 1) ? te_short : te_long;
          if (duration_diff(delta_ts, complementary_duration) >= te_delta) {
            decode_state = DECODE_INVALID;
          } else {
            decode_state = (decode_state_t)(decode_state + 1);
          }
        }
        break;
      }
    }

    prev_ts = curr_ts;
    prev_val = curr_val;
  }

  cc1101_exec(cc1101_strobe::SIDLE);
  return EXIT_SUCCESS;
}

static int do_set_serial(int argc, char *argv[]) {
  if (auto val = parse_hex_argument(argc, argv, 6)) {
    selected_serial = *val;
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}

static int do_set_channel(int argc, char *argv[]) {
  if (auto val = parse_hex_argument(argc, argv, 2)) {
    selected_channel = *val;
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}

static int do_cmd_impl(uint8_t cmd1,
                       std::optional<uint8_t> cmd2 = std::nullopt) {
  if (!selected_serial.has_value()) {
    puts("No serial has been configured yet, call \"set-serial\" first.");
    return EXIT_FAILURE;
  }

  uint64_t base_cmd =
      (static_cast<uint64_t>(*selected_serial) << 16) | (selected_channel << 8);

  cc1101_exec(cc1101_strobe::STX);
  gpio_set_dir(PIN_GDO0, true);
  sleep_ms(10);

  for (uint i = 0; i < 5; i++) {
    send_packet(base_cmd | cmd1);
  }

  if (cmd2.has_value()) {
    for (uint i = 0; i < 5; i++) {
      send_packet(base_cmd | *cmd2);
    }
  }

  sleep_ms(10);
  gpio_set_dir(PIN_GDO0, false);
  cc1101_exec(cc1101_strobe::SIDLE);

  return EXIT_SUCCESS;
}

static int do_up(int argc, char *argv[]) { return do_cmd_impl(0x11, 0x1E); }

static int do_down(int argc, char *argv[]) { return do_cmd_impl(0x33, 0x3C); }

static int do_stop(int argc, char *argv[]) { return do_cmd_impl(0x55); }

static constexpr console_command cmds[] = {
    {
        .usage = "sniff",
        .func = do_sniff,
        .help = "Sniff and dump packets",
    },
    {
        .usage = "set-serial <hex-value-with-6-digits>",
        .func = do_set_serial,
        .help = "Set the serial number to be emitted",
    },
    {
        .usage = "set-channel <hex-value-with-2-digits>",
        .func = do_set_channel,
        .help = "Set the channel value to be emitted (default: 05)",
    },
    {
        .usage = "up",
        .func = do_up,
        .help = "Send the UP command",
    },
    {
        .usage = "down",
        .func = do_down,
        .help = "Send the DOWN command",
    },
    {
        .usage = "stop",
        .func = do_stop,
        .help = "Send the STOP command",
    },
};

int main() {
  stdio_init_all();
  sleep_ms(4000);

  puts("Initializing CC1101...");
  cc1101_init();
  cc1101_exec(cc1101_strobe::SRES);
  uint8_t partnum = cc1101_read(cc1101_reg::PARTNUM);
  uint8_t version = cc1101_read(cc1101_reg::VERSION);
  printf("CC1101: detected chip ID = %02x%02x\n", partnum, version);
  hard_assert(partnum == 0x00 && version == 0x14);

  // GDO2 function = Serial Data Output.
  cc1101_write(cc1101_reg::IOCFG2, 0x0D);

  // GDO0 function = None (high impedance).
  cc1101_write(cc1101_reg::IOCFG0, 0x2E);

  // Asynchronous serial mode, infinite length.
  cc1101_write(cc1101_reg::PKTCTRL0, 0x32);

  // Carrier frequency (MHz).
  cc1101_write_freq012(433.92);

  // RX filter bandwidth = 81 kHz.
  cc1101_write(cc1101_reg::MDMCFG4, 0xDC);

  // Set modulation to ASK/OOK.
  cc1101_write(cc1101_reg::MDMCFG2, 0x30);

  // Auto-calibrate when going from IDLE to SRX/STX.
  cc1101_write(cc1101_reg::MCSM0, 0x18);

  // Front End TX config: PATABLE index = 1.
  cc1101_write(cc1101_reg::FREND0, 0x11);

  // Set TX power level (0xC0 = 10 dBm).
  const uint8_t patable[8] = {0, 0xC0, 0, 0, 0, 0, 0, 0};
  cc1101_write_burst(cc1101_reg::PATABLE, patable, sizeof(patable));

  cc1101_exec(cc1101_strobe::SIDLE);

  gpio_init(PIN_GDO0);
  gpio_put(PIN_GDO0, false);  // idle by default
  gpio_init(PIN_GDO2);

  while (true) {
    console_run_once("dooya-cc1101-rf> ", cmds, sizeof(cmds) / sizeof(cmds[0]));
  }
}
