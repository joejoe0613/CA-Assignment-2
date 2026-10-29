#include "../microtest/utest.h"
#include "../../minirubik/solver_core.h"
#include "reference_cases.h"

#include <stdint.h>

typedef struct {
    const char *state;
    int optimal_length;
} cube_case_t;

static const cube_case_t cases[] = {
    {"12345671111111", 0},
    {"62345713133111", 8},
    {"24316572122213", 8},
    {"25713642221111", 8},
    {"24513763133333", 9},
    {"43752611332133", 9},
    {"25416373331111", 10},
    {"21345671111111", 11}
};

/* Path replay based on HW1 original cube model. */

typedef struct {
    uint8_t p[7];
    uint8_t o[7];
} replay_state_t;

static const uint8_t replay_src[3][7] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6}
};

static const uint8_t replay_twist[3][7] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0}
};

/* Apply one quarter-turn using the original model. */
static void replay_quarter(replay_state_t *state,
                           unsigned face)
{
    replay_state_t before = *state;

    for (unsigned i = 0; i < 7; ++i) {
        unsigned from = replay_src[face][i];

        state->p[i] = before.p[from];
        state->o[i] = (uint8_t)(
            (before.o[from] + replay_twist[face][i]) % 3u
        );
    }
}

/* Return 1 if the path restores the solved cube. */
static int replay_solution(const char *input,
                           const uint8_t *path,
                           unsigned length)
{
    replay_state_t state;

    if (!input || !path || length > 11)
        return 0;

    for (unsigned i = 0; i < 7; ++i) {
        state.p[i] = (uint8_t)(input[i] - '1');
        state.o[i] = (uint8_t)(input[i + 7] - '1');
    }

    for (unsigned i = 0; i < length; ++i) {
        unsigned move = path[i];

        if (move >= 9)
            return 0;

        unsigned face = move / 3u;
        unsigned turns = move % 3u + 1u;

        for (unsigned j = 0; j < turns; ++j)
            replay_quarter(&state, face);
    }

    /* A solved cube has identity permutation
       and zero orientation. */
    for (unsigned i = 0; i < 7; ++i) {
        if (state.p[i] != i || state.o[i] != 0)
            return 0;
    }

    return 1;
}

static int test_path_validation(void)
{
    uint8_t path[11];

    for (unsigned i = 0;
         i < sizeof(cases) / sizeof(cases[0]);
         ++i) {

        int length = minirubik_solve(
            cases[i].state, path
        );

        ASSERT_EQ(cases[i].optimal_length, length);
        ASSERT_TRUE(length >= 0 && length <= 11);

        ASSERT_TRUE(
            replay_solution(cases[i].state,
                            path,
                            (unsigned)length)
        );
    }

    return TEST_PASS;
}

static int test_solved_state(void)
{
    uint8_t path[11];

    ASSERT_EQ(0, minirubik_solve(
        "12345671111111", path));

    return TEST_PASS;
}

static int test_invalid_state(void)
{
    uint8_t path[11];

    ASSERT_EQ(-1, minirubik_solve(
        "11111111111111", path));

    ASSERT_EQ(-1, minirubik_solve(
        "12345671111112", path));

    ASSERT_EQ(-1, minirubik_solve(
        "1234567", path));

    return TEST_PASS;
}

static int test_move_inverse(void)
{
    ASSERT_TRUE(minirubik_smoke_test());
    return TEST_PASS;
}

static int test_hw1_cases(void)
{
    uint8_t path[11];

    for (unsigned i = 0;
         i < sizeof(cases) / sizeof(cases[0]); ++i) {

        int length = minirubik_solve(
            cases[i].state, path);

        ASSERT_EQ(cases[i].optimal_length, length);

        for (int j = 0; j < length; ++j)
            ASSERT_TRUE(path[j] < 9);
    }

    return TEST_PASS;
}

static int test_oracle_reference(void)
{
    uint8_t path[11];

    for (unsigned i = 0;
         i < REFERENCE_CASE_COUNT;
         ++i) {

        int length = minirubik_solve(
            reference_cases[i].state,
            path
        );

        /* Independent BFS shortest distance */
        ASSERT_EQ(
            reference_cases[i].optimal_length,
            length
        );

        /* Solver must return a valid length */
        ASSERT_TRUE(length >= 0 && length <= 11);

        /* Replay the resulting sequence */
        ASSERT_TRUE(replay_solution(
            reference_cases[i].state,
            path,
            (unsigned)length
        ));
    }

    return TEST_PASS;
}

void register_minirubik_tests(void)
{
    register_test(test_solved_state, 0, 0,
                "cube_solved");

    register_test(test_invalid_state, 0, 0,
                "cube_invalid");

    register_test(test_move_inverse, 0, 0,
                "cube_inverse");

    register_test(test_hw1_cases, 0, 0,
                "cube_hw1_cases");

    register_test(test_path_validation, 0, 0,
                "cube_path_validation");

    register_test(test_oracle_reference, 0, 0,
                "cube_oracle_reference");
}