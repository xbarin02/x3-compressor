#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

void fload(void *ptr, size_t size, FILE *stream)
{
	if (fread(ptr, 1, size, stream) < size) {
		abort();
	}
}

size_t fsize(FILE *stream)
{
	long begin = ftell(stream);

	if (begin == (long)-1) {
		fprintf(stderr, "Stream is not seekable\n");
		abort();
	}

	if (fseek(stream, 0, SEEK_END)) {
		abort();
	}

	long end = ftell(stream);

	if (end == (long)-1) {
		abort();
	}

	if (fseek(stream, begin, SEEK_SET)) {
		abort();
	}

	return (size_t)end - (size_t)begin;
}

/* search buffer */
#define FORWARD_WINDOW (8 * 1024)

/* log. size */
#define MATCH_LOGSIZE 3

/* look-ahead buffer */
#define MAX_MATCH_LEN (1 << MATCH_LOGSIZE)

/* found empirically */
#define TCOUNT 10

size_t find_best_match(char *p)
{
	char *end = p + FORWARD_WINDOW;

	for (int tc = TCOUNT; tc > 0; --tc) {
		for (size_t len = MAX_MATCH_LEN; len > 0; --len) {
			/* trying match string of the length 'len' chars */
			int count = 0;

			/* start matching at 's' */
			for (char *s = p + len; s < end - len; ) {
				if (memcmp(p, s, len) == 0) {
					count++;
					s += len;
				} else {
					s++;
				}
			}

			if (count > tc) {
				return len;
			}
		}
	}

	return 1;
}

struct elem {
	char s[MAX_MATCH_LEN]; /* the string */
	size_t len; /* of the length */
	char *last_pos; /* recently seen at the position */
	size_t cost; /* sort key */
	size_t tag; /* id */
};

struct item {
	size_t tag;
	size_t freq; /* used n-times */
};

struct ctx {
	size_t items; /* allocated elements */
	struct item *arr; /* pointer to the first item */

	size_t symb_sum, symb_cnt; /* due to Golomb-Rice coding */
};

/* allocated size, enlarged logarithmically */
size_t dict_logsize = 0;
size_t dict_size = 1;

/* number of elements */
size_t elems = 0;

struct elem *dict = NULL;
struct ctx *ctx1 = NULL;
struct ctx ctx2[65536];

void enlarge_dict()
{
	dict_logsize++;
	dict_size = (size_t)1 << dict_logsize;

	dict = realloc(dict, dict_size * sizeof(struct elem));

	if (dict == NULL) {
		abort();
	}

	// memset(dict + elems, 0, (dict_size - elems) * sizeof(struct elem));

	ctx1 = realloc(ctx1, dict_size * sizeof(struct ctx));

	if (ctx1 == NULL) {
		abort();
	}

	memset(ctx1 + elems, 0, (dict_size - elems) * sizeof(struct ctx));
}

size_t calc_cost(struct elem *e, char *curr_pos)
{
	assert(e != NULL);

	assert(curr_pos >= e->last_pos);

	size_t dist = curr_pos - e->last_pos;

	size_t cost = dist;

	return cost;
}

void fill_elem(struct elem *e, char *p, size_t len)
{
	assert(e != NULL);

	memcpy(e->s, p, len);
	e->len = len;

	e->last_pos = p;

	e->cost = calc_cost(e, p);
}

static int cmp(const void *l, const void *r)
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

int is_zero(const struct elem *e)
{
	return e->len == 0;
}

void insert_elem(const struct elem *e)
{
	assert(e != NULL);

	assert(!is_zero(e));

	if (elems >= dict_size) {
		enlarge_dict();
	}

	assert(elems < dict_size);

	dict[elems] = *e;
	dict[elems].tag = elems; /* element is filled except a tag, set the tag */
	elems++;
}

size_t find_in_dictionary(const char *p)
{
	size_t best_len = 0;
	size_t best_len_i;

	for (size_t i = 0; i < elems; ++i) {
		if (dict[i].len > 0 && memcmp(p, dict[i].s, dict[i].len) == 0) {
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

size_t tag_match_count = 0;
size_t tag_newentry_count = 0;
size_t stream_size_raw = 0;
size_t stream_size_raw_str = 0;
size_t stream_size_gr = 0;

void update_dict(char *p)
{
	struct elem e0;
	memset(&e0, 0, sizeof(struct elem));

	for (size_t i = 0; i < elems; ++i) {
		assert(!is_zero(&dict[i]));

		dict[i].cost = calc_cost(&dict[i], p);
	}

	qsort(dict, elems, sizeof(struct elem), cmp);

	if (elems >= 2) {
		assert(dict[0].cost <= dict[1].cost);
		assert(dict[elems-2].cost <= dict[elems-1].cost);
	}
}

size_t bio_sizeof_gr(size_t k, size_t N)
{
	size_t size;
	size_t Q = N >> k;

	size = Q + 1;

	size += k;

	return size;
}

#define RESET_INTERVAL 256 /* recompute Golomb-Rice codes after... */

size_t opt_k = 11;
size_t symbol_sum = 0, symbol_count = 0; /* mean = symbol_sum / symbol_count */

size_t get_opt_k(size_t symb_sum, size_t symb_cnt)
{
	if (symb_cnt == 0) {
		return 0;
	}

	int k;

	for (k = 1; (symb_cnt << k) <= symb_sum; ++k)
		;

	return (size_t)(k - 1);
}

void update_model(size_t delta)
{
	if (symbol_count == RESET_INTERVAL) {
		opt_k = get_opt_k(symbol_sum, symbol_count);

		symbol_count = 0;
		symbol_sum = 0;
	}

	symbol_sum += delta;
	symbol_count++;
}

struct item *ctx_query_tag(struct ctx *c, size_t tag)
{
	for (size_t i = 0; i < c->items; ++i) {
		if (c->arr[i].tag == tag) {
			return &(c->arr[i]);
		}
	}

	return NULL;
}

size_t ctx_query_tag_index(struct ctx *c, size_t tag)
{
	for (size_t i = 0; i < c->items; ++i) {
		if (c->arr[i].tag == tag) {
			return i;
		}
	}

	return (size_t)-1;
}

void ctx_add_tag(struct ctx *c, size_t tag)
{
	assert(!ctx_query_tag(c, tag));

	c->items++;

	c->arr = realloc(c->arr, c->items * sizeof(struct item));

	if (c->arr == NULL) {
		abort();
	}

	c->arr[c->items - 1].tag = tag;
	c->arr[c->items - 1].freq = 1;
}

size_t ctx_miss = 0;
size_t ctx1_hit = 0;
size_t ctx2_hit = 0;

int elem_query_dictionary(struct elem *e)
{
	for (size_t i = 0; i < elems; ++i) {
		if (dict[i].len == e->len && memcmp(dict[i].s, e->s, e->len) == 0) {
			return 1;
		}
	}

	return 0;
}

int compar_items(const void *l, const void *r)
{
	const struct item *li = l;
	const struct item *ri = r;

	if (li->freq > ri->freq) {
		return -1;
	}

	if (li->freq < ri->freq) {
		return +1;
	}

	return 0;
}

/* sort ctx->items[] according to item.freq */
void sort_ctx(struct ctx *ctx)
{
	qsort(ctx->arr, ctx->items, sizeof(struct item), compar_items);

	if (ctx->items > 1) {
		assert(ctx->arr[0].freq >= ctx->arr[1].freq);
	}
}

/* encode dict[index].tag in context, rather than index */
void encode_tag(size_t context1, size_t context2, size_t index)
{
	assert(ctx1 != NULL);

	struct ctx *c1 = ctx1 + context1;
	struct ctx *c2 = ctx2 + context2;

	size_t tag = dict[index].tag;

	if (ctx_query_tag(c1, tag) != NULL) {
		ctx1_hit++;

		if (c1->items > 1) {
			size_t k = get_opt_k(c1->symb_sum, c1->symb_cnt);
			size_t item_index = ctx_query_tag_index(c1, tag);
			stream_size_gr += 1 + bio_sizeof_gr(k, item_index); /* signal: hit (ctx1) + index (1 bit: 1) */
			c1->symb_sum += item_index;
			c1->symb_cnt++;
		} else {
			stream_size_gr += 1; /* signal: hit (ctx1) + index (1 bit: 1) no information needed */
		}

		// increment item->freq
		struct item *item = ctx_query_tag(c1, tag);
		item->freq++;

		// sort ctx
		sort_ctx(c1);
	} else {
		if (ctx_query_tag(c2, tag) != NULL) {
			ctx2_hit++;

			if (c2->items > 1) {
				size_t k = get_opt_k(c2->symb_sum, c2->symb_cnt);
				size_t item_index = ctx_query_tag_index(c2, tag);
				stream_size_gr += 2 + bio_sizeof_gr(k, item_index); /* signal: hit (ctx2) + index (2 bits: 01) */
				c2->symb_sum += item_index;
				c2->symb_cnt++;
			} else {
				stream_size_gr += 2; /* signal: hit (ctx2) + index (2 bits: 01) no information needed */
			}
		} else {
			ctx_miss++;
			stream_size_gr += 3 + bio_sizeof_gr(opt_k, index); /* signal: miss + index (3 bits: 001) */
			update_model(index);
		}

		ctx_add_tag(c1, tag);

		// sort ctx
		sort_ctx(c1);
	}

	if (ctx_query_tag(c2, tag) == NULL) {
		ctx_add_tag(c2, tag);
		sort_ctx(c2);
	} else {
		struct item *item = ctx_query_tag(c2, tag);
		item->freq++;
		sort_ctx(c2);
	}
}

size_t make_context2(char *p)
{
	return (unsigned char)p[-1] | (unsigned char)p[-2];
}

void compress(char *ptr, size_t size)
{
	char *end = ptr + size;

	size_t context1 = 0; /* last tag */
	size_t context2 = 0; /* last two bytes */

	for (char *p = ptr; p < end; ) {
#if 0
		printf("find %p / %p\n", p, end);
#endif
		/* (1) look into dictionary */
		size_t index = find_in_dictionary(p);

		if (index != (size_t)-1 && dict[index].len >= find_best_match(p)) {
			/* found in dictionary */
			size_t len = dict[index].len;

#if 0
			printf("[DEBUG] (match size %zu) incrementing [%zu] freq %zu\n", len, index, dict[index].freq);
#endif

			encode_tag(context1, context2, index);

			context1 = dict[index].tag;

			dict[index].last_pos = p;

			p += len;

			if (p >= ptr + 2) {
				context2 = make_context2(p);
			}

			/* recalc all costs, sort */
			update_dict(p);

			tag_match_count++;
		} else {
			/* (2) else find best match and insert it into dictionary */
			size_t len = find_best_match(p);
#if 0
			printf("[DEBUG] new match len %zu\n", len);
#endif

#if 0
			if (fwrite(p, len, 1, rawstream) < 1) {
				abort();
			}
#endif

			struct elem e;
			fill_elem(&e, p, len);

			assert(elem_query_dictionary(&e) == 0);

			insert_elem(&e);

			p += len;

			context1 = 0;

			if (p >= ptr + 2) {
				context2 = make_context2(p);
			}

			update_dict(p);

			tag_newentry_count++;

			stream_size_raw += 3 + MATCH_LOGSIZE + 8 * len; /* 3 bits: 000 */
			stream_size_raw_str += 8 * len;
		}
	}
}

void dump_dict()
{
	for (size_t i = 0; i < elems; ++i) {
		printf("dict[%zu] = \"%.*s\" (len=%zu)\n", i, (int)dict[i].len, dict[i].s, dict[i].len);
	}
}

int main(int argc, char *argv[])
{
	char *path = argc > 1 ? argv[1] : "enwik6";

	printf("path: %s\n", path);

	FILE *stream = fopen(path, "r");

	if (stream == NULL) {
		abort();
	}

	size_t size = fsize(stream);

	char *ptr = malloc(size + FORWARD_WINDOW);

	if (ptr == NULL) {
		abort();
	}

	memset(ptr + size, 0, FORWARD_WINDOW);

	fload(ptr, size, stream);

	enlarge_dict();

	compress(ptr, size);

	printf("tags: match %zu, new entry %zu\n", tag_match_count, tag_newentry_count);
	printf("input stream: %zu\n", size);
	printf("est. stream size: %zu (tags %zu / %f%%, uncompressed %zu / %f%%)\n",
		(stream_size_gr + stream_size_raw)/8,
		stream_size_gr/8, 100.f*stream_size_gr/(stream_size_gr + stream_size_raw),
		stream_size_raw/8, 100.f*stream_size_raw/(stream_size_gr + stream_size_raw)
	);
	printf("Golomb-Rice stream size: %zu\n", stream_size_gr/8);
	printf("uncompressed raw output: %zu\n", stream_size_raw_str/8);
	printf("ratio: %f\n", size / (float)((stream_size_gr + stream_size_raw)/8));

	printf("contexts: hit1=%zu hit2=%zu miss=%zu new_entry=%zu\n", ctx1_hit, ctx2_hit, ctx_miss, tag_newentry_count);

#if 0
	dump_dict();
#endif

	return 0;
}
