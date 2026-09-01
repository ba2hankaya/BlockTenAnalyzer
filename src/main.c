#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "rng.h"
#include "safety.h"

#define UART0 ((volatile uint8_t *)0x10000000)
#define MAX_ASCII_DIGITS_UINT64T 24

void* memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    for (size_t i = 0; i < len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

static void serial_transmit(char data)
{ 
  *UART0 = data;
}

static void print_string(const char* str)
{ 
  while(*str)
  {
    serial_transmit(*str++);
  }
}

static void print_uint32(uint32_t num)
{
  if(num == 0)
  {
    serial_transmit('0');
    return;
  }

  char buffer[MAX_ASCII_DIGITS_UINT64T];
  uint8_t index = 0;

  while(num > 0)
  {
    buffer[index] = (char)((num%10) + '0');
    index ++;
    num /= 10;
  }

  while (index > 0)
  {
    index--;
    serial_transmit(buffer[index]);
  }
}

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
#define NIBBLE_SHIFT       4
#define NIBBLE_MASK_LOWER  0x0F
#define NIBBLE_MASK_UPPER  0xF0
#define BIT_MASK_ODD       0x01


// Adding no lint since swapping first_index with second_index doesn't affect the result of the function 
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static AppStatus swap_nibbles(uint8_t first_index, uint8_t second_index, uint8_t arr[])
{
  if(!c_assert(first_index <= DECK_SIZE - 1) == true)
  {
    return ERROR_INVALID_INPUT;
  }

  if(!c_assert(second_index <= DECK_SIZE - 1) == true)
  {
    return ERROR_INVALID_INPUT;
  }

  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }
  
  size_t idx1 = first_index >> 1;
  size_t idx2 = second_index >> 1;

  uint8_t val1 = (first_index & BIT_MASK_ODD)  ? (arr[idx1] & NIBBLE_MASK_LOWER) : (arr[idx1] >> NIBBLE_SHIFT);
  uint8_t val2 = (second_index & BIT_MASK_ODD) ? (arr[idx2] & NIBBLE_MASK_LOWER) : (arr[idx2] >> NIBBLE_SHIFT);

  arr[idx1] &= (first_index & BIT_MASK_ODD)  ? NIBBLE_MASK_UPPER : NIBBLE_MASK_LOWER;
  arr[idx2] &= (second_index & BIT_MASK_ODD) ? NIBBLE_MASK_UPPER : NIBBLE_MASK_LOWER;

  arr[idx1] |= (first_index & BIT_MASK_ODD)  ? val2 : (val2 << NIBBLE_SHIFT);
  arr[idx2] |= (second_index & BIT_MASK_ODD) ? val1 : (val1 << NIBBLE_SHIFT);

  return SYS_OK;
}

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

    for (uint8_t i = DECK_SIZE - 1; i > 0; i--) {
        uint8_t second_index = (uint8_t)random_range(0, (uint32_t)i, s_thread_id); 
        
        AppStatus ret_code_swap = swap_nibbles(i, second_index, arr);
        if(!c_assert(ret_code_swap == SYS_OK) == true)
        {
          return ret_code_swap;
        }
    }

    return SYS_OK;
}

static AppStatus assert_deck_is_valid(const uint8_t arr[])
{ 
  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }

  int val_array[(DECK_SIZE/NUM_SUITS) + 1] = {0};
  for(int i = 0; i < DECK_SIZE / 2; ++i)
  {
    val_array[arr[i] & NIBBLE_MASK_LOWER]++;
    val_array[(arr[i] & NIBBLE_MASK_UPPER) >> 4]++;
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

static AppStatus score_num_less_than_unmatchable_card(int8_t val_array[], uint8_t num, int* out_delta_count)
{
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num <= DECK_SIZE / NUM_SUITS) == true)
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

static AppStatus score_num_greater_than_unmatchable_card(int8_t val_array[], uint8_t num, int* out_delta_count)
{
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num <= DECK_SIZE / NUM_SUITS) == true)
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

static AppStatus score_num_equal_to_unmatchable_card(int8_t val_array[], uint8_t num, int* out_delta_count)
{
    if(!require_valid_ptr(out_delta_count))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }

    if(!c_assert(num <= DECK_SIZE / NUM_SUITS) == true)
    { 
      return ERROR_INVALID_INPUT;
    }

    val_array[num]++;
    *out_delta_count = 1;

    return SYS_OK;
}


static AppStatus score_num(int8_t val_array[], uint8_t num, int* out_delta_count)
{ 
    if(!require_valid_ptr(val_array))
    {
      return ERROR_INVALID_INPUT;
    }
    
    if(!c_assert(num <= DECK_SIZE / NUM_SUITS) == true)
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

    if(!c_assert(ret_code_score_func == SYS_OK) == true)
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

static AppStatus process_single_card(uint8_t card_val, int8_t val_array[], int* current_count, bool* out_is_finished)
{
    int delta_count = 0;
    AppStatus ret_score_num = score_num(val_array, card_val, &delta_count);

    if(!c_assert(ret_score_num == SYS_OK) == true)
    {
      return ret_score_num;
    }

    if (!c_assert(val_array[card_val] >= 0 && val_array[card_val] <= NUM_SUITS) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    if (!c_assert(*current_count >= 0 && *current_count <= MAX_ACTIVE_CARDS + 1) == true)
    {
      return ERROR_IMPOSSIBLE_VALUE_REACHED;
    }

    *current_count += delta_count;

    if(*current_count == MAX_ACTIVE_CARDS + 1)
    {
      if (!c_assert(val_array[0] >= 0 && (val_array[0] < NUM_POSSIBLE_SCORES)) == true)
      {
        return ERROR_IMPOSSIBLE_VALUE_REACHED;
      }
      *out_is_finished = true;
    }
    else
    {
      *out_is_finished = false;
    }

    return SYS_OK;
}

static AppStatus score_array(uint8_t arr[], ScoreResult* out_result)
{
  if(!require_valid_ptr(arr))
  {
    return ERROR_INVALID_INPUT;
  }

  AppStatus assert_deck_is_valid_ret_code = assert_deck_is_valid(arr);
  if(!c_assert(assert_deck_is_valid_ret_code == SYS_OK) == true)
  {
    return assert_deck_is_valid_ret_code;
  }

  int count = 0;
  int8_t val_array[(DECK_SIZE/NUM_SUITS) + 1] = {0};

  for(uint8_t i = 0; i < DECK_SIZE; i++)
  {
    uint8_t num;
    if(i & 0x01) { num = (arr[i >> 1] & NIBBLE_MASK_LOWER); }
    else { num = (arr[i >> 1] & NIBBLE_MASK_UPPER) >> 4; }

    bool is_finished = false;

    AppStatus process_ret = process_single_card(num, val_array, &count, &is_finished);
    if(!c_assert(process_ret == SYS_OK) == true)
    {
      return process_ret;
    }

    if(is_finished)
    {
      out_result->value = (int)val_array[0];
      return SYS_OK;
    }
  }

  out_result->value = (int)val_array[0];
  return SYS_OK;
}

static AppStatus run_simulation(uint32_t result_arr[], uint32_t num_sim)
{ 
  if(!require_valid_ptr(result_arr))
  {
    return ERROR_INVALID_INPUT;
  }

  uint8_t deck[DECK_SIZE / 2] = { 0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xAA, 0xAA, 0xBB, 0xBB, 0xCC, 0xCC, 0xDD, 0xDD };

  ScoreResult res = {0};

  for(uint32_t i = 0; i < num_sim; i++)
  {
    AppStatus ret_code_fisher_yates = fisher_yates_shuffle(deck, 0);
    if(!c_assert(ret_code_fisher_yates == SYS_OK))
    {
      return ret_code_fisher_yates;
    }
    AppStatus ret_code_score_array = score_array(deck, &res);
    if(!c_assert(ret_code_score_array == SYS_OK))
    {
      return ret_code_score_array;
    }
    result_arr[res.value]++;
  }

  return SYS_OK;
}

int main(void)
{
  print_string("RISC-V sim booting...");

  initialize_rng();

  uint32_t result_array[NUM_POSSIBLE_SCORES] = {0}; 
  
  AppStatus ret_code_run_simulation = run_simulation(result_array, (uint32_t)NUM_SIMS);
    
  if(!c_assert(ret_code_run_simulation == SYS_OK) == true)
  {
    return 1;
  }

  uint32_t count = 0;

  print_string("\n ------------- SCORES ------------- \n");
  for(int i = 0; i < NUM_POSSIBLE_SCORES; ++i)
  {
    count += result_array[i];
    print_uint32((uint32_t)i);
    print_string(": ");
    print_uint32(result_array[i]);
    print_string("\n");
  }
  print_string("Simulations complete. Total sim ran: ");
  print_uint32(count);
  print_string("\n");

  while(1) {}
  return 0;
}

