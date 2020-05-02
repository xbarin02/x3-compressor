#include "dict.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* allocated size, enlarged logarithmically */
size_t dict_logsize = 0;
size_t dict_size = 1;

/* number of elements in the dictionary */
size_t dict_elems = 0;

struct elem *dict = NULL; /* the dictionary, sorted by distance = curr_pos - dict[i]->last_pos */

size_t dict_get_size()
{
	return dict_size;
}

size_t dict_get_elems()
{
	return dict_elems;
}

void dict_enlarge()
{
	dict_logsize++;
	dict_size = (size_t)1 << dict_logsize;

	dict = realloc(dict, dict_size * sizeof(struct elem));

	if (dict == NULL) {
		abort();
	}
}

size_t elem_calc_cost(struct elem *e, char *curr_pos)
{
	assert(e != NULL);

	assert(curr_pos >= e->last_pos);

	size_t dist = curr_pos - e->last_pos;

	size_t cost = dist;

	return cost;
}

void elem_fill(struct elem *e, char *p, size_t len)
{
	assert(e != NULL);

	memcpy(e->s, p, len);
	e->len = len;

	e->last_pos = p;
}

static int elem_compar(const void *l, const void *r)
{
	const struct elem *le = l;
	const struct elem *re = r;

	if (le->cost > re->cost) {
		return +1;
	}

	if (le->cost < re->cost) {
		return -1;
	}

	return 0;
}

int elem_is_zero(const struct elem *e)
{
	return e->len == 0;
}

int dict_insert_elem(const struct elem *e)
{
	int reall = 0;

	assert(e != NULL);

	assert(!elem_is_zero(e));

	if (dict_elems >= dict_size) {
		dict_enlarge();
		reall = 1;
	}

	assert(dict_elems < dict_size);

	dict[dict_elems] = *e;
	dict[dict_elems].tag = dict_elems; /* element is filled except a tag, set the tag */

	dict_elems++;

	return reall;
}

size_t dict_find_match(const char *p)
{
	size_t best_len = 0;
	size_t best_len_i;

	for (size_t i = 0; i < dict_elems; ++i) {
		assert(dict[i].len > 0);

		if (memcmp(p, dict[i].s, dict[i].len) == 0) {
			/* match */
#if 0
			printf("dictionary match @ [%i] len %zu\n", i, dict[i].len);
#endif
			if (dict[i].len > best_len) {
				best_len = dict[i].len;
				best_len_i = i;
			}
		}
	}

	if (best_len > 0) {
		return best_len_i;
	}

	return (size_t)-1; /* not found */
}

void dict_update_costs(char *p)
{
	for (size_t i = 0; i < dict_elems; ++i) {
		assert(!elem_is_zero(&dict[i]));

		dict[i].cost = elem_calc_cost(&dict[i], p);
	}

	qsort(dict, dict_elems, sizeof(struct elem), elem_compar);

	if (dict_elems >= 2) {
		assert(dict[0].cost <= dict[1].cost);
		assert(dict[dict_elems - 2].cost <= dict[dict_elems - 1].cost);
	}
}

int dict_query_elem(struct elem *e)
{
	for (size_t i = 0; i < dict_elems; ++i) {
		if (dict[i].len == e->len && memcmp(dict[i].s, e->s, e->len) == 0) {
			return 1;
		}
	}

	return 0;
}

size_t dict_get_len_by_index(size_t index)
{
	return dict[index].len;
}

size_t dict_get_tag_by_index(size_t index)
{
	return dict[index].tag;
}

void dict_set_last_pos(size_t index, char *p)
{
	dict[index].last_pos = p;
}

void dict_dump()
{
	for (size_t i = 0; i < dict_elems; ++i) {
		printf("dict[%zu] = \"%.*s\" (len=%zu)\n", i, (int)dict[i].len, dict[i].s, dict[i].len);
	}
}

void dict_destroy()
{
	free(dict);
}
