#include "console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pico/stdio.h>

// Character buffer size (final '\0' excluded).
static constexpr size_t MAX_CHARS = 127;

// Maximum argv length (final nullptr excluded): all strings of length 1.
static constexpr size_t MAX_ARGC = (MAX_CHARS + 1) / 2;

static char char_buffer[MAX_CHARS + 1];
static char *argv_buffer[MAX_ARGC + 1];

static void print_raw_and_flush(const char *text) {
  while (*text != '\0') {
    putchar_raw(*text++);
  }
  stdio_flush();
}

static void read_into_char_buffer() {
  bool return_pressed = false;
  size_t char_buffer_pos = 0;
  while (!return_pressed) {
    switch (int ch = getchar()) {
      case '\r':
        print_raw_and_flush("\r\n");
        return_pressed = true;
        break;
      case '\b':
        if (char_buffer_pos != 0) {
          print_raw_and_flush("\b \b");
          char_buffer_pos--;
        }
        break;
      default:
        if (char_buffer_pos != MAX_CHARS) {
          putchar_raw(ch);
          stdio_flush();
          char_buffer[char_buffer_pos++] = ch;
        }
        break;
    }
  }
  char_buffer[char_buffer_pos] = '\0';
}

static int split_argv() {
  char *saveptr, *ptr = char_buffer;
  int argc = 0;
  while (char *arg = strtok_r(ptr, " ", &saveptr)) {
    ptr = nullptr;
    argv_buffer[argc++] = arg;
  }
  argv_buffer[argc] = nullptr;
  return argc;
}

static void print_help(const console_command *cmds, size_t num_cmds) {
  // Find longest command (at least "help", i.e. 4).
  size_t max_usage_len = 4;
  for (size_t i = 0; i < num_cmds; ++i) {
    size_t usage_len = strlen(cmds[i].usage);
    if (max_usage_len < usage_len) {
      max_usage_len = usage_len;
    }
  }

  auto print_cmd = [max_usage_len](const char *usage, const char *help) {
    // Print usage text, padded to max_usage_len characters.
    size_t usage_len = strlen(usage);
    for (size_t i = 0; i < max_usage_len; ++i) {
      putchar(i < usage_len ? usage[i] : ' ');
    }

    // Print separator (two ' ' characters) and the help text.
    printf("  %s\n", help);
  };

  print_cmd("help", "Show this message");
  for (size_t i = 0; i < num_cmds; ++i) {
    print_cmd(cmds[i].usage, cmds[i].help);
  }
}

void console_run_once(const char *prompt, const console_command *cmds,
                      size_t num_cmds) {
  print_raw_and_flush(prompt);
  read_into_char_buffer();
  int argc = split_argv();

  if (argc == 0) {
    return;
  }

  const char *argv0 = argv_buffer[0];
  if (strcasecmp(argv0, "help") == 0) {
    print_help(cmds, num_cmds);
    return;
  }

  for (size_t i = 0; i < num_cmds; ++i) {
    // Extract the first word from the usage text.
    const char *name = cmds[i].usage;
    const char *name_end = name;
    while (*name_end != '\0' && *name_end != ' ') {
      ++name_end;
    }

    // argv0 should match the name.
    size_t name_len = name_end - name;
    if (strlen(argv0) == name_len && strncasecmp(argv0, name, name_len) == 0) {
      int exit_code = cmds[i].func(argc, argv_buffer);
      if (exit_code != EXIT_SUCCESS) {
        printf("Warning! Non-successful exit code %d\n", exit_code);
      }
      return;
    }
  }

  printf("%s: command not found\n", argv0);
}
