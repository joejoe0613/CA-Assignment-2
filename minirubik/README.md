# Mini-Rubik bare-metal port (initial smoke test)

This is a minimal extraction of the supplied `solver_opt.c`, not a full Assignment 2 submission.
The IDA* core and transition/distance generation have not been rewritten.

- `solver_core.c/.h`: functions callable without argc/argv/stdio.
- `baremetal_runtime.c`: freestanding `memset` used to initialize tables.
- `mini_driver.c`: standalone **RV32I** test entry, with write(64) and `main` returning exit status.
- `host_test.c`: quick native test (not linked into the bare-metal ELF).

Host smoke test:

```sh
gcc -O2 -std=c11 -Wall -Wextra solver_core.c baremetal_runtime.c host_test.c -o host_test
./host_test
```

For a separate bare-metal ELF using the existing Playground `start.S`, `linker.ld`, `perfcounter.S`:

```sh
riscv-none-elf-gcc -O2 -std=gnu11 -march=rv32i_zicsr -mabi=ilp32 -ffreestanding -fno-builtin -Wall -Wextra -c solver_core.c baremetal_runtime.c mini_driver.c
riscv-none-elf-gcc -march=rv32i_zicsr -mabi=ilp32 -ffreestanding -fno-builtin -c start.S
riscv-none-elf-gcc -nostdlib -Wl,-T,linker.ld -Wl,--no-warn-rwx-segments -o minirubik.elf start.o solver_core.o baremetal_runtime.o mini_driver.o
riscv-none-elf-nm -u minirubik.elf
../rv32emu/build/rv32emu minirubik.elf
```

The rv32emu relative path above assumes these files are placed at `HW2/assignment2/` and its sibling is `HW2/rv32emu/`; otherwise adjust.
Do **not** link Playground's existing `main.o` simultaneously (duplicate `main`).
If the link reports missing `__udivsi3`, `__umodsi3` or `__mulsi3`, implement and test those helpers in RV32I. Do not silently link libgcc, or enable RV32M.
Run `riscv-none-elf-objdump -d minirubik.elf` to inspect ISA usage; verify actual mode includes CONFIG_SYSTEM and CONFIG_ELF_LOADER.
The original program's full 3,674,160-rank enumeration must be retained as a host-only test. Assignment 2 also requires micro-test and target reference vectors; these are NOT yet in this first smoke-test port.
