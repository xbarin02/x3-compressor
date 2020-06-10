#include "backend.h"
#ifdef _OPENMP
#	include <omp.h>
#endif
#include <assert.h>
#include <string.h>

/* search buffer */
static size_t g_forward_window = 8 * 1024;

void set_forward_window(size_t n)
{
	g_forward_window = n;
}

size_t get_forward_window()
{
	return g_forward_window;
}

static int g_num_threads = 8;

void set_num_threads(int n)
{
	g_num_threads = n;
}

int get_num_threads()
{
#ifdef _OPENMP
	return g_num_threads;
#else
	return 1;
#endif
}

/* found empirically */
static int g_max_match_count = 15;

void set_max_match_count(int n)
{
	g_max_match_count = n;
}

int get_max_match_count()
{
	return g_max_match_count;
}

#ifdef _OPENMP
size_t find_best_match(char *p)
{
	assert(g_forward_window > (size_t)g_num_threads);

	size_t segment_size = g_forward_window / g_num_threads;

	assert(g_forward_window % g_num_threads == 0);

	assert(segment_size > MAX_MATCH_LEN);

	char *end = p + g_forward_window;

	for (int tc = g_max_match_count; tc > 0; --tc) {
		for (size_t len = MAX_MATCH_LEN; len > 0; --len) {
			/* trying match string of the length 'len' chars */
			int count[g_num_threads];
			memset(count, 0, sizeof(int) * g_num_threads);

			#pragma omp parallel num_threads(g_num_threads)
			{
				int id = omp_get_thread_num();

				char *s0 = p + (id + 0) * segment_size;
				char *s1 = p + (id + 1) * segment_size;

				if (s0 == p) {
					s0 = p + len;
				}
				if (s1 == end) {
					s1 = end - len;
				}

				/* start matching at 's' */
				for (char *s = s0; s < s1; s++) {
					if (memcmp(p, s, len) == 0) {
						count[id]++;
					}
				}

			}

			for (int i = 1; i < g_num_threads; ++i) {
				count[0] += count[i];
			}

			if (count[0] > tc) {
				return len;
			}
		}
	}

	return 1;
}
#else
size_t find_best_match(char *p)
{
	char *end = p + g_forward_window;

	for (int tc = g_max_match_count; tc > 0; --tc) {
		for (size_t len = MAX_MATCH_LEN; len > 0; --len) {
			/* trying match string of the length 'len' chars */
			int count = 0;

			/* start matching at 's' */
			for (char *s = p + len; s < end - len; s++) {
				if (memcmp(p, s, len) == 0) {
					count++;
				}
			}

			if (count > tc) {
				return len;
			}
		}
	}

	return 1;
}
#endif

size_t fast_find_best_match(char *p)
{
	size_t len_count[MAX_MATCH_LEN];

	char *end = p + g_forward_window;

	for (int i = 0; i < MAX_MATCH_LEN; ++i) {
		len_count[i] = 0;
	}

	for (char *s = p + 1; s < end - MAX_MATCH_LEN; ++s) {
		for (int i = 0; i < MAX_MATCH_LEN; ++i) {
			if (p[i] == s[i]) {
				len_count[i]++;
			} else {
				break;
			}
		}
	}

	// find largest len_count[] <= g_max_match_count

	int max_i = 1;
	size_t max = len_count[1];

	for (int i = 2; i < MAX_MATCH_LEN; ++i) {
		if (len_count[i] > max && len_count[i] <= (size_t)g_max_match_count) {
			max = len_count[i];
			max_i = i;
		}
	}

	assert(max_i > 0);

	return max_i;
}
