#ifndef BAREMETAL_PRINTF_H
#define BAREMETAL_PRINTF_H

#include <stddef.h>

/* Print a formatted string through RISC-V write(64). */
int baremetal_printf(const char *format, ...);

/* Print a raw buffer without format conversion. */
int baremetal_write(const char *buf, size_t len);

#endif