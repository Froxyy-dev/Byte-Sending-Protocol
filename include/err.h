#ifndef MIM_ERR_H
#define MIM_ERR_H

#include <stdnoreturn.h>

// Print information about a system error and quits.
noreturn void sys_fatal(const char* fmt, ...);

// Print information about an error and quits.
noreturn void fatal(const char* fmt, ...);

// Print information about an error and return.
void error(const char* fmt, ...);

// Print information about a system error and return.
void sys_error(const char* fmt, ...);

#define ASSERT_MALLOC(expr)                                                                      \
    do {                                                                                         \
        if ((expr) == NULL)                                                                      \
            sys_fatal("malloc");                                                                 \
    } while(0)

#endif
