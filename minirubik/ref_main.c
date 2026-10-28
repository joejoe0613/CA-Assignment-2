/* ref_main.c -- 供 riscv64-unknown-elf-gcc -O2 -march=rv32i 編譯的 C 基準
 *
 * 演算法 = solver_opt.c 原封不動 (已套用 D)；只換外殼:
 *   - 原本的 main 變成 static 而且沒人呼叫，-O2 會整個丟掉，stdio 就不會被連進來
 *   - 表格用預先算好的初始化資料，和組語版使用同樣的四張表，不呼叫 build_tables()
 *   - 輸入寫死；結果 (最短長度) 當成 exit code 回傳
 *
 * 量測的工作量: parse_state -> rank_state -> solve_ida
 */
#ifndef SOLVER_FILE
#define SOLVER_FILE "solver_opt.c"
#endif
#ifndef INPUT
#define INPUT "21345671111111"
#endif

#define main static solver_main_unused   /* "int main" 變成 "int static solver_main_unused" */
#include SOLVER_FILE
#undef main

#include "ref_tables.inc"   /* 初始化版本的四張表 */

static const char input[] = INPUT;

int main(void)
{
    state_t state;
    uint8_t path[MAX_SOLUTION];

    if (!parse_state(input, &state))
        return 100;                     /* 輸入不合法 */
    uint32_t rank = rank_state(&state);
    return solve_ida((uint16_t) (rank / ORIENTATIONS),
                     (uint16_t) (rank % ORIENTATIONS), path);
}

#ifndef HOST_TEST
/* 裸機進入點: 放在 .text.startup, 預設連結腳本會把它排在 .text 最前面 (位址 0) */
__asm__(".section .text.startup,\"ax\",@progbits\n"
        ".globl _start\n"
        "_start:\n"
        "    jal  ra, main\n"
        "    li   a7, 93\n"            /* Ripes: a7=93 結束，a0 = exit code */
        "    ecall\n");
#endif
