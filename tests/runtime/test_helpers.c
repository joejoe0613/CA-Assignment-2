#include "../microtest/utest.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include "reference_vectors.h"

/* Software arithmetic helpers */
extern uint32_t __mulsi3(uint32_t a, uint32_t b);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);
extern uint32_t __umodsi3(uint32_t a, uint32_t b);
extern int32_t __divsi3(int32_t a, int32_t b);
extern int32_t __modsi3(int32_t a, int32_t b);

static int test_multiply(void)
{
    ASSERT_UNSIGNED_EQ(0u, __mulsi3(0u, 123u));
    ASSERT_UNSIGNED_EQ(123u, __mulsi3(1u, 123u));
    ASSERT_UNSIGNED_EQ(42u, __mulsi3(6u, 7u));
    ASSERT_UNSIGNED_EQ(1000000u, __mulsi3(1000u, 1000u));

    /* Only low 32 bits are returned */
    ASSERT_UNSIGNED_EQ(0xfffffffeu,
                       __mulsi3(0xffffffffu, 2u));

    return TEST_PASS;
}

static int test_unsigned_division(void)
{
    ASSERT_UNSIGNED_EQ(3u, __udivsi3(10u, 3u));
    ASSERT_UNSIGNED_EQ(5u, __udivsi3(25u, 5u));
    ASSERT_UNSIGNED_EQ(0u, __udivsi3(0u, 7u));
    ASSERT_UNSIGNED_EQ(0u, __udivsi3(5u, 10u));

    ASSERT_UNSIGNED_EQ(1431655765u,
                       __udivsi3(UINT32_MAX, 3u));

    return TEST_PASS;
}

static int test_unsigned_modulo(void)
{
    ASSERT_UNSIGNED_EQ(1u, __umodsi3(10u, 3u));
    ASSERT_UNSIGNED_EQ(0u, __umodsi3(25u, 5u));
    ASSERT_UNSIGNED_EQ(5u, __umodsi3(5u, 10u));
    ASSERT_UNSIGNED_EQ(0u, __umodsi3(0u, 7u));
    ASSERT_UNSIGNED_EQ(3u,
                       __umodsi3(UINT32_MAX, 7u));

    return TEST_PASS;
}

static int test_signed_divmod(void)
{
    ASSERT_EQ(-3, __divsi3(-10, 3));
    ASSERT_EQ(-3, __divsi3(10, -3));
    ASSERT_EQ(3, __divsi3(-10, -3));
    ASSERT_EQ(3, __divsi3(10, 3));

    ASSERT_EQ(-1, __modsi3(-10, 3));
    ASSERT_EQ(1, __modsi3(10, -3));
    ASSERT_EQ(-1, __modsi3(-10, -3));

    ASSERT_EQ(-1073741824,
              __divsi3(INT32_MIN, 2));

    return TEST_PASS;
}

static int test_memory_functions(void)
{
    unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char dst[8] = {0};

    memset(dst, 0xAA, sizeof(dst));
    for (unsigned i = 0; i < 8; ++i)
        ASSERT_UNSIGNED_EQ(0xAAu, dst[i]);

    memcpy(dst, src, sizeof(src));
    for (unsigned i = 0; i < 8; ++i)
        ASSERT_UNSIGNED_EQ(src[i], dst[i]);

    /* Zero-length copy must leave destination unchanged */
    dst[0] = 99;
    memcpy(dst, src, 0);
    ASSERT_UNSIGNED_EQ(99u, dst[0]);

    return TEST_PASS;
}

static int test_string_functions(void)
{
    char dst[8];

    ASSERT_UNSIGNED_EQ(0u, strlen(""));
    ASSERT_UNSIGNED_EQ(5u, strlen("hello"));

    memset(dst, 'X', sizeof(dst));
    strncpy(dst, "abc", sizeof(dst));

    ASSERT_EQ('a', dst[0]);
    ASSERT_EQ('b', dst[1]);
    ASSERT_EQ('c', dst[2]);
    ASSERT_EQ('\0', dst[3]);
    ASSERT_EQ('\0', dst[7]);

    memset(dst, 'X', sizeof(dst));
    strncpy(dst, "abcdefghij", 4);

    ASSERT_EQ('a', dst[0]);
    ASSERT_EQ('d', dst[3]);
    ASSERT_EQ('X', dst[4]);

    return TEST_PASS;
}

static int test_integer_boundaries(void)
{
    /* Multiplication: low 32-bit results */
    ASSERT_UNSIGNED_EQ(0u, __mulsi3(0u, UINT32_MAX));
    ASSERT_UNSIGNED_EQ(UINT32_MAX, __mulsi3(UINT32_MAX, 1u));
    ASSERT_UNSIGNED_EQ(0u, __mulsi3(0x80000000u, 2u));
    ASSERT_UNSIGNED_EQ(1u, __mulsi3(UINT32_MAX, UINT32_MAX));

    /* Unsigned division */
    ASSERT_UNSIGNED_EQ(UINT32_MAX,
                       __udivsi3(UINT32_MAX, 1u));
    ASSERT_UNSIGNED_EQ(1u,
                       __udivsi3(UINT32_MAX, UINT32_MAX));
    ASSERT_UNSIGNED_EQ(0u,
                       __udivsi3(0u, UINT32_MAX));

    /* Unsigned remainder */
    ASSERT_UNSIGNED_EQ(0u,
                       __umodsi3(UINT32_MAX, 1u));
    ASSERT_UNSIGNED_EQ(0u,
                       __umodsi3(UINT32_MAX, UINT32_MAX));
    ASSERT_UNSIGNED_EQ(1u,
                       __umodsi3(UINT32_MAX, 2u));

    /* Signed division */
    ASSERT_EQ(INT32_MIN, __divsi3(INT32_MIN, 1));
    ASSERT_EQ(INT32_MAX, __divsi3(INT32_MAX, 1));
    ASSERT_EQ(-1, __divsi3(INT32_MIN, INT32_MAX));

    /* Signed remainder */
    ASSERT_EQ(0, __modsi3(INT32_MIN, 1));
    ASSERT_EQ(-2, __modsi3(INT32_MIN, 3));
    ASSERT_EQ(0, __modsi3(INT32_MAX, INT32_MAX));

    return TEST_PASS;
}

static int test_memory_boundaries(void)
{
    unsigned char src[65];
    unsigned char dst[65];

    for (unsigned i = 0; i < 65; ++i) {
        src[i] = (unsigned char)(i + 1);
        dst[i] = 0xAA;
    }

    /* Length 0: destination must not change */
    memset(dst, 0, 0);
    memcpy(dst, src, 0);
    ASSERT_UNSIGNED_EQ(0xAAu, dst[0]);

    /* Length 1: first byte changes */
    memset(dst, 0x55, 1);
    ASSERT_UNSIGNED_EQ(0x55u, dst[0]);
    ASSERT_UNSIGNED_EQ(0xAAu, dst[1]);

    /* Length 64: preserve the guard byte */
    memset(dst, 0, 64);
    for (unsigned i = 0; i < 64; ++i)
        ASSERT_UNSIGNED_EQ(0u, dst[i]);

    ASSERT_UNSIGNED_EQ(0xAAu, dst[64]);

    /* Restore destination and test copying */
    memcpy(dst, src, 64);
    for (unsigned i = 0; i < 64; ++i)
        ASSERT_UNSIGNED_EQ(src[i], dst[i]);

    ASSERT_UNSIGNED_EQ(0xAAu, dst[64]);

    /* Copy one byte */
    dst[0] = 0;
    memcpy(dst, src, 1);
    ASSERT_UNSIGNED_EQ(1u, dst[0]);

    /* strlen boundary */
    ASSERT_UNSIGNED_EQ(0u, strlen(""));
    ASSERT_UNSIGNED_EQ(1u, strlen("A"));
    ASSERT_UNSIGNED_EQ(6u, strlen("ABCDEF"));

    /* strncpy length 0 */
    char text[8] = {'X','X','X','X','X','X','X','X'};
    strncpy(text, "ABC", 0);
    ASSERT_EQ('X', text[0]);

    /* strncpy length 1 */
    strncpy(text, "ABC", 1);
    ASSERT_EQ('A', text[0]);
    ASSERT_EQ('X', text[1]);

    /* strncpy with zero padding */
    strncpy(text, "AB", 8);
    ASSERT_EQ('A', text[0]);
    ASSERT_EQ('B', text[1]);
    ASSERT_EQ('\0', text[2]);
    ASSERT_EQ('\0', text[7]);

    return TEST_PASS;
}

static int test_host_reference(void)
{
    const unsigned count =
        sizeof(ref) / sizeof(ref[0]);

    /* Function pointers prevent constant folding
       of direct helper calls. */
    uint32_t (*volatile mul_fn)(uint32_t, uint32_t)
        = __mulsi3;
    uint32_t (*volatile div_fn)(uint32_t, uint32_t)
        = __udivsi3;
    uint32_t (*volatile mod_fn)(uint32_t, uint32_t)
        = __umodsi3;

    for (unsigned i = 0; i < count; ++i) {
        uint32_t a = ref[i][0];
        uint32_t b = ref[i][1];

        ASSERT_UNSIGNED_EQ(ref[i][2], mul_fn(a, b));
        ASSERT_UNSIGNED_EQ(ref[i][3], div_fn(a, b));
        ASSERT_UNSIGNED_EQ(ref[i][4], mod_fn(a, b));
    }

    return TEST_PASS;
}

/* Called from test_main.c */
void register_runtime_tests(void)
{
    register_test(test_multiply, 0, 0, "multiply");
    register_test(test_unsigned_division, 0, 0, "unsigned_div");
    register_test(test_unsigned_modulo, 0, 0, "unsigned_mod");
    register_test(test_signed_divmod, 0, 0, "signed_divmod");
    register_test(test_memory_functions, 0, 0, "memory");
    register_test(test_string_functions, 0, 0, "string");
    register_test(test_integer_boundaries, 0, 0, "integer_boundaries");
    register_test(test_memory_boundaries, 0, 0, "memory_boundaries");
    register_test(test_host_reference, 0, 0, "host_reference");
}
