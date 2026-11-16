/* Minimal nodesort usage: sort with defaults, then with explicit options.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>

#include <nodesort/nodesort.h>

int main(void) {
    int32_t values[] = {42, -7, 2147483647, 0, -2147483647 - 1, 13, 13, -1};
    size_t n = sizeof values / sizeof values[0];

    nodesort_status st = nodesort_i32(values, n, NULL);
    if (st != NODESORT_OK) {
        fprintf(stderr, "nodesort failed: %s\n", nodesort_strerror(st));
        return 1;
    }

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = 2; /* 0 would mean "all available cores" */
    st = nodesort_i32(values, n, &opt);
    if (st != NODESORT_OK) {
        fprintf(stderr, "nodesort failed: %s\n", nodesort_strerror(st));
        return 1;
    }

    printf("nodesort %s:", nodesort_version());
    for (size_t i = 0; i < n; i++) printf(" %d", (int)values[i]);
    printf("\n");
    return 0;
}
