#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "rng.h"
#include "safety.h"


typedef enum {
  SYS_OK = 0,
  ERROR_INVALID_INPUT = 1,
  ERROR_INVALID_DECK = 2,
  ERROR_IMPOSSIBLE_VALUE_REACHED = 3,
  ERROR_COULD_NOT_CREATE_OUTPUT_FILE = 4,
  ERROR_COULD_NOT_CLOSE_OUTPUT_FILE = 5
} AppStatus;

#define DECK_SIZE 52
#define MAX_ACTIVE_CARDS 9
#define NUM_SUITS 4
#define UNMATCHABLE_CARD 10
#define NUM_POSSIBLE_SCORES ((((DECK_SIZE)-NUM_SUITS)/2) + 1)    //2D array for holding each thread's score, plus their sum at the last row. (Deck size - number of tens) / 2 gives the number of possible matches and therefore scores, + 1 for no match (a score of 0)

static AppStatus fisher_yates_shuffle(uint8_t arr[], size_t thread_id) {
    if(!require_valid_ptr(arr))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(thread_id < NUM_THREADS) == true)
    {
      return ERROR_INVALID_INPUT;
    }

    ThreadID s_thread_id;
    s_thread_id.id = thread_id;

    for (uint32_t i = DECK_SIZE - 1; i > 0; i--) {
        uint32_t second_index = random_range(0, i, s_thread_id); 

        int temp = arr[second_index];
        arr[second_index] = arr[i];
        arr[i] = temp;
    }

    return SYS_OK;
}

static AppStatus assert_deck_is_valid(const int arr[])
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

  return SYS_OK;
}

static AppStatus score_num_less_than_unmatchable_card(int val_array[], int num, int* out_delta_count)
{
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num >= 0 && num <= DECK_SIZE / NUM_SUITS) == true)
    { 
      return ERROR_INVALID_INPUT;
    }

    if(val_array[UNMATCHABLE_CARD - num] > 0)
    {
      val_array[UNMATCHABLE_CARD - num]--;
      val_array[0]++;
      *out_delta_count = -1; 
    }
    else
    {
      val_array[num]++;
      *out_delta_count = 1; 
    }

    return SYS_OK;
}

static AppStatus score_num_greater_than_unmatchable_card(int val_array[], int num, int* out_delta_count)
{
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num >= 0 && num <= DECK_SIZE / NUM_SUITS) == true)
    { 
      return ERROR_INVALID_INPUT;
    }

    if(val_array[num] > 0)
    {
      val_array[num]--;
      val_array[0]++;
      *out_delta_count = -1;
    }
    else
    {
      val_array[num]++;
      *out_delta_count = 1;
    }

    return SYS_OK;
}

static AppStatus score_num_equal_to_unmatchable_card(int val_array[], int num, int* out_delta_count)
{
  
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num >= 0 && num <= DECK_SIZE / NUM_SUITS) == true)
    { 
      return ERROR_INVALID_INPUT;
    }

    val_array[num]++;
    *out_delta_count = 1;

    return SYS_OK;
}


static AppStatus score_num(int val_array[], int num, int* out_delta_count)
{ 
    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }
    
    if(!c_assert(num >= 0 && num <= DECK_SIZE / NUM_SUITS) == true)
    { 
      return ERROR_INVALID_INPUT;
    }

    int delta_count = 0;
    AppStatus ret_code_score_func;
    if(num < UNMATCHABLE_CARD)
    {
      ret_code_score_func = score_num_less_than_unmatchable_card(val_array, num, &delta_count); 
    }
    else if(num > UNMATCHABLE_CARD)
    {
      ret_code_score_func = score_num_greater_than_unmatchable_card(val_array, num, &delta_count);
    }
    else
    {
      ret_code_score_func = score_num_equal_to_unmatchable_card(val_array, num, &delta_count);
    }

    if(ret_code_score_func != SYS_OK)
    {
      return ret_code_score_func;
    }
    
    if(!c_assert(delta_count == 1 || delta_count == -1) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    *out_delta_count = delta_count;
    return SYS_OK;
}

typedef struct
{
  int value;
} ScoreResult;

static AppStatus score_array(int arr[], ScoreResult* out_result)
{
  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }

  AppStatus assert_deck_is_valid_ret_code = assert_deck_is_valid(arr);
  if(assert_deck_is_valid_ret_code != SYS_OK)
  {
    return assert_deck_is_valid_ret_code;
  }

  int count = 0;
  int val_array[(DECK_SIZE/NUM_SUITS) + 1] = {0};

  for(int i = 0; i < DECK_SIZE; i++)
  {
    int num = arr[i];
    int delta_count = 0;

    AppStatus ret_score_num = score_num(val_array, num, &delta_count);
    if(ret_score_num != SYS_OK)
    {
      return ret_score_num;
    }

    if (!c_assert(val_array[num] >= 0 && val_array[num] <= NUM_SUITS) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    if (!c_assert(count >= 0 && count <= MAX_ACTIVE_CARDS + 1) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    count += delta_count;

    if(count == MAX_ACTIVE_CARDS + 1)
    {
      if (!c_assert(val_array[0] >= 0 && (val_array[0] <= (NUM_POSSIBLE_SCORES))) == true)
      {
        return ERROR_IMPOSSIBLE_VALUE_REACHED;
      }
      out_result->value = val_array[0];
      return SYS_OK;
    }
  }

  out_result->value = val_array[0];
  return SYS_OK;
}

struct ThreadData{
  size_t id;
  uint64_t* thread_result_arr;
  uint32_t num_sim;
  //output
  AppStatus out_status;
};

// cppcheck-suppress [constParameterCallback]
void* worker(void* arg)
{
  if(arg == NULL)
  {
    pthread_exit(NULL);
  }

  struct ThreadData* args = (struct ThreadData*)arg;
  //TODO: maybe add type checking to args if possible

  if(!c_assert(args->id <= NUM_THREADS))
  {
    args->out_status = ERROR_INVALID_INPUT;
    pthread_exit(NULL);
  }
  if(!require_valid_ptr(args->thread_result_arr))
  {
    args->out_status = ERROR_INVALID_INPUT;
    pthread_exit(NULL);
  }

  size_t thread_id = args->id;
  uint64_t* local_result_array = args->thread_result_arr;

  uint64_t num_runs = args->num_sim;

  uint8_t deck[DECK_SIZE];
  for(int j = 0; j < NUM_SUITS; j++)
  {
    for(int i = 0; i < DECK_SIZE / NUM_SUITS; i++)
    {
      deck[(j * DECK_SIZE / NUM_SUITS) + i] = i + 1;
    }
  }

  ScoreResult res = {0};
  for(uint64_t i = 0; i < num_runs; i++)
  {
    AppStatus ret_code_fisher_yates = fisher_yates_shuffle(deck, thread_id);
    if(!c_assert(ret_code_fisher_yates == SYS_OK))
    {
      (void)fprintf(stderr, "Thread no %zu aborting... Fisher Yates error: %d\n", thread_id, ret_code_fisher_yates);
      args->out_status = ret_code_fisher_yates;
      pthread_exit(NULL);
    }
    AppStatus ret_code_score_array = score_array(deck, &res);
    if(!c_assert(ret_code_score_array == SYS_OK))
    {
      (void)fprintf(stderr, "Thread no %zu aborting... Scoring error: %d\n", thread_id, ret_code_score_array);
      args->out_status = ret_code_score_array;
      pthread_exit(NULL);
    }
    local_result_array[res.value]++;
  }

  args->out_status = SYS_OK;
  return NULL;
}

static AppStatus write_output_as_json(const uint64_t result_arr[])
{
  if(!require_valid_ptr(result_arr))
  {
    return ERROR_INVALID_INPUT;
  }
  
  FILE *fptr;
  fptr = fopen("output.json", "w");
  
  if(fptr == NULL)
  {
    return ERROR_COULD_NOT_CREATE_OUTPUT_FILE;
  }
  
  (void)fprintf(fptr, "{\n");
  for(int i = 0; i < NUM_POSSIBLE_SCORES; ++i) {
      (void)fprintf(fptr, "  \"%d\": %ld", i, result_arr[i]);
      if(i != NUM_POSSIBLE_SCORES - 1){
          (void)fprintf(fptr, ",");
      }
      (void)fprintf(fptr, "\n");
  }
  (void)fprintf(fptr, "}\n");
  int ret_val_fclose = fclose(fptr);
  if(ret_val_fclose != 0)
  {
    (void)fprintf(stderr, "fclose failed: error code is '%d'", errno);
    return ERROR_COULD_NOT_CLOSE_OUTPUT_FILE;
  }
  (void)fprintf(stdout, "results written to output.json");
  return SYS_OK;
}

int main(void)
{
  if(!c_assert(NUM_THREADS <= 64) == true)
  {
    return 1;
  }

  initialize_rng();
  pthread_t threads[NUM_THREADS];
  struct ThreadData thread_data[NUM_THREADS] = {0};
  uint64_t result_array[NUM_THREADS + 1][NUM_POSSIBLE_SCORES] = {{0}}; 

  int modulo_num = NUM_SIMS % NUM_THREADS;
  for(size_t i = 0; i < NUM_THREADS; i++)
  {
    thread_data[i].id = i;
    thread_data[i].num_sim = (NUM_SIMS/NUM_THREADS) + (modulo_num > 0 ? 1 : 0);
    thread_data[i].thread_result_arr = result_array[i];
    modulo_num--;
    int ret_code_pthread_create = pthread_create(&threads[i], NULL, worker, &thread_data[i]); 
    if(!c_assert(ret_code_pthread_create == 0) == true)
    {
      (void)fprintf(stderr, "Thread no %zu creation failed with return code: %d\n", i, ret_code_pthread_create);
      return 1;
    }
  }

  uint64_t thread_bitmask = 0;
  for(int i = 0; i < NUM_THREADS; i++)
  {
    int ret_code_join = pthread_join(threads[i], NULL);
    if(ret_code_join != 0)
    {
      (void)fprintf(stderr, "Thread no %d join call failed with return code: %d\n", i, ret_code_join);
      return 1;
    }
    
    AppStatus ret_code_worker_thread = thread_data[i].out_status;
    if(!c_assert(ret_code_worker_thread == SYS_OK) == true)
    {
      (void)fprintf(stderr, "Thread no %d reported error: %d. Its data will not be evaluated.\n", i, ret_code_worker_thread);
      thread_bitmask = thread_bitmask | (1ULL << i);
    }
  }

  uint64_t count = 0;
  for(int i = 0; i < NUM_THREADS; i++)
  {
    if((thread_bitmask & (1ULL << i))) {continue;}
    for(int j = 0; j < NUM_POSSIBLE_SCORES; j++)
    {
      result_array[NUM_THREADS][j] += result_array[i][j];
      count += result_array[i][j];
    }
  }
  AppStatus ret_code_write_output = write_output_as_json(result_array[NUM_THREADS]);
  if(!c_assert(ret_code_write_output == SYS_OK) == true)
  {
     (void)fprintf(stderr, "Error while writing results to output.json\n");
  }

  (void)fprintf(stdout, "\n ------------- SCORES ------------- \n");
  for(int i = 0; i < NUM_POSSIBLE_SCORES; ++i)
  {
    (void)fprintf(stdout, "%ld ", result_array[NUM_THREADS][i]);
  }
  (void)fprintf(stdout, "\nTotal sim ran: %lu\n", count);
  return 0;
}

