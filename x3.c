#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

/* search buffer */
#define FORWARD_WINDOW (4 * 1024)

/* log. size */
#define MATCH_LOGSIZE 4

/* look-ahead buffer */
#define MAX_MATCH_LEN (1 << MATCH_LOGSIZE)

/* found empirically */
#define MAX_MATCH_COUNT 10

/* recompute Golomb-Rice codes after... */
#define RESET_INTERVAL 256

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

struct gr {
	size_t opt_k;
	/* mean = symb_sum / symb_count */
	size_t symb_sum;
	size_t symb_cnt;
};

struct ctx {
	size_t items; /* allocated elements */
	struct item *arr; /* pointer to the first item */

	struct gr gr;
};

struct tag_pair {
	size_t tag0;
	size_t tag1;

	size_t e; /* linear id */
	struct tag_pair *l, *r;
};

/* map: (tag, tag) -> index */
struct tag_pair *map0 = NULL; /* root */
size_t tag_pair_elems = 0;
size_t tag_pair_size = 1; /* allocated */

/* allocated size, enlarged logarithmically */
size_t dict_logsize = 0;
size_t dict_size = 1;

/* number of elements */
size_t elems = 0;

struct elem *dict = NULL;
struct ctx *ctx0 = NULL;
struct ctx *ctx1 = NULL;
struct ctx ctx2[65536];

struct gr gr_dict;

size_t dict_hit_count = 0;
size_t dict_miss_count = 0;
size_t stream_size = 0;
size_t stream_size_raw = 0;
size_t stream_size_raw_str = 0;
size_t stream_size_gr = 0;
size_t stream_size_gr_hit0 = 0;
size_t stream_size_gr_hit1 = 0;
size_t stream_size_gr_hit2 = 0;
size_t stream_size_gr_miss = 0;
size_t ctx_miss = 0;
size_t ctx0_hit = 0;
size_t ctx1_hit = 0;
size_t ctx2_hit = 0;

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

void tag_pair_realloc()
{
	tag_pair_size <<= 1;

	ctx0 = realloc(ctx0, tag_pair_size * sizeof(struct ctx));

	if (ctx0 == NULL) {
		abort();
	}

	memset(ctx0 + tag_pair_elems, 0, (tag_pair_size - tag_pair_elems) * sizeof(struct ctx));
}

void tag_pair_init()
{
	tag_pair_realloc();
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

size_t tag_pair_add(struct tag_pair *pair)
{
	assert(tag_pair_query(pair) == (size_t)-1);

	if (tag_pair_elems == tag_pair_size) {
		tag_pair_realloc();
	}

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

size_t find_best_match(char *p)
{
	char *end = p + FORWARD_WINDOW;

	for (int tc = MAX_MATCH_COUNT; tc > 0; --tc) {
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

void gr_init(struct gr *gr, size_t k)
{
	gr->opt_k = k;
	gr->symb_sum = 0;
	gr->symb_cnt = 0;
}

void enlarge_dict()
{
	dict_logsize++;
	dict_size = (size_t)1 << dict_logsize;

	dict = realloc(dict, dict_size * sizeof(struct elem));

	if (dict == NULL) {
		abort();
	}

	ctx1 = realloc(ctx1, dict_size * sizeof(struct ctx));

	if (ctx1 == NULL) {
		abort();
	}

	memset(ctx1 + elems, 0, (dict_size - elems) * sizeof(struct ctx));

	for (size_t e = elems; e < dict_size; ++e) {
		gr_init(&ctx1[e].gr, 0);
	}
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

void update_dict(char *p)
{
	for (size_t i = 0; i < elems; ++i) {
		assert(!is_zero(&dict[i]));

		dict[i].cost = calc_cost(&dict[i], p);
	}

	qsort(dict, elems, sizeof(struct elem), elem_compar);

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

void gr_recalc_k(struct gr *gr)
{
	gr->opt_k = get_opt_k(gr->symb_sum, gr->symb_cnt);
}

void gr_update(struct gr *gr, size_t symb)
{
	gr->symb_sum += symb;
	gr->symb_cnt++;
}

void update_model(struct gr *gr, size_t delta)
{
	if (gr->symb_cnt == RESET_INTERVAL) {
		gr_recalc_k(gr);

		gr_init(gr, gr->opt_k);
	}

	gr_update(gr, delta);
}

struct item *ctx_query_tag_item(struct ctx *c, size_t tag)
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
	assert(!ctx_query_tag_item(c, tag));

	c->items++;

	c->arr = realloc(c->arr, c->items * sizeof(struct item));

	if (c->arr == NULL) {
		abort();
	}

	c->arr[c->items - 1].tag = tag;
	c->arr[c->items - 1].freq = 1;
}

int elem_query_dictionary(struct elem *e)
{
	for (size_t i = 0; i < elems; ++i) {
		if (dict[i].len == e->len && memcmp(dict[i].s, e->s, e->len) == 0) {
			return 1;
		}
	}

	return 0;
}

int item_compar(const void *l, const void *r)
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
void ctx_sort(struct ctx *ctx)
{
	qsort(ctx->arr, ctx->items, sizeof(struct item), item_compar);

	if (ctx->items > 1) {
		assert(ctx->arr[0].freq >= ctx->arr[1].freq);
	}
}

void ctx_item_inc_freq(struct ctx *ctx, size_t tag)
{
	struct item *item = ctx_query_tag_item(ctx, tag);

	item->freq++;
}

/* returns number of bits produced */
size_t ctx_encode_tag(struct ctx *ctx, size_t tag)
{
	size_t size = 0;

	if (ctx->items > 1) {
		gr_recalc_k(&ctx->gr);
		size_t item_index = ctx_query_tag_index(ctx, tag);
		size += bio_sizeof_gr(ctx->gr.opt_k, item_index);
		gr_update(&ctx->gr, item_index);
	} else {
		size += 0; /* no information needed */
	}

	return size;
}

size_t ctx_sizeof_tag(struct ctx *ctx, size_t tag)
{
	size_t size = 0;

	if (ctx->items > 1) {
		gr_recalc_k(&ctx->gr);
		size_t item_index = ctx_query_tag_index(ctx, tag);
		size += bio_sizeof_gr(ctx->gr.opt_k, item_index);
	} else {
		size += 0; /* no information needed */
	}

	return size;
}

/* encode dict[index].tag in context, rather than index */
void encode_tag(size_t context0, size_t context1, size_t context2, size_t index)
{
	assert(ctx1 != NULL);

	struct ctx *c1 = ctx1 + context1;
	struct ctx *c2 = ctx2 + context2;

	size_t tag = dict[index].tag;

	// tags: (context0, context1, tag)
	struct tag_pair ctx_pair = make_tag_pair(context0, context1);

	size_t ctx0_id = tag_pair_query(&ctx_pair);
	if (ctx0_id == (size_t)-1) {
		// not found context id
		ctx0_id = 0;
	}

	struct ctx *c0 = ctx0 + ctx0_id;

	// find best option

	int mode = 3;
	size_t size = 2 + bio_sizeof_gr(gr_dict.opt_k, index);

	if (ctx_query_tag_item(c0, tag) != NULL && 2 + ctx_sizeof_tag(c0, tag) < size) {
		mode = 0;
		size = 2 + ctx_sizeof_tag(c0, tag);
	}
	if (ctx_query_tag_item(c1, tag) != NULL && 2 + ctx_sizeof_tag(c1, tag) < size) {
		mode = 1;
		size = 2 + ctx_sizeof_tag(c1, tag);
	}
	if (ctx_query_tag_item(c2, tag) != NULL && 3 + ctx_sizeof_tag(c2, tag) < size) {
		mode = 2;
		size = 3 + ctx_sizeof_tag(c2, tag);
	}

	// encode
	switch (mode) {
		case 0:
			ctx0_hit++;
			stream_size += 2 + ctx_sizeof_tag(c0, tag); /* signal: hit (ctx1) + index (2 bit: 00) */
			stream_size_gr += 2 + ctx_sizeof_tag(c0, tag);
			stream_size_gr_hit0 += 2 + ctx_sizeof_tag(c0, tag);
			break;
		case 1:
			ctx1_hit++;
			stream_size += 2 + ctx_sizeof_tag(c1, tag); /* signal: hit (ctx1) + index (2 bit: 01) */
			stream_size_gr += 2 + ctx_sizeof_tag(c1, tag);
			stream_size_gr_hit1 += 2 + ctx_sizeof_tag(c1, tag);
			break;
		case 2:
			ctx2_hit++;
			stream_size += 3 + ctx_sizeof_tag(c2, tag); /* signal: hit (ctx2) + index (3 bits: 110) */
			stream_size_gr += 3 + ctx_sizeof_tag(c2, tag);
			stream_size_gr_hit2 += 3 + ctx_sizeof_tag(c2, tag);
			break;
		case 3:
			ctx_miss++;
			stream_size += 2 + bio_sizeof_gr(gr_dict.opt_k, index); /* signal: miss + index (2 bits: 10) */
			stream_size_gr += 2 + bio_sizeof_gr(gr_dict.opt_k, index);
			stream_size_gr_miss += 2 + bio_sizeof_gr(gr_dict.opt_k, index);
			break;
	}

	// update models in all contexts

	// mode = 0
	if (ctx_query_tag_item(c0, tag) != NULL) {
		ctx_encode_tag(c0, tag);
	}
	// mode = 1
	if (ctx_query_tag_item(c1, tag) != NULL) {
		ctx_encode_tag(c1, tag);
	}
	// mode = 2
	if (ctx_query_tag_item(c2, tag) != NULL) {
		ctx_encode_tag(c2, tag);
	}
	// mode = 3
	if (mode == 3) {
		update_model(&gr_dict, index);
	}

	// update contexts

	if (ctx_query_tag_item(c0, tag) == NULL) {
		ctx_add_tag(c0, tag);
		ctx_sort(c0);
	} else {
		ctx_item_inc_freq(c0, tag);
		ctx_sort(c0);
	}

	if (ctx_query_tag_item(c1, tag) == NULL) {
		ctx_add_tag(c1, tag);
		ctx_sort(c1);
	} else {
		ctx_item_inc_freq(c1, tag);
		ctx_sort(c1);
	}

	if (ctx_query_tag_item(c2, tag) == NULL) {
		ctx_add_tag(c2, tag);
		ctx_sort(c2);
	} else {
		ctx_item_inc_freq(c2, tag);
		ctx_sort(c2);
	}

	struct tag_pair pair = make_tag_pair(context1, tag);

	if (tag_pair_query(&pair) == (size_t)-1) {
		// add new context
		tag_pair_add(&pair);
	}
}

size_t make_context2(char *p)
{
	return (unsigned char)p[-1] | (((unsigned char)p[-2]) << 8);
}

void create()
{
	enlarge_dict();

	gr_init(&gr_dict, 11);

	for (size_t e = 0; e < 65536; ++e) {
		gr_init(&ctx2[e].gr, 0);
	}

	tag_pair_init();
}

void compress(char *ptr, size_t size)
{
	char *end = ptr + size;

	size_t context0 = 0; /* previous context1 */
	size_t context1 = 0; /* last tag */
	size_t context2 = 0; /* last two bytes */

	for (char *p = ptr; p < end; ) {
		/* (1) look into dictionary */
		size_t index = find_in_dictionary(p);

		if (index != (size_t)-1 && dict[index].len >= find_best_match(p)) {
			/* found in dictionary */
			size_t len = dict[index].len;

#if 0
			printf("[DEBUG] (match size %zu) incrementing [%zu] freq %zu\n", len, index, dict[index].freq);
#endif

			encode_tag(context0, context1, context2, index);

			context0 = context1;
			context1 = dict[index].tag;

			dict[index].last_pos = p;

			p += len;

			if (p >= ptr + 2) {
				context2 = make_context2(p);
			}

			/* recalc all costs, sort */
			update_dict(p);

			dict_hit_count++;
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

			context0 = 0;
			context1 = 0;

			if (p >= ptr + 2) {
				context2 = make_context2(p);
			}

			update_dict(p);

			dict_miss_count++;

			stream_size += 3 + MATCH_LOGSIZE + 8 * len; /* 3 bits: 111 */
			stream_size_raw += 3 + MATCH_LOGSIZE + 8 * len; /* 3 bits: 111 */
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

void destroy()
{
#if 0
	dump_dict();
#endif

	for (size_t e = 0; e < elems; ++e) {
		free(ctx1[e].arr);
	}
	for (size_t e = 0; e < 65536; ++e) {
		free(ctx2[e].arr);
	}
	free(ctx1);
	free(dict);

	for (size_t e = 0; e < tag_pair_elems; ++e) {
		free(ctx0[e].arr);
	}
	free(ctx0);

	tag_pair_free(map0);
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

	fclose(stream);

	create();

	compress(ptr, size);

	destroy();

	free(ptr);

	printf("input stream size: %zu\n", size);
	printf("output stream size: %zu\n", (stream_size + 7) / 8);
	printf("dictionary: hit %zu, miss %zu\n", dict_hit_count, dict_miss_count);

	printf("codestream size: dictionary %zu / %f%% (Golomb-Rice), uncompressed %zu / %f%% (of which raw %zu / %f%%)\n",
		(stream_size_gr + 7) / 8, 100.f * stream_size_gr / stream_size,
		(stream_size_raw + 7) / 8, 100.f * stream_size_raw / stream_size,
		(stream_size_raw_str + 7) / 8, 100.f * stream_size_raw_str / stream_size
	);

	printf("compression ratio: %f\n", size / (float)((stream_size_gr + stream_size_raw + 7) / 8));

	printf("contexts used: ctx0 %zu, ctx1 %zu, ctx2 %zu, no context %zu; uncompressed %zu\n", ctx0_hit, ctx1_hit, ctx2_hit, ctx_miss, dict_miss_count);
	printf("contexts size: ctx0 %f%%, ctx1 %f%%, ctx2 %f%%, no context %f%%\n",
		100.f * stream_size_gr_hit0 / stream_size,
		100.f * stream_size_gr_hit1 / stream_size,
		100.f * stream_size_gr_hit2 / stream_size,
		100.f * stream_size_gr_miss / stream_size
	);

	printf("context entries: ctx0 %zu, ctx1 %zu, ctx2 %zu\n", tag_pair_elems, elems, (size_t)65536);

	return 0;
}
