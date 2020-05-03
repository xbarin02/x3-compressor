#include "tag_pair.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* map: (tag, tag) -> index */
struct tag_pair *map0 = NULL; /* root of tree */
size_t tag_pair_elems = 0;
size_t tag_pair_size = 1; /* allocated */

size_t tag_pair_get_elems()
{
	return tag_pair_elems;
}

size_t tag_pair_get_size()
{
	return tag_pair_size;
}

struct tag_pair make_tag_pair(size_t tag0, size_t tag1)
{
	struct tag_pair pair;

	pair.tag0 = tag0;
	pair.tag1 = tag1;

	return pair;
}

int tag_pair_compar(const void *l, const void *r)
{
	const struct tag_pair *lpair = l;
	const struct tag_pair *rpair = r;

	if (lpair->tag0 < rpair->tag0) {
		return -1;
	}

	if (lpair->tag0 > rpair->tag0) {
		return +1;
	}

	if (lpair->tag1 < rpair->tag1) {
		return -1;
	}

	if (lpair->tag1 > rpair->tag1) {
		return +1;
	}

	return 0;
}

void tag_pair_enlarge()
{
	tag_pair_size <<= 1;
}

size_t tag_pair_query(struct tag_pair *pair)
{
	struct tag_pair *this = map0;

	while (this != NULL) {
		int r = tag_pair_compar(this, pair);

		if (r == 0) {
			return this->e;
		} else if (r < 0) {
			this = this->l;
		} else {
			this = this->r;
		}
	}

	return (size_t)-1;
}

void tag_pair_free(struct tag_pair *this)
{
	if (this != NULL) {
		tag_pair_free(this->l);
		tag_pair_free(this->r);
		free(this);
	}
}

int tag_pair_can_add()
{
	return tag_pair_elems != tag_pair_size;
}

size_t tag_pair_add(struct tag_pair *pair)
{
	assert(tag_pair_query(pair) == (size_t)-1);

	assert(tag_pair_elems != tag_pair_size);

	struct tag_pair **this = &map0;

	while (*this != NULL) {
		if (tag_pair_compar(*this, pair) < 0) {
			this = & (*this)->l;
		} else {
			this = & (*this)->r;
		}
	}

	*this = malloc(sizeof(struct tag_pair));

	if (*this == NULL) {
		abort();
	}

	**this = *pair;
	(*this)->e = tag_pair_elems;
	(*this)->l = NULL;
	(*this)->r = NULL;

	tag_pair_elems++;

	return tag_pair_elems - 1;
}

void tag_pair_destroy()
{
	tag_pair_free(map0);
}
