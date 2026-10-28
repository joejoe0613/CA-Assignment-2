#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Source-level search counters. Enabled by verify.c only.
 * COUNT/MEASURE do not change the search result. MEASURE evaluates its
 * expression once and preserves short-circuit evaluation at call sites.
 * These counts are not CPU/RV32I instructions or hardware memory accesses.
 */
#ifdef SEARCH_STATS
typedef struct {
    uint64_t goal_tests;
    uint64_t div3_evaluations;
    uint64_t mod3_evaluations;
    uint64_t move_lut_reads;
    uint64_t heuristic_calls;
} search_stats_t; 
static search_stats_t search_stats;
#define COUNT(field) ((void) ++search_stats.field)
#else
#define COUNT(field) ((void) 0)
#endif

#define MEASURE(field, expression) (COUNT(field), (expression))

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

/*@ predicate valid_state(state_t *state) =
      (\forall integer i; 0 <= i < CUBIES ==>
         state->p[i] < CUBIES && state->o[i] < 3) &&
      (\forall integer i, j; 0 <= i < j < CUBIES ==>
         state->p[i] != state->p[j]) &&
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
 */

static const char *const move_names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                              "B'", "D",  "D2", "D'"};
static const uint8_t inverse_move[MOVES] = {2, 1, 0, 5, 4, 3, 8, 7, 6};
/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

/* The three quarter-turns preserve the fixed front-upper-left corner. */
/*@ requires face < 3;
    assigns \nothing;
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.p[i] == state.p[source[face][i]];
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.o[i] == (state.o[source[face][i]] + twist[face][i]) % 3;
 */
static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant \forall integer j; 0 <= j < i ==>
          result.p[j] == state.p[source[face][j]];
        loop invariant \forall integer j; 0 <= j < i ==>
          result.o[j] == (state.o[source[face][j]] + twist[face][j]) % 3;
        loop assigns i, result.p[0..6], result.o[0..6];
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t) ((state.o[from] + twist[face][i]) % 3U);
    }
    
    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t turns = (uint8_t) (move % 3U + 1U);
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, (uint8_t) (move / 3U));
    return state;
}

/*@ requires \valid_read(state);
    requires \forall integer i; 0 <= i < CUBIES ==>
      0 <= state->p[i] < CUBIES;
    requires \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    requires \forall integer i; 0 <= i < CUBIES ==>
      0 <= state->o[i] < 3;
    assigns \nothing;
    ensures \result < STATES;
 */
static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant (i == 0 ==> p == 0) && (i == 1 ==> p <= 6) &&
          (i == 2 ==> p <= 41) && (i == 3 ==> p <= 209) &&
          (i == 4 ==> p <= 839) && (i == 5 ==> p <= 2519) &&
          (i >= 6 ==> p <= 5039);
        loop assigns i, p;
        loop variant CUBIES - i;
     */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        /*@ loop invariant i + 1 <= j <= CUBIES;
            loop invariant smaller <= j - i - 1;
            loop assigns j, smaller;
            loop variant CUBIES - j;
         */
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        p = p * (CUBIES - i) + smaller;
    }
    /*@ loop invariant 0 <= i <= 6;
        loop invariant (i == 0 ==> o == 0) && (i == 1 ==> o < 3) &&
          (i == 2 ==> o < 9) && (i == 3 ==> o < 27) &&
          (i == 4 ==> o < 81) && (i == 5 ==> o < 243) &&
          (i == 6 ==> o < 729);
        loop assigns i, o;
        loop variant 6 - i;
     */
    for (uint8_t i = 0; i < 6; ++i)
        o = o * 3U + state->o[i];
    return p * ORIENTATIONS + o;
}

/*@ requires \valid(state); requires rank < STATES; assigns *state; */
static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS, o = rank % ORIENTATIONS, f = 720;
    uint8_t sum = 0;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;
        state->p[i] = available[q];

        for (uint8_t j = q; j + 1U < CUBIES - i; ++j)
            available[j] = available[j + 1U];
        if (i < 5)
            f /= 6U - i;
    }
    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }
    
    state->o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
}

/*@ requires \valid_read(state);
    requires \initialized(&state->p[0..6]) && \initialized(&state->o[0..6]);
    assigns \nothing;
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] < CUBIES && state->o[i] < 3;
    ensures \result != 0 ==> \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    ensures \result != 0 ==>
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
    ensures complete: valid_state(state) ==> \result != 0;
 */
static int valid(const state_t *state)
{
    uint8_t sum = 0;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant sum <= 2 * i;
        loop invariant sum == (i > 0 ? state->o[0] : 0) +
          (i > 1 ? state->o[1] : 0) + (i > 2 ? state->o[2] : 0) +
          (i > 3 ? state->o[3] : 0) + (i > 4 ? state->o[4] : 0) +
          (i > 5 ? state->o[5] : 0) + (i > 6 ? state->o[6] : 0);
        loop invariant \forall integer j; 0 <= j < i ==>
          state->p[j] < CUBIES && state->o[j] < 3;
        loop invariant \forall integer j, k; 0 <= j < k < i ==>
          state->p[j] != state->p[k];
        loop assigns i, sum;
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;
        /*@ loop invariant 0 <= j <= i;
            loop invariant \forall integer k; 0 <= k < j ==>
              state->p[k] != state->p[i];
            loop assigns j;
            loop variant i - j;
        */
        for (uint8_t j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;

        sum = (uint8_t) (sum + state->o[i]);
    }

    return sum % 3U == 0;
}

/* Quarter-turn transitions are unchanged mathematically; now persistent. */
static uint16_t permutation[3][PERMUTATIONS];
static uint16_t orientation[3][ORIENTATIONS];
static uint8_t permutation_distance[PERMUTATIONS];
static uint8_t orientation_distance[ORIENTATIONS];
enum { MAX_SOLUTION = 11 };

/* BFS by level scans: no queue, only the small abstract distance array.
 * A move is 1, 2 or 3 quarter turns; EACH costs one HTM step. */
static int build_distances(uint8_t *distance, unsigned count,
                           const uint16_t transition[3][count])
{
    memset(distance, UINT8_MAX, count);
    distance[0] = 0;
    unsigned found = 1;

    for (unsigned depth = 0; found < count; ++depth) {
        unsigned before = found;

        for (unsigned here = 0; here < count; ++here) {
            if (distance[here] != depth)
                continue;

            for (unsigned face = 0; face < 3; ++face) {
                unsigned next = here;

                for (unsigned turn = 0; turn < 3; ++turn) {
                    next = transition[face][next];

                    if (distance[next] == UINT8_MAX) {
                        distance[next] = (uint8_t) (depth + 1);
                        ++found;
                    }
                }
            }
        }

        if (found == before)
            return 0;
    }

    return 1;
}

static int build_tables(void)
{
    state_t state;

    for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state((uint32_t) rank * ORIENTATIONS, &state);

        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            permutation[face][rank] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }
    for (uint16_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);

        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            orientation[face][rank] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
    return build_distances(permutation_distance, PERMUTATIONS,
                           (const uint16_t (*)[PERMUTATIONS]) permutation) &&
           build_distances(orientation_distance, ORIENTATIONS,
                           (const uint16_t (*)[ORIENTATIONS]) orientation);
}

static unsigned lower_bound(uint16_t p, uint16_t o)
{
    unsigned a = permutation_distance[p], b = orientation_distance[o];
    return a > b ? a : b;
}

static const uint8_t move_face[MOVES] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t move_turn[MOVES] = {0, 1, 2, 0, 1, 2, 0, 1, 2};
static const uint8_t next_face_start[MOVES] = {3, 3, 3, 6, 6, 6, 9, 9, 9};

/* Explicit DFS frame. next_p/next_o reuse successive quarter turns on
 * the same face: R -> R2 -> R', then reset to p/o for the next face. */
typedef struct {
    uint16_t p, o, next_p, next_o;
    uint8_t next_move;
    uint8_t last_face;
} frame_t;

/* Nonrecursive IDA*. Returns optimal length, or -1 on model/search failure.
 * path[d] is the move FROM frame d TO frame d+1. No global visited table. */
static int solve_ida(uint16_t p, uint16_t o, uint8_t path[MAX_SOLUTION])
{
    COUNT(goal_tests);
    if (p == 0 && o == 0)
        return 0;

    frame_t stack[MAX_SOLUTION + 1];
    unsigned bound = MEASURE(heuristic_calls, lower_bound(p, o));

    while (bound <= MAX_SOLUTION) {
        unsigned next_bound = MAX_SOLUTION + 1;
        unsigned depth = 0;
        stack[0] = (frame_t) {p, o, p, o, 0, 3};

        for (;;) {
            frame_t *frame = &stack[depth];

            if (frame->next_move == MOVES /* || depth == bound */) {
                if (depth == 0)
                    break;
                --depth;
                continue;
            }
            
            uint8_t move = frame->next_move++;
            uint8_t face = MEASURE(move_lut_reads, move_face[move]);

            /* Two consecutive moves of one face combine into <=1 move.
             * Thus no shortest path contains such a pair. */
            if (face == frame->last_face) {
                frame->next_move = MEASURE(move_lut_reads, next_face_start[move]);
                continue;
            }

            if (MEASURE(move_lut_reads, move_turn[move]) == 0) {
                frame->next_p = frame->p;
                frame->next_o = frame->o;
            }
            
            frame->next_p = permutation[face][frame->next_p];
            frame->next_o = orientation[face][frame->next_o];
            unsigned h = MEASURE(heuristic_calls,
                lower_bound(frame->next_p, frame->next_o));
            COUNT(goal_tests);
            if (h == 0) {
                path[depth] = move;
                return (int) (depth + 1);
            }

            unsigned f = depth + 1 + h;

            if (f > bound) {
                if (f < next_bound)
                    next_bound = f;
                continue;
            }

            path[depth] = move;
            uint16_t np = frame->next_p, no = frame->next_o;
            stack[++depth] = (frame_t) {np, no, np, no, 0, face};
        }

        bound = next_bound;
    }

    return -1;
}

/*@ requires valid_read_string(input);
    requires \valid(state);
    assigns state->p[0..6], state->o[0..6];
    ensures \result != 0 ==> input[14] == '\0';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] < CUBIES && state->o[i] < 3;
    ensures \result != 0 ==> \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    ensures \result != 0 ==>
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] == input[i] - '1';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->o[i] == input[i + CUBIES] - '1';
 */
static int parse_state(const char *input, state_t *state)
{
    /*@ loop invariant 0 <= i <= 14;
        loop invariant i <= strlen(input);
        loop invariant i <= 7 ==> \initialized(&state->p[0..i-1]);
        loop invariant i >= 7 ==> \initialized(&state->p[0..6]);
        loop invariant i >= 7 ==> \initialized(&state->o[0..i-8]);
        loop invariant \forall integer j; 0 <= j < i && j < CUBIES ==>
          state->p[j] == input[j] - '1';
        loop invariant \forall integer j; 0 <= j < i - CUBIES ==>
          state->o[j] == input[j + CUBIES] - '1';
        loop assigns i, state->p[0..6], state->o[0..6];
        loop variant 14 - i;
     */
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        (i < 7 ? state->p : state->o)[i % 7] = (uint8_t) (input[i] - '1');
    }

    return input[14] == '\0' && valid(state);
}

/* stdout is fully buffered off a terminal, so a write error surfaces at the
 * flush, not at the printf that queued the bytes. Every exit path that has
 * produced output goes through here.
 */
static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

static int self_test(void)
{
    const state_t solved = {{0, 1, 2, 3, 4, 5, 6}, {0}};
    state_t state;

    for (uint8_t move = 0; move < MOVES; ++move) {
        state = solved;
        state = apply_move(state, move);
        state = apply_move(state, inverse_move[move]);
        if (memcmp(&solved, &state, sizeof solved))
            return 0;
    }

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        unrank_state(rank, &state);
        if (!valid(&state) || rank_state(&state) != rank)
            return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    state_t state;
    
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (!self_test() || !build_tables()) {
            fputs("self-test failed\n", stderr);
            return 1;
        }
        puts("3674160 rank round trips; abstract tables complete (not full BFS)");
        return output_failed();
    }
    
    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }

    if (!build_tables()) {
        fputs("could not build abstract distance tables\n", stderr);
        return 1;
    }

    uint32_t rank = rank_state(&state);
    uint8_t path[MAX_SOLUTION];

    int length = solve_ida((uint16_t) (rank / ORIENTATIONS),
                           (uint16_t) (rank % ORIENTATIONS), path);

    if (length < 0) {
        fputs("no solution within 11 moves\n", stderr);
        return 1;
    }

    for (int i = 0; i < length; ++i)
        printf("%s%s", i ? " " : "", move_names[path[i]]);
    putchar('\n');

    return output_failed();
}