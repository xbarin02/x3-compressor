#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

void dump_dict();

size_t popcount_sz(size_t n)
{
	switch (sizeof(size_t)) {
		case sizeof(unsigned int): return __builtin_popcount(n);
		case sizeof(unsigned long): return __builtin_popcountl(n);
		default: __builtin_trap();
	}
}

size_t copymsb_sz(size_t n)
{
	size_t shift = 1;

	while (shift < sizeof(size_t) * CHAR_BIT) {
		n |= n >> shift;
		shift <<= 1;
	}

	return n;
}

size_t log2_sz(size_t n)
{
	--n;

	n = copymsb_sz(n);

	return popcount_sz(n);
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

/* search buffers */
#define FORWARD_WINDOW (8 * 1024)
#define BACKWARD_WINDOW (FORWARD_WINDOW * 128)

/* log. size */
#define MATCH_LOGSIZE 4

/* the look-ahead buffer */
#define MAX_MATCH_LEN (1 << MATCH_LOGSIZE)

size_t find_best_match(char *p)
{
	char *end = p + FORWARD_WINDOW;

	/* tc = 3 found empirically */
	for (int tc = 3; tc > 0; --tc) {
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
	char s[MAX_MATCH_LEN * /*HACK*/2]; /* the string */
	size_t len; /* of the length */
	size_t freq; /* that was already used n-times */
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
};

/* allocated size, enlarged logarithmically */
size_t dict_logsize = 0;
size_t dict_size = 1;

struct elem *dict = NULL;
struct ctx *ctx = NULL;

/* number of elements */
size_t elems = 0;

void enlarge_dict()
{
	dict_logsize++;
	dict_size = (size_t)1 << dict_logsize;

	dict = realloc(dict, dict_size * sizeof(struct elem));

	if (dict == NULL) {
		abort();
	}

	memset(dict + elems, 0, (dict_size - elems) * sizeof(struct elem));

	ctx = realloc(ctx, dict_size * sizeof(struct ctx));

	if (ctx == NULL) {
		abort();
	}

	memset(ctx + elems, 0, (dict_size - elems) * sizeof(struct ctx));
}

size_t calc_cost(struct elem *e, char *curr_pos)
{
	assert(e != NULL);

	assert(curr_pos >= e->last_pos);

	size_t dist = curr_pos - e->last_pos;
	size_t freq = e->freq;

	assert(freq > 0);

	/* cost function to be optimized */
	size_t cost = 256 * freq;

	if (dist < BACKWARD_WINDOW + MAX_MATCH_LEN + 1) {
		cost += 1 * (BACKWARD_WINDOW + MAX_MATCH_LEN + 1 - dist);
	}

	return cost;
}

void fill_elem(struct elem *e, char *p, size_t len)
{
	assert(e != NULL);

	memcpy(e->s, p, len);
	e->len = len;

	e->freq = 1;
	e->last_pos = p;

	e->cost = calc_cost(e, p);
}

static int cmp(const void *l, const void *r)
{
	const struct elem *le = l;
	const struct elem *re = r;

	if (le->cost > re->cost) {
		return -1;
	}

	if (le->cost < re->cost) {
		return +1;
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

	if (is_zero(&dict[elems])) {
		dict[elems] = *e;
		dict[elems].tag = elems; /* element is filled except a tag, set the tag */
		elems++;
	} else {
		printf("WARN rewritting non-zero entry\n");
		abort();
	}
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

static size_t tag_match_count = 0;
static size_t tag_newentry_count = 0;
static size_t stream_size = 0;
static size_t stream_size_tag = 0;
static size_t stream_size_raw = 0;
static size_t stream_size_raw_str = 0;

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
		assert(dict[0].cost >= dict[1].cost);
		assert(dict[elems-2].cost >= dict[elems-1].cost);
	}
}

size_t stream_size_gr = 0;

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

static void update_model(size_t delta)
{
	if (symbol_count == RESET_INTERVAL) {
		int k;

		for (k = 1; (symbol_count << k) <= symbol_sum; ++k)
			;

		opt_k = k - 1;

		symbol_count = 0;
		symbol_sum = 0;
	}

	symbol_sum += delta;
	symbol_count++;
}

int ctx_query_tag(struct ctx *c, size_t tag)
{
	for (size_t i = 0; i < c->items; ++i) {
		if (c->arr[i].tag == tag) {
			return 1;
		}
	}

	return 0;
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
size_t ctx_hit = 0;

/* encode dict[index].tag in context, rather than index */
void encode_tag(size_t context, size_t index)
{
	assert(ctx != NULL);

	struct ctx *c = ctx + context;

	stream_size_gr += 1; /* decision to use dictionary */

#if 1
	if (ctx_query_tag(c, dict[index].tag)) {
		ctx_hit++;
#	if 1
		//stream_size_gr += bio_sizeof_gr(opt_k, 0);
		//update_model(0);
		stream_size_gr += 1 + log2_sz(c->items); /* hit + index */
		// printf("log2(items) = %zu\n", log2_sz(c->items));
#	endif
	} else {
		ctx_miss++;
		ctx_add_tag(c, dict[index].tag);
#	if 1
		stream_size_gr += 1 + bio_sizeof_gr(opt_k, index); /* miss + index */
		update_model(index);
#	endif
	}
#else
	stream_size_gr += bio_sizeof_gr(opt_k, index); /* +1 due to decision */
	update_model(index);
#endif
}

void add_concatenated_words(size_t context_tag, size_t index)
{
	// find context_index
	size_t context_index = (size_t)-1;
	for (size_t i = 0; i < elems; ++i) {
		if (dict[i].tag == context_tag) {
			context_index = i;
		}
	}

	if (context_index == (size_t)-1) {
		abort();
	}

	// if len() + len() is too large, return
	if (dict[context_index].len + dict[index].len > MAX_MATCH_LEN * 2) {
		printf("not enough space\n");
		return;
	}

	// fill struct elem
	struct elem e;

	memcpy(e.s, dict[context_index].s, dict[context_index].len);
	memcpy(e.s + dict[context_index].len, dict[index].s, dict[index].len);

	e.len = dict[context_index].len + dict[index].len;

	e.freq = 1;

	e.last_pos = dict[context_index].last_pos;

	// insert new element
	insert_elem(&e);

	// invoke update_dict()
}

void compress(char *ptr, size_t size, FILE *rawstream)
{
	char *end = ptr + size;

	size_t context = 0; /* last tag */

	for (char *p = ptr; p < end; ) {
#if 0
		printf("find %p / %p\n", p, end);
#endif
		/* (1) look into dictionary */
		size_t index = find_in_dictionary(p);

		if (index != (size_t)-1 && dict[index].len >= find_best_match(p)) {
			/* found */
			size_t len = dict[index].len;

			/* increment freq, recalc all costs, sort */
#if 0
			printf("[DEBUG] (match size %zu) incrementing [%zu] freq %zu\n", len, index, dict[index].freq);
#endif

			encode_tag(context, index); /* FIXME: add context */

			context = dict[index].tag;

			dict[index].freq++;
			dict[index].last_pos = p;

			p += len;
#if 0
			add_concatenated_words(context, index);
#endif
			update_dict(p);

			tag_match_count++;

			stream_size += 1 + log2_sz(elems);
			stream_size_tag += 1 + log2_sz(elems);
		} else {
			/* (2) else find best match and insert it into dictionary */
			size_t len = find_best_match(p);
#if 0
			printf("[DEBUG] new match len %zu\n", len);
#endif

			if (fwrite(p, len, 1, rawstream) < 1) {
				abort();
			}

			struct elem e;
			fill_elem(&e, p, len);
			insert_elem(&e);

			p += len;

			update_dict(p);

			tag_newentry_count++;

			stream_size += 1 + MATCH_LOGSIZE + 8*len;
			stream_size_raw += 1 + MATCH_LOGSIZE + 8*len;
			stream_size_raw_str += 8*len;
		}
	}
}

void dump_dict()
{
	for (size_t i = 0; i < elems; ++i) {
		printf("dict[%zu] = \"%.*s\" (freq=%zu len=%zu)\n", i, (int)dict[i].len, dict[i].s, dict[i].freq, dict[i].len);
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

	FILE *rawstream = fopen("stream2", "w");

	if (rawstream == NULL) {
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

	compress(ptr, size, rawstream);

	fclose(rawstream);

	printf("tags: match %zu, new entry %zu\n", tag_match_count, tag_newentry_count);
	printf("input stream: %zu\n", size);
	printf("est. stream size: %zu (tags %zu / %f%%, uncompressed %zu / %f%%)\n",
		stream_size/8,
		stream_size_gr/8, 100.f*stream_size_gr/(stream_size_gr + stream_size_raw),
		stream_size_raw/8, 100.f*stream_size_raw/(stream_size_gr + stream_size_raw)
	);
	printf("Golomb-Rice stream size: %zu\n", stream_size_gr/8);
	printf("uncompressed raw output: %zu\n", stream_size_raw_str/8);
	printf("ratio: %f\n", size / (float)((stream_size_gr + stream_size_raw)/8));

	printf("contexts: hit=%zu miss=%zu\n", ctx_hit, ctx_miss);

#if 0
	dump_dict();
#endif

	return 0;
}
