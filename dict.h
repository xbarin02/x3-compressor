#ifndef DICT_H
#define DICT_H

#include "backend.h"
#include <stddef.h>

struct elem {
	char s[MAX_MATCH_LEN]; /* the string */
	size_t len; /* of the length */
	char *last_pos; /* recently seen at the position */
	size_t cost; /* sort key */
	size_t tag; /* id */
};

size_t dict_get_size();

size_t dict_get_elems();

void dict_enlarge();

size_t elem_calc_cost(struct elem *e, char *curr_pos);

void elem_fill(struct elem *e, char *p, size_t len);

int dict_can_insert_elem();

void dict_insert_elem(const struct elem *e);

/*
 * Searches the dictionary and returns the best match.
 *
 * Returns the index of the best match.
 * If no match is found, the function returns (size_t)-1.
 */
size_t dict_find_match(const char *p);

void dict_update_costs(char *p);

int dict_query_elem(struct elem *e);

size_t dict_get_len_by_index(size_t index);

const char *dict_get_str_by_index(size_t index);

size_t dict_get_tag_by_index(size_t index);

size_t dict_get_index_by_tag(size_t tag);

void dict_set_last_pos(size_t index, char *p);

void dict_dump();

void dict_destroy();

#endif
