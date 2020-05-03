#include "utils.h"
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>

long wall_clock()
{
	struct timespec t;

	if (clock_gettime(CLOCK_REALTIME, &t) < 0) {
		fprintf(stderr, "wall-clock error\n");
		return 0;
	}

	return t.tv_sec * 1000000000L + t.tv_nsec;
}
