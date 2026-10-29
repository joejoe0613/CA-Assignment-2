#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * Host-only original BFS oracle.
 * oracle.c already includes solver_original.c.
 */
#include "oracle.c"

int main(void)
{
    uint8_t *dist = oracle_distances();

    if (!dist) {
        fprintf(stderr, "Oracle BFS failed\n");
        return 1;
    }

    /* One solved state, four cases per other depth. */
    unsigned selected[12] = {0};
    const unsigned wanted[12] = {
        1, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4
    };

    printf("#ifndef MINIRUBIK_REFERENCE_CASES_H\n");
    printf("#define MINIRUBIK_REFERENCE_CASES_H\n\n");

    printf(
        "typedef struct {\n"
        "    const char *state;\n"
        "    int optimal_length;\n"
        "} reference_case_t;\n\n"
    );

    printf(
        "static const reference_case_t reference_cases[] = {\n"
    );

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint8_t depth = dist[rank];

        if (depth > 11 ||
            selected[depth] >= wanted[depth])
            continue;

        state_t state;
        unrank_state(rank, &state);

        char input[15];

        for (int i = 0; i < 7; ++i) {
            input[i] = (char)('1' + state.p[i]);
            input[i + 7] = (char)('1' + state.o[i]);
        }

        input[14] = '\0';

        printf("    {\"%s\", %u},\n",
               input, (unsigned)depth);

        ++selected[depth];
    }

    printf("};\n\n");
    printf(
        "#define REFERENCE_CASE_COUNT "
        "(sizeof(reference_cases) / sizeof(reference_cases[0]))\n\n"
    );
    printf("#endif\n");

    unsigned total = 0;

    for (unsigned d = 0; d < 12; ++d) {
        fprintf(stderr,
                "Depth %u: %u cases\n",
                d, selected[d]);

        if (selected[d] != wanted[d]) {
            free(dist);
            return 2;
        }

        total += selected[d];
    }

    fprintf(stderr,
            "Generated %u reference cases\n", total);

    free(dist);
    return 0;
}