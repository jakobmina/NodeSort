/* nodesort - command-line front end of the nodesort library.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

#include "cli.h"

static void usage(FILE *out) {
    fprintf(out, "usage: nodesort <command> [options]\n"
                 "\n"
                 "commands:\n"
                 "  sort   sort a 1-D array from a .npy or raw file\n"
                 "  top    top-k most similar rows of an embedding matrix to a query vector\n"
                 "  demo   live race: nodesort vs qsort on random data\n"
                 "\n"
                 "Run 'nodesort <command> --help' for the options of a command.\n"
                 "nodesort --version prints the version.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return EXIT_USAGE;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        usage(stdout);
        return EXIT_OK;
    }
    if (strcmp(cmd, "--version") == 0) {
        printf("nodesort %s\n", nodesort_version());
        return EXIT_OK;
    }
    if (strcmp(cmd, "sort") == 0) return cmd_sort(argc - 1, argv + 1);
    if (strcmp(cmd, "top") == 0) return cmd_top(argc - 1, argv + 1);
    if (strcmp(cmd, "demo") == 0) return cmd_demo(argc - 1, argv + 1);
    cli_error("unknown command '%s'", cmd);
    usage(stderr);
    return EXIT_USAGE;
}
