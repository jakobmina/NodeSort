/* SPDX-License-Identifier: MIT */
#include <nodesort/nodesort.h>
#include <stdio.h>

int main(void) {
    uint32_t a[] = {5, 3, 9, 1};
    if (nodesort_u32(a, 4, NULL) != NODESORT_OK) return 1;
    if (a[0] != 1 || a[1] != 3 || a[2] != 5 || a[3] != 9) return 1;
    printf("consumer ok, nodesort %s\n", nodesort_version());
    return 0;
}
