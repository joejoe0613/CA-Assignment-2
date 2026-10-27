/*
 * Bare-metal RV32I playground: ChaCha20 in assembly and software BFloat16.
 *
 * The program runs with no operating system, no C library, and no libgcc.
 * It talks to the emulator through two newlib-style system calls (ecall with
 * the call number in a7): write (64) for output and exit (93), the latter
 * issued by start.S. Each test is timed with the unprivileged cycle and
 * instret counters (perfcounter.S).
 *
 * RV32I has neither multiply nor divide, so this file supplies __mulsi3
 * (which GCC calls for '*'), udiv/umod, and memcpy itself. The cost of those
 * helpers shows up in the instruction counts.
 *
 * Known limitations of the BFloat16 routines (kept as an exercise):
 *   * results are truncated, not rounded to nearest even;
 *   * addition that overflows returns NaN instead of infinity;
 *   * a subnormal product is wrong, often zero: the hidden bit is masked off
 *     before the shift into the subnormal range;
 *   * division by an infinite divisor returns zero even for a NaN dividend.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Write length bytes at ptr to stdout (fd 1) with the write system call.
 *
 * The "memory" clobber is required: the ecall reads the buffer, but the
 * compiler only sees register operands. Without it, GCC may delay or drop
 * the stores that fill the buffer, and the program prints garbage at -O1 and
 * above.
 */
#define printstr(ptr, length)                    \
    do {                                         \
        asm volatile(                            \
            "add a7, x0, 0x40;" /* write */      \
            "add a0, x0, 0x1;"  /* stdout */     \
            "add a1, x0, %0;"   /* buffer */     \
            "mv a2, %1;"        /* length */     \
            "ecall;"                             \
            :                                    \
            : "r"(ptr), "r"(length)              \
            : "a0", "a1", "a2", "a7", "memory"); \
    } while (0)

#define TEST_OUTPUT(msg, length) printstr(msg, length)

/* Print a string literal, passing a stack copy of it to printstr. */
#define TEST_LOGGER(msg)                     \
    {                                        \
        char _msg[] = msg;                   \
        TEST_OUTPUT(_msg, sizeof(_msg) - 1); \
    }

/* 64-bit counter reads, implemented in perfcounter.S. */
extern uint64_t get_cycles(void);
extern uint64_t get_instret(void);

/* ============= Freestanding runtime helpers ============= */

/* memcpy: GCC emits calls to it for aggregate initialization and copies,
 * even with no C library. A byte loop is enough here. */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dest;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dest;
}

/* Unsigned division by restoring shift-and-subtract, one quotient bit per
 * iteration, 32 iterations. Division by zero returns 0 instead of trapping. */
static unsigned long udiv(unsigned long dividend, unsigned long divisor)
{
    if (divisor == 0)
        return 0;

    unsigned long quotient = 0;
    unsigned long remainder = 0;

    for (int i = 31; i >= 0; i--) {
        remainder <<= 1;
        remainder |= (dividend >> i) & 1;

        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1UL << i);
        }
    }

    return quotient;
}

/* Unsigned remainder, same algorithm as udiv. */
static unsigned long umod(unsigned long dividend, unsigned long divisor)
{
    if (divisor == 0)
        return 0;

    unsigned long remainder = 0;

    for (int i = 31; i >= 0; i--) {
        remainder <<= 1;
        remainder |= (dividend >> i) & 1;

        if (remainder >= divisor) {
            remainder -= divisor;
        }
    }

    return remainder;
}

/* Unsigned multiplication by shift-and-add; the low 32 bits of a * b. */
static uint32_t umul(uint32_t a, uint32_t b)
{
    uint32_t result = 0;
    while (b) {
        if (b & 1)
            result += a;
        a <<= 1;
        b >>= 1;
    }
    return result;
}

/* GCC lowers '*' on RV32I (no M extension) to a call to __mulsi3, which
 * normally comes from libgcc. */
uint32_t __mulsi3(uint32_t a, uint32_t b)
{
    return umul(a, b);
}

/* Print val in lowercase hexadecimal followed by a newline. */
static void print_hex(unsigned long val)
{
    char buf[20];
    char *p = buf + sizeof(buf) - 1;
    *p = '\n';
    p--;

    if (val == 0) {
        *p = '0';
        p--;
    } else {
        while (val > 0) {
            int digit = val & 0xf;
            *p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            p--;
            val >>= 4;
        }
    }

    p++;
    printstr(p, (buf + sizeof(buf) - p));
}

/* Print val in decimal followed by a newline. Division by 10 goes through
 * udiv/umod, whose 32-iteration loops dominate the cost of each digit. */
static void print_dec(unsigned long val)
{
    char buf[20];
    char *p = buf + sizeof(buf) - 1;
    *p = '\n';
    p--;

    if (val == 0) {
        *p = '0';
        p--;
    } else {
        while (val > 0) {
            *p = '0' + umod(val, 10);
            p--;
            val = udiv(val, 10);
        }
    }

    p++;
    printstr(p, (buf + sizeof(buf) - p));
}

/* ============= BFloat16 Implementation ============= */

/*
 * BFloat16 layout: 1 sign bit, 8 exponent bits (bias 127, as in IEEE 754
 * binary32), 7 fraction bits. It is the upper half of a binary32 value, so
 * the range matches float with fewer significant bits. Exponent 0xFF encodes
 * infinity (fraction 0) and NaN (fraction non-zero); exponent 0 encodes zero
 * and subnormals.
 *
 * The routines work on the 16-bit encoding with integer operations only.
 */
typedef struct {
    uint16_t bits;
} bf16_t;

#define BF16_EXP_BIAS 127
#define BF16_SIGN_MASK 0x8000U
#define BF16_EXP_MASK 0x7F80U
#define BF16_MANT_MASK 0x007FU

#define BF16_NAN() ((bf16_t) {.bits = 0x7FC0})
#define BF16_ZERO() ((bf16_t) {.bits = 0x0000})

static inline bool bf16_isnan(bf16_t a)
{
    return ((a.bits & BF16_EXP_MASK) == BF16_EXP_MASK) &&
           (a.bits & BF16_MANT_MASK);
}

static inline bool bf16_isinf(bf16_t a)
{
    return ((a.bits & BF16_EXP_MASK) == BF16_EXP_MASK) &&
           !(a.bits & BF16_MANT_MASK);
}

static inline bool bf16_iszero(bf16_t a)
{
    return !(a.bits & 0x7FFF);
}

/* Count leading zeros by binary search on the shift amount (5 steps), since
 * RV32I has no clz instruction. */
static inline unsigned clz(uint32_t x)
{
    int n = 32, c = 16;
    do {
        uint32_t y = x >> c;
        if (y) {
            n -= c;
            x = y;
        }
        c >>= 1;
    } while (c);
    return n - x;
}

/*
 * a + b. Shift the significand of the operand with the larger exponent left
 * by the exponent difference, so both align to the smaller exponent without
 * losing bits; add or subtract the 8-bit significands (hidden bit restored),
 * then renormalize. Truncates; if the exponents differ by more than 8 the
 * larger operand is returned unchanged.
 */
static inline bf16_t bf16_add(bf16_t a, bf16_t b)
{
    uint16_t sign_a = a.bits >> 15 & 0x1, sign_b = b.bits >> 15 & 1;
    int16_t exp_a = a.bits >> 7 & 0xFF, exp_b = b.bits >> 7 & 0xFF;
    uint16_t mant_a = a.bits & 0x7F, mant_b = b.bits & 0x7F;

    /* Infinity and NaN */
    if (exp_a == 0xFF) {
        if (mant_a)
            return a;
        if (exp_b == 0xFF)
            return (mant_b || sign_a == sign_b) ? b : BF16_NAN();
        return a;
    }

    /* if a is normal/denormal, but b is infinity/NaN */
    if (exp_b == 0xFF)
        return b;

    /* if a == 0, b == 0 */
    if (!exp_a && !mant_a)
        return b;
    if (!exp_b && !mant_b)
        return a;

    /* if a, b is normal */
    if (exp_a)
        mant_a |= 0x80;
    if (exp_b)
        mant_b |= 0x80;

    int16_t exp_diff = exp_a - exp_b;
    uint16_t result_sign;
    int16_t result_exp;
    uint32_t result_mant;

    /* deal with result of exp */
    if (exp_diff > 0) {
        result_exp = exp_b;
        if (exp_diff > 8)
            return a;
        mant_a <<= exp_diff;
    } else if (exp_diff < 0) {
        result_exp = exp_a;
        if (exp_diff < -8)
            return b;
        mant_b <<= -exp_diff;
    } else
        result_exp = exp_a;

    if (sign_a == sign_b) {
        result_sign = sign_a;
        result_mant = (uint32_t) mant_a + mant_b;
        uint32_t lz = clz(result_mant);
        for (unsigned i = 0; i < 32 - lz - 8; i++) {
            result_mant >>= 1;
            if (++result_exp >= 255)
                return BF16_NAN();
        }
    } else {
        if (mant_a >= mant_b) {
            result_sign = sign_a;
            result_mant = mant_a - mant_b;
        } else {
            result_sign = sign_b;
            result_mant = mant_b - mant_a;
        }
        if (!result_mant)
            return BF16_ZERO();
        if (result_mant < 0x80) {
            while (!(result_mant & 0x80)) {
                result_mant <<= 1;
                if (--result_exp <= 0)
                    return BF16_ZERO();
            }
        } else {
            uint32_t lz = clz(result_mant);
            for (unsigned i = 0; i < 32 - lz - 8; i++) {
                result_mant >>= 1;
                if (++result_exp >= 255)
                    return BF16_NAN();
            }
        }
    }
    return (bf16_t) {
        .bits =
            result_sign << 15 | (result_exp & 0xFF) << 7 | (result_mant & 0x7F),
    };
}

/* a - b: flip the sign of b and add. */
static inline bf16_t bf16_sub(bf16_t a, bf16_t b)
{
    b.bits ^= 0x8000U;
    return bf16_add(a, b);
}

/*
 * a * b. Multiply the 8-bit significands (through __mulsi3), add the
 * exponents and remove one bias, then renormalize by at most one bit.
 * Subnormal inputs are normalized first. A product below the normal range
 * loses its hidden bit in the final mask, so it comes out wrong, often zero.
 */
static inline bf16_t bf16_mul(bf16_t a, bf16_t b)
{
    uint16_t sign_a = (a.bits >> 15) & 1;
    uint16_t sign_b = (b.bits >> 15) & 1;
    int16_t exp_a = ((a.bits >> 7) & 0xFF);
    int16_t exp_b = ((b.bits >> 7) & 0xFF);
    uint16_t mant_a = a.bits & 0x7F;
    uint16_t mant_b = b.bits & 0x7F;

    uint16_t result_sign = sign_a ^ sign_b;

    if (exp_a == 0xFF) {
        if (mant_a)
            return a;
        if (!exp_b && !mant_b)
            return BF16_NAN();
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    }
    if (exp_b == 0xFF) {
        if (mant_b)
            return b;
        if (!exp_a && !mant_a)
            return BF16_NAN();
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    }
    if ((!exp_a && !mant_a) || (!exp_b && !mant_b))
        return (bf16_t) {.bits = result_sign << 15};

    int16_t exp_adjust = 0;
    if (!exp_a) {
        while (!(mant_a & 0x80)) {
            mant_a <<= 1;
            exp_adjust--;
        }
        exp_a = 1;
    } else
        mant_a |= 0x80;
    if (!exp_b) {
        while (!(mant_b & 0x80)) {
            mant_b <<= 1;
            exp_adjust--;
        }
        exp_b = 1;
    } else
        mant_b |= 0x80;

    uint32_t result_mant = (uint32_t) mant_a * mant_b;
    int32_t result_exp = (int32_t) exp_a + exp_b - BF16_EXP_BIAS + exp_adjust;

    if (result_mant & 0x8000) {
        result_mant = (result_mant >> 8) & 0x7F;
        result_exp++;
    } else
        result_mant = (result_mant >> 7) & 0x7F;

    if (result_exp >= 0xFF)
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    if (result_exp <= 0) {
        if (result_exp < -6)
            return (bf16_t) {.bits = result_sign << 15};
        result_mant >>= (1 - result_exp);
        result_exp = 0;
    }

    return (bf16_t) {.bits = (result_sign << 15) | ((result_exp & 0xFF) << 7) |
                             (result_mant & 0x7F)};
}

/*
 * a / b. Restoring division of the significands, one quotient bit per
 * iteration for 16 iterations, then renormalize; the exponent is
 * exp_a - exp_b + bias. A result that underflows becomes signed zero.
 */
static inline bf16_t bf16_div(bf16_t a, bf16_t b)
{
    uint16_t sign_a = (a.bits >> 15) & 1;
    uint16_t sign_b = (b.bits >> 15) & 1;
    int16_t exp_a = ((a.bits >> 7) & 0xFF);
    int16_t exp_b = ((b.bits >> 7) & 0xFF);
    uint16_t mant_a = a.bits & 0x7F;
    uint16_t mant_b = b.bits & 0x7F;

    uint16_t result_sign = sign_a ^ sign_b;

    if (exp_b == 0xFF) {
        if (mant_b)
            return b;
        /* Inf/Inf = NaN */
        if (exp_a == 0xFF && !mant_a)
            return BF16_NAN();
        return (bf16_t) {.bits = result_sign << 15};
    }
    if (!exp_b && !mant_b) {
        if (!exp_a && !mant_a)
            return BF16_NAN();
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    }
    if (exp_a == 0xFF) {
        if (mant_a)
            return a;
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    }
    if (!exp_a && !mant_a)
        return (bf16_t) {.bits = result_sign << 15};

    if (exp_a)
        mant_a |= 0x80;
    if (exp_b)
        mant_b |= 0x80;

    uint32_t dividend = (uint32_t) mant_a << 15;
    uint32_t divisor = mant_b;
    uint32_t quotient = 0;

    for (int i = 0; i < 16; i++) {
        quotient <<= 1;
        if (dividend >= (divisor << (15 - i))) {
            dividend -= (divisor << (15 - i));
            quotient |= 1;
        }
    }

    int32_t result_exp = (int32_t) exp_a - exp_b + BF16_EXP_BIAS;

    if (!exp_a)
        result_exp--;
    if (!exp_b)
        result_exp++;

    if (quotient & 0x8000)
        quotient >>= 8;
    else {
        while (!(quotient & 0x8000) && result_exp > 1) {
            quotient <<= 1;
            result_exp--;
        }
        quotient >>= 8;
    }
    quotient &= 0x7F;

    if (result_exp >= 0xFF)
        return (bf16_t) {.bits = (result_sign << 15) | 0x7F80};
    if (result_exp <= 0)
        return (bf16_t) {.bits = result_sign << 15};
    return (bf16_t) {.bits = (result_sign << 15) | ((result_exp & 0xFF) << 7) |
                             (quotient & 0x7F)};
}

/* ============= ChaCha20 Declaration ============= */

/* Implemented in chacha20_asm.S. */
extern void chacha20(uint8_t *out,
                     const uint8_t *in,
                     size_t inlen,
                     const uint8_t *key,
                     const uint8_t *nonce,
                     uint32_t ctr);

/* ============= Test Suite ============= */

/* Each test prints a header, runs one case, and prints PASSED or FAILED.
 * main() wraps every test in the counter reads that produce the Cycles and
 * Instructions lines, so the figures include the test's own printing. */

static void test_chacha20(void)
{
    /* Test vector from RFC 7539 section 2.4.2 */
    const uint8_t key[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                             11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                             22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    const uint8_t nonce[12] = {0, 0, 0, 0, 0, 0, 0, 74, 0, 0, 0, 0};
    const uint32_t ctr = 1;

    /* 114-byte plaintext: one full 64-byte block plus a 50-byte tail. */
    /* 114 characters with no terminating NUL, hence nonstring. */
    uint8_t in[114] __attribute__((nonstring)) =
        "Ladies and Gentlemen of the class of '99: If I could offer you only "
        "one tip for the future, sunscreen would be it.";
    uint8_t out[114];

    /* Expected ciphertext from RFC 7539 section 2.4.2. */
    static const uint8_t exp[] = {
        0x6e, 0x2e, 0x35, 0x9a, 0x25, 0x68, 0xf9, 0x80, 0x41, 0xba, 0x07, 0x28,
        0xdd, 0x0d, 0x69, 0x81, 0xe9, 0x7e, 0x7a, 0xec, 0x1d, 0x43, 0x60, 0xc2,
        0x0a, 0x27, 0xaf, 0xcc, 0xfd, 0x9f, 0xae, 0x0b, 0xf9, 0x1b, 0x65, 0xc5,
        0x52, 0x47, 0x33, 0xab, 0x8f, 0x59, 0x3d, 0xab, 0xcd, 0x62, 0xb3, 0x57,
        0x16, 0x39, 0xd6, 0x24, 0xe6, 0x51, 0x52, 0xab, 0x8f, 0x53, 0x0c, 0x35,
        0x9f, 0x08, 0x61, 0xd8, 0x07, 0xca, 0x0d, 0xbf, 0x50, 0x0d, 0x6a, 0x61,
        0x56, 0xa3, 0x8e, 0x08, 0x8a, 0x22, 0xb6, 0x5e, 0x52, 0xbc, 0x51, 0x4d,
        0x16, 0xcc, 0xf8, 0x06, 0x81, 0x8c, 0xe9, 0x1a, 0xb7, 0x79, 0x37, 0x36,
        0x5a, 0xf9, 0x0b, 0xbf, 0x74, 0xa3, 0x5b, 0xe6, 0xb4, 0x0b, 0x8e, 0xed,
        0xf2, 0x78, 0x5e, 0x42, 0x87, 0x4d};

    TEST_LOGGER("Test: ChaCha20\n");

    /* Encrypt. */
    chacha20(out, in, sizeof(in), key, nonce, ctr);

    /* Compare with the expected ciphertext. */
    bool passed = true;
    for (size_t i = 0; i < sizeof(exp); i++) {
        if (out[i] != exp[i]) {
            passed = false;
            break;
        }
    }

    if (passed) {
        TEST_LOGGER("  ChaCha20 RFC 7539: PASSED\n");
    } else {
        TEST_LOGGER("  ChaCha20 RFC 7539: FAILED\n");
    }
}

static void test_bf16_add(void)
{
    TEST_LOGGER("Test: bf16_add\n");

    /* 1.0 + 1.0 = 2.0 */
    bf16_t a = {.bits = 0x3F80}; /* 1.0 */
    bf16_t b = {.bits = 0x3F80}; /* 1.0 */
    bf16_t result = bf16_add(a, b);
    TEST_LOGGER("  1.0 + 1.0 = ");
    print_hex(result.bits);

    /* Expected: 0x4000 (2.0) */
    if (result.bits == 0x4000) {
        TEST_LOGGER("  PASSED\n");
    } else {
        TEST_LOGGER("  FAILED (expected 0x4000)\n");
    }
}

static void test_bf16_sub(void)
{
    TEST_LOGGER("Test: bf16_sub\n");

    /* 3.0 - 2.0 = 1.0 */
    bf16_t a = {.bits = 0x4040}; /* 3.0 */
    bf16_t b = {.bits = 0x4000}; /* 2.0 */
    bf16_t result = bf16_sub(a, b);
    TEST_LOGGER("  3.0 - 2.0 = ");
    print_hex(result.bits);

    /* Expected: 0x3F80 (1.0) */
    if (result.bits == 0x3F80) {
        TEST_LOGGER("  PASSED\n");
    } else {
        TEST_LOGGER("  FAILED (expected 0x3F80)\n");
    }
}

static void test_bf16_mul(void)
{
    TEST_LOGGER("Test: bf16_mul\n");

    /* 2.0 * 3.0 = 6.0 */
    bf16_t a = {.bits = 0x4000}; /* 2.0 */
    bf16_t b = {.bits = 0x4040}; /* 3.0 */
    bf16_t result = bf16_mul(a, b);
    TEST_LOGGER("  2.0 * 3.0 = ");
    print_hex(result.bits);

    /* Expected: 0x40C0 (6.0) */
    if (result.bits == 0x40C0) {
        TEST_LOGGER("  PASSED\n");
    } else {
        TEST_LOGGER("  FAILED (expected 0x40C0)\n");
    }
}

static void test_bf16_div(void)
{
    TEST_LOGGER("Test: bf16_div\n");

    /* 6.0 / 2.0 = 3.0 */
    bf16_t a = {.bits = 0x40C0}; /* 6.0 */
    bf16_t b = {.bits = 0x4000}; /* 2.0 */
    bf16_t result = bf16_div(a, b);
    TEST_LOGGER("  6.0 / 2.0 = ");
    print_hex(result.bits);

    /* Expected: 0x4040 (3.0) */
    if (result.bits == 0x4040) {
        TEST_LOGGER("  PASSED\n");
    } else {
        TEST_LOGGER("  FAILED (expected 0x4040)\n");
    }
}

static void test_bf16_special_cases(void)
{
    TEST_LOGGER("Test: bf16_special_cases\n");

    /* Test zero */
    bf16_t zero = BF16_ZERO();
    TEST_LOGGER("  bf16_iszero(0): ");
    if (bf16_iszero(zero)) {
        TEST_LOGGER("PASSED\n");
    } else {
        TEST_LOGGER("FAILED\n");
    }

    /* Test NaN */
    bf16_t nan = BF16_NAN();
    TEST_LOGGER("  bf16_isnan(NaN): ");
    if (bf16_isnan(nan)) {
        TEST_LOGGER("PASSED\n");
    } else {
        TEST_LOGGER("FAILED\n");
    }

    /* Test infinity */
    bf16_t inf = {.bits = 0x7F80};
    TEST_LOGGER("  bf16_isinf(Inf): ");
    if (bf16_isinf(inf)) {
        TEST_LOGGER("PASSED\n");
    } else {
        TEST_LOGGER("FAILED\n");
    }
}

/*
 * Run call, then print the cycle and instruction counts it took. Counters
 * are read immediately before and after the call. The deltas include the
 * test's own printing and the few instructions of the counter reads.
 */
#define MEASURE(call)                                  \
    do {                                               \
        start_cycles = get_cycles();                   \
        start_instret = get_instret();                 \
        call;                                          \
        end_cycles = get_cycles();                     \
        end_instret = get_instret();                   \
        cycles_elapsed = end_cycles - start_cycles;    \
        instret_elapsed = end_instret - start_instret; \
                                                       \
        TEST_LOGGER("  Cycles: ");                     \
        print_dec((unsigned long) cycles_elapsed);     \
        TEST_LOGGER("  Instructions: ");               \
        print_dec((unsigned long) instret_elapsed);    \
    } while (0)

int main(void)
{
    uint64_t start_cycles, end_cycles, cycles_elapsed;
    uint64_t start_instret, end_instret, instret_elapsed;

    TEST_LOGGER("\n=== ChaCha20 Tests ===\n\n");

    TEST_LOGGER("Test 0: ChaCha20 (RISC-V Assembly)\n");
    MEASURE(test_chacha20());
    TEST_LOGGER("\n");

    TEST_LOGGER("\n=== BFloat16 Tests ===\n\n");

    TEST_LOGGER("Test 1: bf16_add\n");
    MEASURE(test_bf16_add());
    TEST_LOGGER("\n");

    TEST_LOGGER("Test 2: bf16_sub\n");
    MEASURE(test_bf16_sub());
    TEST_LOGGER("\n");

    TEST_LOGGER("Test 3: bf16_mul\n");
    MEASURE(test_bf16_mul());
    TEST_LOGGER("\n");

    TEST_LOGGER("Test 4: bf16_div\n");
    MEASURE(test_bf16_div());
    TEST_LOGGER("\n");

    TEST_LOGGER("Test 5: bf16_special_cases\n");
    MEASURE(test_bf16_special_cases());

    TEST_LOGGER("\n=== All Tests Completed ===\n");

    return 0;
}
