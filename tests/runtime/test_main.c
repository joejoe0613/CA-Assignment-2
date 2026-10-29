#include "../microtest/utest.h"
#include "../../runtime/baremetal_printf.h"

#include <stdint.h>

static int test_printer(void)
{
    baremetal_printf("Testing printer: %d\n", 123);

    baremetal_printf(
        "64-bit unsigned: %llu\n",
        18446744073709551615ULL
    );

    baremetal_printf(
        "64-bit signed: %lld\n",
        -9223372036854775807LL - 1
    );

    ASSERT_EQ(10, 5 + 5);
    ASSERT_UNSIGNED_EQ(100u, 50u + 50u);

    return TEST_PASS;
}

static int test_failing_assertion(void)
{
    ASSERT_EQ(10, 9);
    return TEST_PASS;
}

void register_runtime_tests(void);
void register_minirubik_tests(void);

int main(void)
{
    /* Redirect micro-test output to our printer */
    utest_printer = baremetal_printf;

    register_test(test_printer, 0, 0,
                  "printer_smoke");
    
    register_runtime_tests();
    register_minirubik_tests();

#ifdef TEST_EXPECTED_FAILURE
    register_test(test_failing_assertion, 0, 0,
                  "expected_failure");
#else
    (void)test_failing_assertion;
#endif

    return run_tests();
}