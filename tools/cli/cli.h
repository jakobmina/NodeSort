/* Shared helpers for the `nodesort` command-line tool.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_CLI_H
#define NODESORT_CLI_H

#include <stddef.h>
#include <stdint.h>

#include "array_io.h"
#include "nodesort/nodesort.h"

enum { EXIT_OK = 0, EXIT_USAGE = 1, EXIT_FILE = 2, EXIT_SORT = 3 };

int cmd_sort(int argc, char **argv);
int cmd_top(int argc, char **argv);
int cmd_demo(int argc, char **argv);

/* Prints "nodesort: <message>" to stderr. */
CLI_PRINTF(1, 2) void cli_error(const char *fmt, ...);

double now_ms(void);
int stdout_is_terminal(void);
unsigned cpu_count(void);

/* Parses a non-negative integer; accepts underscores and k/M/G suffixes (1k = 1000). */
int parse_count(const char *s, uint64_t *out);

/* Formats n with thousands separators into buf. */
const char *fmt_count(uint64_t n, char *buf, size_t len);

/* Sorts a 1-D array of any supported element type in place. */
nodesort_status sort_array(array_t *a, const nodesort_options *opt);

/* Prints element i of `a` (format depends on the type). */
void print_element(const array_t *a, size_t i);

/* Option-scanner helper: returns the value after argv[*i] or NULL (and reports) if missing. */
const char *option_value(int argc, char **argv, int *i);

#endif /* NODESORT_CLI_H */
