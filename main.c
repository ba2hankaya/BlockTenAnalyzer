#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#define NUM_THREADS 16
#define DECK_SIZE 52
#define MAX_ACTIVE_CARDS 9
#define NUM_SUITS 4
const uint32_t num_simulations = 1000;

uint64_t x = 12345; //main seed
static uint64_t s[NUM_THREADS * 4]; //for holding each thread's 4 seeds
uint64_t result_array[NUM_THREADS + 1][(DECK_SIZE - NUM_SUITS)/2 + 1] = {{0}}; //2D array for holding each thread's score, plus their sum at the last row. (Deck size - number of tens) / 2 gives the number of possible matches and therefore scores, + 1 for no match (a score of 0)

uint64_t splitmix_next() {
	uint64_t z = (x += UINT64_C(0x9E3779B97F4A7C15));
	z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
	return z ^ (z >> 31);
}

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

uint64_t next(int i) {
	const uint64_t result = rotl(s[0 + i * 4] + s[3 + i * 4], 23) + s[0 + i * 4];

	const uint64_t t = s[1 + i*4] << 17;

	s[2 + i * 4] ^= s[0 + i * 4];
	s[3 + i * 4] ^= s[1 + i * 4];
	s[1 + i * 4] ^= s[2 + i * 4];
	s[0 + i * 4] ^= s[3 + i * 4];

	s[2 + i * 4] ^= t;

	s[3 + i * 4] = rotl(s[3 + i * 4], 45);

	return result;
}

uint32_t random_range(uint32_t min, uint32_t max, int thread_id) {
    uint32_t range = max - min + 1;
    uint64_t random_32bit = next(thread_id) & 0xFFFFFFFF;

    uint64_t multi_result = random_32bit * range;
    uint32_t low_bits = (uint32_t)multi_result;

    if (low_bits < range) {
        uint32_t threshold = -range % range;
        while (low_bits < threshold) {
            random_32bit = next(thread_id) & 0xFFFFFFFF;
            multi_result = random_32bit * range;
            low_bits = (uint32_t)multi_result;
        }
    }

    return min + (multi_result >> 32);
}

void fisher_yates_shuffle(uint8_t arr[], int thread_id) {
    for (int i = DECK_SIZE - 1; i > 0; i--) {
        int j = random_range(0, i, thread_id); 

        uint8_t temp = arr[j];
        arr[j] = arr[i];
        arr[i] = temp;
    }
}

void assert_deck_is_valid(uint8_t arr[])
{
  int val_array[DECK_SIZE/NUM_SUITS + 1] = {0};
  for(int i = 0; i < DECK_SIZE; ++i)
  {
    val_array[arr[i]]++;
  }
  
  assert(val_array[0] == 0);
  for(int i = 1; i < 14; ++i)
  {
    assert(val_array[i] == 4);
  }
}

int score_array(uint8_t arr[])
{
  int score = 0;
  int count = 0;
  uint8_t val_array[DECK_SIZE/NUM_SUITS + 1] = {0};

  assert_deck_is_valid(arr);

  for(int i = 0; i < DECK_SIZE; i++)
  {
    int num = arr[i];
    assert(num >= 1);
    assert(num <= DECK_SIZE/NUM_SUITS);
    if(num < 10)
    {
      if(val_array[10 - num] > 0)
      {
        val_array[10 - num]--;
        val_array[0]++;
        count--;
      }
      else
      {
        val_array[num]++;
        count++;
      }
    }
    else if(num > 10)
    {
      if(val_array[num] > 0)
      {
        val_array[num]--;
        val_array[0]++;
        count--;
      }
      else
      {
        val_array[num]++;
        count++;
      }
    }
    else
    {
      val_array[num]++;
      count++;
    }
    assert(val_array[num] >= 0);
    assert(val_array[num] <= NUM_SUITS);
    assert(count >= 0);
    assert(count <= MAX_ACTIVE_CARDS + 1);

    if(count == MAX_ACTIVE_CARDS + 1)
    {
      assert(val_array[0] >= 0);
      assert(val_array[0] <= (DECK_SIZE - NUM_SUITS) / 2 + 1);
      return val_array[0];
    }
  }
  return val_array[0];
}

struct ThreadArgs{
  int id;
  uint32_t num_sim;
};

void* worker(void* arg)
{
  struct ThreadArgs* args = (struct ThreadArgs*)arg;
  int thread_id = args->id;
  int num_runs = args->num_sim;
  uint8_t deck[DECK_SIZE];
  for(int j = 0; j < 4; j++)
  {
    for(int i = 0; i < DECK_SIZE/4; i++)
    {
      deck[j*13 + i] = i + 1; 
    }
  }
  for(int i = 0; i < num_runs; i++)
  {
    fisher_yates_shuffle(deck, thread_id);
    uint8_t score = score_array(deck);
    result_array[thread_id][score]++;
  }
  return NULL;
}

void initialize_xoshiro_seeds()
{
  for(int i = 0; i < NUM_THREADS * 4; i++)
  {
    s[i] = splitmix_next();
  }
}

void init()
{
  initialize_xoshiro_seeds();
}


int main()
{
  init();
  pthread_t threads[NUM_THREADS];
  struct ThreadArgs args[NUM_THREADS];
  int modulo_num = num_simulations % NUM_THREADS;
  for(int i = 0; i < NUM_THREADS; i++)
  {
    args[i].id = i;
    args[i].num_sim = num_simulations/NUM_THREADS + (modulo_num > 0 ? 1 : 0);
    modulo_num--;
    int return_code = pthread_create(&threads[i], NULL, worker, &args[i]); 
    if(return_code != 0)
    {
      fprintf(stderr, "Thread no %d creation failed with return code: %d\n", i, return_code);
      return 1;
    }
  }
  for(int i = 0; i < NUM_THREADS; i++)
  {
    int join_ret_code = pthread_join(threads[i], NULL);
    if(join_ret_code != 0)
    {
      fprintf(stderr, "Thread no %d join call failed with return code: %d\n", i, join_ret_code);
      return 1;
    }
  }
  for(int i = 0; i < NUM_THREADS; i++)
  {
    for(int j = 0; j < 25; j++)
    {
      result_array[NUM_THREADS][j] += result_array[i][j];
    }
  }
  uint64_t count = 0;
  for(int j = 0; j < 25; j++)
  {
    fprintf(stdout, "%ld ", result_array[NUM_THREADS][j]);
    count += result_array[NUM_THREADS][j];
  }
  fprintf(stdout, "\nTotal sim ran: %ld", count);
  return 0;
}

