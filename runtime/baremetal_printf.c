
#include "baremetal_printf.h"

#include <stdint.h>
#include <stdarg.h>

/* RISC-V newlib-compatible write syscall */
int baremetal_write(const char *buf, size_t len)
{
    register long a0 asm("a0") = 1;
    register const char *a1 asm("a1") = buf;
    register size_t a2 asm("a2") = len;
    register long a7 asm("a7") = 64;

    asm volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a2), "r"(a7)
        : "memory"
    );

    return (int)a0;
}

typedef struct {
    char data[128];
    unsigned used;
    int count;
    int failed;
} output_t;

static void flush(output_t *o)
{
    if (o->failed || !o->used)
        return;

    unsigned sent = 0;

    while (sent < o->used) {
        int n = baremetal_write(
            o->data + sent, o->used - sent);

        if (n <= 0 || (unsigned)n > o->used - sent) {
            o->failed = 1;
            return;
        }

        sent += (unsigned)n;
    }

    o->used = 0;
}

static void putc_out(output_t *o, char c)
{
    if (o->failed)
        return;

    if (o->used == sizeof(o->data))
        flush(o);

    if (!o->failed) {
        o->data[o->used++] = c;
        ++o->count;
    }
}

static void puts_out(output_t *o, const char *s)
{
    if (!s)
        s = "(null)";

    while (*s)
        putc_out(o, *s++);
}

/*
 * Decimal conversion without 64-bit division.
 * Each decimal digit uses repeated subtraction.
 */
static void put_u64(output_t *o, uint64_t value)
{
    static const uint64_t powers[] = {
        UINT64_C(10000000000000000000),
        UINT64_C(1000000000000000000),
        UINT64_C(100000000000000000),
        UINT64_C(10000000000000000),
        UINT64_C(1000000000000000),
        UINT64_C(100000000000000),
        UINT64_C(10000000000000),
        UINT64_C(1000000000000),
        UINT64_C(100000000000),
        UINT64_C(10000000000),
        UINT64_C(1000000000),
        UINT64_C(100000000),
        UINT64_C(10000000),
        UINT64_C(1000000),
        UINT64_C(100000),
        UINT64_C(10000),
        UINT64_C(1000),
        UINT64_C(100),
        UINT64_C(10),
        UINT64_C(1)
    };

    int started = 0;

    for (unsigned i = 0;
         i < sizeof(powers) / sizeof(powers[0]);
         ++i) {

        unsigned digit = 0;

        while (value >= powers[i]) {
            value -= powers[i];
            ++digit;
        }

        if (digit || started ||
            i == sizeof(powers) / sizeof(powers[0]) - 1) {

            putc_out(o, (char)('0' + digit));
            started = 1;
        }
    }
}

static void put_i64(output_t *o, int64_t value)
{
    uint64_t magnitude = (uint64_t)value;

    if (value < 0) {
        putc_out(o, '-');
        magnitude = UINT64_C(0) - magnitude;
    }

    put_u64(o, magnitude);
}

static void put_hex(output_t *o, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    putc_out(o, '0');
    putc_out(o, 'x');

    int started = 0;

    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned digit = (value >> shift) & 15u;

        if (digit || started || shift == 0) {
            putc_out(o, digits[digit]);
            started = 1;
        }
    }
}

int baremetal_printf(const char *format, ...)
{
    output_t out = {{0}, 0, 0, 0};
    va_list args;

    va_start(args, format);

    while (*format) {
        if (*format != '%') {
            putc_out(&out, *format++);
            continue;
        }

        ++format;

        if (*format == '%') {
            putc_out(&out, '%');
            ++format;
            continue;
        }

        int long_count = 0;
        while (*format == 'l') {
            ++long_count;
            ++format;
        }

        char spec = *format;
        if (!spec)
            break;

        ++format;

        switch (spec) {
        case 's':
            puts_out(&out, va_arg(args, const char *));
            break;

        case 'c':
            putc_out(&out, (char)va_arg(args, int));
            break;

        case 'd':
        case 'i':
            if (long_count >= 2)
                put_i64(&out, va_arg(args, long long));
            else if (long_count == 1)
                put_i64(&out, va_arg(args, long));
            else
                put_i64(&out, va_arg(args, int));
            break;

        case 'u':
            if (long_count >= 2)
                put_u64(&out, va_arg(args, unsigned long long));
            else if (long_count == 1)
                put_u64(&out, va_arg(args, unsigned long));
            else
                put_u64(&out, va_arg(args, unsigned int));
            break;

        case 'p':
            put_hex(&out,
                (uint32_t)(uintptr_t)va_arg(args, void *));
            break;

        case 'x':
            put_hex(&out, va_arg(args, unsigned int));
            break;

        default:
            /* Unsupported specifier: show it literally. */
            putc_out(&out, '%');
            putc_out(&out, spec);
            break;
        }
    }

    va_end(args);
    flush(&out);

    return out.failed ? -1 : out.count;
}