#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>

/* match log. size */
#define MATCH_LOGSIZE 4

/* match size */
#define MAX_MATCH_LEN (1 << MATCH_LOGSIZE)

size_t find_best_match(char *p);

void set_forward_window(size_t n);
size_t get_forward_window();

void set_num_threads(int n);
int get_num_threads();

void set_max_match_count(int n);
int get_max_match_count();

#endif /* BACKEND_H */
