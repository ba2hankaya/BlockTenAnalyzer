#ifndef RNG_H
#define RNG_H

#include <stdint.h>

void initialize_rng(void);

uint32_t random_range(uint32_t min, uint32_t max, int thread_id);

#endif
