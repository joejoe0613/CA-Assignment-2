#ifndef MINIRUBIK_SOLVER_CORE_H
#define MINIRUBIK_SOLVER_CORE_H
#include <stdint.h>
int minirubik_init(void);
int minirubik_solve(const char *input, uint8_t path[11]);
const char *minirubik_move_name(unsigned move);
int minirubik_smoke_test(void);
#endif
