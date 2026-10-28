#include <stdint.h>
#include "solver_core.h"

/* For RV32I under rv32emu's supported write(64) syscall. */
static void write_text(const char *s)
{
    uint32_t n = 0;
    while (s[n]) ++n;
    register long a0 asm("a0") = 1;
    register const char *a1 asm("a1") = s;
    register uint32_t a2 asm("a2") = n;
    register long a7 asm("a7") = 64;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

int main(void)
{
    uint8_t path[11] = {0};
    if (!minirubik_smoke_test()) { write_text("move smoke FAIL\n"); return 1; }
    if (!minirubik_init()) { write_text("table init FAIL\n"); return 2; }
    int length = minirubik_solve("12345671111111", path);
    if (length != 0) { write_text("solved-state FAIL\n"); return 3; }
    write_text("Mini-Rubik bare-metal smoke PASS\n");
    return 0;
}
