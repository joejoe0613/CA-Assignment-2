#include "utest.h"
#include <stddef.h>
#include <string.h>


#ifdef NO_STD

int __printstub(const char *format, ...) { return 0; };
int (*utest_printer)(const char *, ...) = __printstub;

test_t tests[UTEST_MAX_TESTS];
static size_t test_count = 0;

#else

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int (*utest_printer)(const char *, ...) = printf;
test_t *head = NULL;

#endif

static int tname_width = 0;

void register_test(int_fn_void fn, void_fn_void setup, void_fn_void teardown,
                   const char *name) {
    size_t nlen;
    test_t test;

#ifdef NO_STD
    if (test_count == UTEST_MAX_TESTS) {
        utest_printer("Too many tests (max tests = %d)!\n", UTEST_MAX_TESTS);
        return;
    }
#else
    test_t *test_buf, *curr;

    if ((test_buf = malloc(sizeof(test_t))) == NULL) {
        perror("");
        exit(EXIT_FAILURE);
    }
#endif
    test.fn = fn;
    test.setup = setup;
    test.teardown = teardown;

    strncpy(test.name, name, UTEST_NAME_SIZE);

    nlen = strlen(test.name);

#ifdef NO_STD
    tests[test_count++] = test;
#else
    test.next = NULL;
    *test_buf = test;

    if (head == NULL) {
        head = test_buf;
    } else {
        curr = head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = test_buf;
    }
#endif

    if (nlen + 2 > tname_width) {
        tname_width = nlen + 2;
    }
}

int run_test(test_t *test)
{
    int status = TEST_PASS;

    if (test->setup)
        test->setup();

    if (test->fn())
        status = TEST_FAIL;

    if (test->teardown)
        test->teardown();

    return status;
}

int run_tests(void) {
    int status, failed = 0, passed = 0;
    test_t test;
    size_t j, nspace, nlen;

#ifdef NO_STD
    size_t i;

    for (i = 0; i < test_count; i++) {
        test = tests[i];
#else
    test_t *curr = head, *tmp;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    while (curr != NULL) {
        test = *curr;
#endif
        utest_printer(CYAN "%s" RESET, test.name);
        nlen = strlen(test.name);
        nspace = (nlen >= tname_width) ? 1 : tname_width - nlen;
        for (j = 0; j < nspace; j++) {
            utest_printer(" ");
        }
        utest_printer("... ");

        if ((status = run_test(&test)) == 1) {
            failed++;
            utest_printer(RED "failed" RESET "\n");
        } else if (status == TEST_ERROR) {
            return TEST_ERROR;
        } else {
            passed++;
            utest_printer(GREEN "ok" RESET "\n");
        }

#ifndef NO_STD
        tmp = curr->next;
        free(curr);
        curr = tmp;
#endif
    }

    utest_printer("Summary: %d passed, %d failed.\n", passed, failed);

    return failed != 0;
}
