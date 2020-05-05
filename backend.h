#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>

/* match log. size */
#define MATCH_LOGSIZE 4

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

void set_num_threads(int n);
int get_num_threads();

void set_max_match_count(int n);
int get_max_match_count();

#endif /* BACKEND_H */
