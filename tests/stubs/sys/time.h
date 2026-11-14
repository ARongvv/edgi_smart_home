/* tcc 0.9.27 ships no <sys/time.h>; other compilers keep their own. */
#pragma once
#if defined(__TINYC__)
#include <time.h>
int gettimeofday(struct timeval *tv, void *tz);
#else
#include_next <sys/time.h>
#endif
