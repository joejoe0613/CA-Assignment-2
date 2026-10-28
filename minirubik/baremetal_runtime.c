#include <stdint.h>
#include <stddef.h>

/* Memory initialization */
void *memset(void *dest, int value, size_t n)
{
    unsigned char *p = dest;
    for (size_t i = 0; i < n; ++i)
        p[i] = (unsigned char)value;
    return dest;
}

/* Memory copy */
void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];

    return dest;
}

/* RV32I software multiplication */
uint32_t __mulsi3(uint32_t a, uint32_t b)
{
    uint32_t result = 0;

    while (b != 0) {
        if (b & 1u)
            result += a;

        a <<= 1;
        b >>= 1;
    }

    return result;
}

/* Unsigned division and remainder */
static uint32_t divmod_u32(uint32_t n, uint32_t d,
                           uint32_t *remainder)
{
    uint32_t q = 0;
    uint32_t r = 0;

    if (d == 0) {
        if (remainder)
            *remainder = 0;
        return 0; /* Temporary policy for zero divisor */
    }

    for (int i = 31; i >= 0; --i) {
        uint32_t carry = r >> 31;
        r = (r << 1) | ((n >> i) & 1u);

        if (carry || r >= d) {
            r -= d;
            q |= (uint32_t)1u << i;
        }
    }

    if (remainder)
        *remainder = r;

    return q;
}

uint32_t __udivsi3(uint32_t a, uint32_t b)
{
    return divmod_u32(a, b, 0);
}

uint32_t __umodsi3(uint32_t a, uint32_t b)
{
    uint32_t r;
    (void)divmod_u32(a, b, &r);
    return r;
}

/* Absolute magnitude without signed overflow */
static uint32_t magnitude_i32(int32_t x)
{
    uint32_t u = (uint32_t)x;
    return x < 0 ? 0u - u : u;
}

int32_t __modsi3(int32_t a, int32_t b)
{
    uint32_t r = __umodsi3(magnitude_i32(a),
                            magnitude_i32(b));

    return a < 0 ? -(int32_t)r : (int32_t)r;
}

int32_t __divsi3(int32_t a, int32_t b)
{
    uint32_t q = __udivsi3(magnitude_i32(a),
                            magnitude_i32(b));

    if ((a < 0) != (b < 0))
        return (int32_t)(0u - q);

    return (int32_t)q;
}