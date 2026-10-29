/* Host-only oracle: original full BFS; never linked into the small solver. */
#define main original_main
#include "solver_original.c"
#undef main

uint8_t *oracle_distances(void)
{
    uint8_t diameter;
    uint8_t *moves = build_table(&diameter);
    
    if (!moves || diameter != 11) { free(moves); return NULL; }
    
    uint8_t *distance = malloc(STATES);
    
    if (!distance) { free(moves); return NULL; }
    
    memset(distance, UINT8_MAX, STATES);
    distance[0] = 0;
    
    for (uint32_t r = 1; r < STATES; ++r) {
        
        uint32_t chain[12], current = r;
        unsigned length = 0;
        
        while (distance[current] == UINT8_MAX) {
            if (length == 11 || moves[current] >= MOVES) {
                free(distance); free(moves); return NULL;
            }
            
            chain[length++] = current;
            state_t s;
            unrank_state(current, &s);
            s = apply_move(s, moves[current]);
            current = rank_state(&s);
        }
        
        unsigned d = distance[current];
        while (length) distance[chain[--length]] = (uint8_t) ++d;
    }
    
    free(moves);
    return distance;
}

/* Replay with the ORIGINAL model, not the new transition tables. */
int oracle_replay(uint32_t rank, const uint8_t *path, unsigned length)
{
    state_t s;
    unrank_state(rank, &s);
    
    for (unsigned i = 0; i < length; ++i) {
        if (path[i] >= MOVES) return 0;
        s = apply_move(s, path[i]);
    }
    
    return rank_state(&s) == 0;
}
