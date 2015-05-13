#ifndef _LXZZ_H_
#define _LXZZ_H_

#define _XZZ_DPRINTF_
#ifdef _XZZ_DPRINTF_

#include <stdio.h>
#define Dlog(format, ...) fprintf(stderr, format, ##__VA_ARGS__); \
	fprintf(stderr, "\n");

#else

#define Dlog(format, ...)

#endif

#endif

