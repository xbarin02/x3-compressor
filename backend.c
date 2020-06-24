#include "backend.h"
#include <assert.h>
#include <string.h>

#include "dict.h"

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

static size_t g_factor1 = 4;
static size_t g_factor2 = 0;

size_t get_magic_factor1()
{
	return g_factor1;
}

void set_magic_factor1(size_t factor)
{
	g_factor1 = factor;
}

size_t get_magic_factor2()
{
	return g_factor2;
}

void set_magic_factor2(size_t factor)
{
	g_factor2 = factor;
}

size_t find_best_match(char *p)
{
	size_t count[MAX_MATCH_LEN];

	char *end = p + g_forward_window;

	for (int i = 0; i < MAX_MATCH_LEN; ++i) {
		count[i] = 0;
	}

	for (char *s = p + 1; s < end - MAX_MATCH_LEN; ++s) {
		for (int i = 0; i < MAX_MATCH_LEN; ++i) {
			if (p[i] == s[i]) {
				count[i]++;
			} else {
				break;
			}
		}
	}

	for (int tc = g_max_match_count; tc > 0; --tc) {
		for (int i = MAX_MATCH_LEN - 1; i >= 0; --i) {
			if (count[i] > (size_t)tc) {
				if (i >= 2 && g_factor1 > 0) {
					if (dict_find_match(p + i) != (size_t)-1 && dict_get_len_by_index(dict_find_match(p + i)) * g_factor1 > (size_t)(i + 1)) {
						goto next;
					}
				}
				if (i >= 1 && g_factor2 > 0) {
					for (int o = 1; o <= i; ++o) {
						if (dict_find_match(p + o) != (size_t)-1 && ((int)dict_get_len_by_index(dict_find_match(p + o)) - o) * (int)g_factor2 > i + 1) {
							goto next;
						}
					}
				}

				return i + 1;
			}
			next:
				;
		}
	}

	return 1;
}
