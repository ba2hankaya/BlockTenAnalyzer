#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>

#ifdef PRODUCTION_RELEASE
    #define tst_debugging(...) ((void)0)
#else
    #define tst_debugging(fmt, ...) ((void)0)
#endif

#define c_assert(e) ((e) ? (true) : \
    (tst_debugging("%s:%d: assertion '%s' failed\n", \
    __FILE__, __LINE__, #e), false))

#define require_valid_ptr(ptr) c_assert((ptr) != NULL)

#define require_in_bounds(idx, max_size) c_assert((idx) >= 0 && (idx) < (max_size))

#endif
