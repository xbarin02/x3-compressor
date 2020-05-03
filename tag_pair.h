#ifndef TAG_PAIR
#define TAG_PAIR

#include <stddef.h>

struct tag_pair {
	size_t tag0;
	size_t tag1;

	size_t e; /* linear id */
	struct tag_pair *l, *r;
};

size_t tag_pair_get_elems();
size_t tag_pair_get_size();

struct tag_pair make_tag_pair(size_t tag0, size_t tag1);

void tag_pair_enlarge();

size_t tag_pair_query(struct tag_pair *pair);

int tag_pair_can_add();
size_t tag_pair_add(struct tag_pair *pair);

void tag_pair_destroy();

#endif /* TAG_PAIR */
