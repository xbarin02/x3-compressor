#ifndef FILE_H
#define FILE_H

#include <stddef.h>
#include <stdio.h>

void fload(void *ptr, size_t size, FILE *stream);

void fsave(void *ptr, size_t size, FILE *stream);

size_t fsize(FILE *stream);

#endif /* FILE_H */
