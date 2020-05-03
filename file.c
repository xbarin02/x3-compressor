#include "file.h"

#include <stdlib.h>

void fload(void *ptr, size_t size, FILE *stream)
{
	if (fread(ptr, 1, size, stream) < size) {
		abort();
	}
}

void fsave(void *ptr, size_t size, FILE *stream)
{
	if (fwrite(ptr, 1, size, stream) < size) {
		abort();
	}
}

size_t fsize(FILE *stream)
{
	long begin = ftell(stream);

	if (begin == (long)-1) {
		fprintf(stderr, "Stream is not seekable\n");
		abort();
	}

	if (fseek(stream, 0, SEEK_END)) {
		abort();
	}

	long end = ftell(stream);

	if (end == (long)-1) {
		abort();
	}

	if (fseek(stream, begin, SEEK_SET)) {
		abort();
	}

	return (size_t)end - (size_t)begin;
}
