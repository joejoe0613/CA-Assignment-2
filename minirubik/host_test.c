#include <stdio.h>
#include <stdint.h>
#include "solver_core.h"
int main(void)
{
    uint8_t path[11] = {0};
    if (!minirubik_smoke_test() || !minirubik_init()) return 1;
    int length = minirubik_solve("12345671111111", path);
    if (length != 0) return 2;
    if (minirubik_solve("invalid", path) != -1) return 3;
    printf("Host smoke PASS: solved=%d, invalid=-1\n", length);
    return 0;
}
