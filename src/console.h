#ifndef CONSOLE_H
#define CONSOLE_H

#include <cstddef>

struct console_command {
  // Name of the command, optionally followed by a ' ' and some free text.
  const char *usage;

  // The function associated to the command.
  int (*func)(int argc, char *argv[]);

  // Descriptive help text (for "help").
  const char *help;
};

void console_run_once(const char *prompt, const console_command *cmds,
                      size_t num_cmds);

#endif
