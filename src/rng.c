#include "config.h"
#include "rng.h"
#include <pthread.h>

//NOLINTBEGIN(readability-magic-numbers, readability-identifier-length)
static uint64_t x = MAIN_SEED;
static uint64_t s[NUM_THREADS * 4]; //for holding each thread's 4 seeds


static uint64_t splitmix_next(void) {
        uint64_t z = (x += UINT64_C(0x9E3779B97F4A7C15));
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        return z ^ (z >> 31);
}

void initialize_rng(void)
{
  for(int i = 0; i < NUM_THREADS * 4; i++)
  {
    s[i] = splitmix_next();
  }
}

static inline uint64_t rotl(const uint64_t seed, int k) {
        return (seed << k) | (seed >> (64 - k));
}

static uint64_t next(size_t thread_id) {
        const uint64_t result = rotl(s[0 + (thread_id * 4)] + s[3 + (thread_id * 4)], 23) + s[0 + (thread_id * 4)];

        const uint64_t t = s[1 + (thread_id*4)] << 17;

        s[2 + (thread_id * 4)] ^= s[0 + (thread_id * 4)];
        s[3 + (thread_id * 4)] ^= s[1 + (thread_id * 4)];
        s[1 + (thread_id * 4)] ^= s[2 + (thread_id * 4)];
        s[0 + (thread_id * 4)] ^= s[3 + (thread_id * 4)];

        s[2 + (thread_id * 4)] ^= t;

        s[3 + (thread_id * 4)] = rotl(s[3 + (thread_id * 4)], 45);

        return result;
}

uint32_t random_range(uint32_t min, uint32_t max, ThreadID thread_id) {
    uint32_t range = max - min + 1;
    uint64_t random_32bit = next(thread_id.id) & 0xFFFFFFFF;

    uint64_t multi_result = random_32bit * range;
    uint32_t low_bits = (uint32_t)multi_result;

    if (low_bits < range) {
        uint32_t threshold = (0 - range) % range;
        while (low_bits < threshold) {
            random_32bit = next(thread_id.id) & 0xFFFFFFFF;
            multi_result = random_32bit * range;
            low_bits = (uint32_t)multi_result;
        }
    }

    return min + (uint32_t)(multi_result >> 32);
}
//NOLINTEND(readability-magic-numbers, readability-identifier-length)
