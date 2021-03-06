#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>

/* match log. size */
#define MATCH_LOGSIZE 5

/* match size */
#define MAX_MATCH_LEN (1 << MATCH_LOGSIZE)

/*
 * Search the segment p to p + get_forward_window(), and find the best match.
 * The algorithm only considers the matches at most MAX_MATCH_LEN characters long.
 * At most get_max_match_count() matches are considered.
 * The get_forward_window() and get_max_match_count() substantially affect the compression ratio and speed.
 *
 * Returns the length of the best match.
 */
size_t find_best_match(char *p);

void set_forward_window(size_t n);
size_t get_forward_window();

void set_max_match_count(int n);
int get_max_match_count();

size_t get_magic_factor1();
void set_magic_factor1(size_t factor);
size_t get_magic_factor2();
void set_magic_factor2(size_t factor);

#endif /* BACKEND_H */
