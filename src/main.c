#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include "config.h"
#include "rng.h"
#include "safety.h"

#define ERROR_INVALID_DECK (-1)
#define ERROR_INVALID_INPUT (-2)
#define ERROR_IMPOSSIBLE_VALUE_REACHED (-3)

#define DECK_SIZE 52
#define MAX_ACTIVE_CARDS 9
#define NUM_SUITS 4
#define UNMATCHABLE_CARD 10
#define POSSIBLE_SCORE_COUNT ((((DECK_SIZE)-NUM_SUITS)/2) + 1)    //2D array for holding each thread's score, plus their sum at the last row. (Deck size - number of tens) / 2 gives the number of possible matches and therefore scores, + 1 for no match (a score of 0)
uint64_t result_array[NUM_THREADS + 1][POSSIBLE_SCORE_COUNT] = {{0}}; 

static int fisher_yates_shuffle(int arr[], size_t thread_id) {
    if(!require_valid_ptr(arr))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_in_bounds(thread_id, NUM_THREADS))
    {
      return ERROR_INVALID_INPUT;
    }

    for (uint32_t i = DECK_SIZE - 1; i > 0; i--) {
        uint32_t j = random_range(0, i, thread_id); 

        int temp = arr[j];
        arr[j] = arr[i];
        arr[i] = temp;
    }

    return 0;
}

static int assert_deck_is_valid(const int arr[])
{ 
  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }

  int val_array[(DECK_SIZE/NUM_SUITS) + 1] = {0};
  for(int i = 0; i < DECK_SIZE; ++i)
  {
    val_array[arr[i]]++;
  }
  
  if(!c_assert(val_array[0] == 0))
  {
    return ERROR_INVALID_DECK;
  }
  for(int i = 1; i < ((DECK_SIZE / NUM_SUITS) + 1); ++i) //one entry for each card value + 1 for keeping the score
  {
    if(!c_assert(val_array[i] == 4))
    {
      return ERROR_INVALID_DECK;
    }
  }
  return 0;
}

static int score_array(int arr[])
{
  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }

  int assert_deck_is_valid_ret_code = assert_deck_is_valid(arr);
  if(assert_deck_is_valid_ret_code != 0)
  {
    return assert_deck_is_valid_ret_code;
  }

  int count = 0;
  int val_array[(DECK_SIZE/NUM_SUITS) + 1] = {0};


  for(int i = 0; i < DECK_SIZE; i++)
  {
    int num = arr[i];
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
    if (!c_assert(val_array[num] <= NUM_SUITS) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }
    if(!c_assert(val_array[num] >= 0) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }
    if (!c_assert(count >= 0) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }
    if (!c_assert(count <= MAX_ACTIVE_CARDS + 1) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    if(count == MAX_ACTIVE_CARDS + 1)
    {
      if (!c_assert((val_array[0] <= (POSSIBLE_SCORE_COUNT)) == true))
      {
        return ERROR_IMPOSSIBLE_VALUE_REACHED;
      }
      if(!c_assert(val_array[0] >= 0) == true)
      {
        return ERROR_IMPOSSIBLE_VALUE_REACHED;
      }
      return val_array[0];
    }
  }
  return val_array[0];
}

struct ThreadArgs{
  int id;
  uint32_t num_sim;
};

// cppcheck-suppress [constParameterCallback]
void* worker(void* arg)
{
  const struct ThreadArgs* args = (const struct ThreadArgs*)arg;
  int thread_id = args->id;
  uint64_t num_runs = args->num_sim;

  int deck[DECK_SIZE];
  for(int j = 0; j < 4; j++)
  {
    for(int i = 0; i < DECK_SIZE/4; i++)
    {
      deck[j * 13 + i] = i + 1;
    }
  }

  for(uint64_t i = 0; i < num_runs; i++)
  {
    int fisher_yates_ret = fisher_yates_shuffle(deck, thread_id);
    if(fisher_yates_ret < 0)
    {
      (void)fprintf(stderr, "Thread no %d aborting... Fisher Yates error: %d\n", thread_id, fisher_yates_ret);
      return (void*)(intptr_t) fisher_yates_ret;
    }
    int score = score_array(deck);
    if(score < 0)
    {
      (void)fprintf(stderr, "Thread no %d aborting... Scoring error: %d\n", thread_id, score);
      return (void*)(intptr_t) score;
    }
    result_array[thread_id][score]++;
  }

  return (void*)(intptr_t)0;
}

struct ValidWorkerOutputRegistry
{
  unsigned int bitmask : NUM_THREADS;
};

int main(void)
{
  if(!c_assert(NUM_THREADS <= 64) == true)
  {
    return 1;
  }
  initialize_rng();
  
  pthread_t threads[NUM_THREADS];

  struct ThreadArgs args[NUM_THREADS];

  int modulo_num = NUM_SIMS % NUM_THREADS;

  for(int i = 0; i < NUM_THREADS; i++)
  {
    args[i].id = i;
    args[i].num_sim = NUM_SIMS/NUM_THREADS + (modulo_num > 0 ? 1 : 0);
    modulo_num--;
    int return_code = pthread_create(&threads[i], NULL, worker, &args[i]); 
    if(return_code != 0)
    {
      (void)fprintf(stderr, "Thread no %d creation failed with return code: %d\n", i, return_code);
      return 1;
    }
  }
  struct ValidWorkerOutputRegistry reg = {0};
  for(int i = 0; i < NUM_THREADS; i++)
  {
    void* thread_ret;
    int join_ret_code = pthread_join(threads[i], &thread_ret);
    if(join_ret_code != 0)
    {
      (void)fprintf(stderr, "Thread no %d join call failed with return code: %d\n", i, join_ret_code);
      return 1;
    }

    intptr_t worker_ret = (intptr_t)thread_ret;
    if(worker_ret < 0)
    {
      (void)fprintf(stderr, "Thread no %d reported error: %d. Its data will not be evaluated.\n", i, (int)worker_ret);
      reg.bitmask = reg.bitmask | (1ULL << i);
    }
  }

  for(int i = 0; i < NUM_THREADS; i++)
  {
    if((reg.bitmask & (1ULL << i)))
    {
      continue;
    }
    for(int j = 0; j < 25; j++)
    {
      result_array[NUM_THREADS][j] += result_array[i][j];
    }
  }

  uint64_t count = 0;
  for(int j = 0; j < 25; j++)
  {
    (void)fprintf(stdout, "%ld ", result_array[NUM_THREADS][j]);
    count += result_array[NUM_THREADS][j];
  }

  (void)fprintf(stdout, "\nTotal sim ran: %lu\n", count);
  return 0;
}

