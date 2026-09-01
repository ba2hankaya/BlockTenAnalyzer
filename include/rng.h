#ifndef RNG_H
#define RNG_H

#include <stdint.h>

void initialize_rng(void);

typedef struct{
  size_t id;
} ThreadID;

uint32_t random_range(uint32_t min, uint32_t max, ThreadID thread_id);

#endif
