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

	/* FIXME tc = 1, 2, 3, ... optimize */
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
	char s[MAX_MATCH_LEN]; /* the string */
	size_t len; /* of the length */
	size_t freq; /* that was already used n-times */
	char *last_pos; /* recently seen at the position */
	size_t cost; /* sort key */
};

/* allocated size, enlarged logarithmically */
size_t dict_logsize = 0;
size_t dict_size = 1;

struct elem *dict = NULL;

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
}

size_t calc_cost(struct elem *e, char *curr_pos)
{
	assert(e != NULL);

	assert(curr_pos >= e->last_pos);

	size_t dist = curr_pos - e->last_pos;
	size_t len = e->len;
	size_t freq = e->freq;

	assert(freq > 0);

	assert(dist < BACKWARD_WINDOW + MAX_MATCH_LEN + 1);

	/* cost function to be optimized */
	size_t cost = 0 * len + 256 * freq + 1 * (BACKWARD_WINDOW + MAX_MATCH_LEN + 1 - dist);

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

	if (is_zero(e)) {
		printf("ERR inserting zero element!\n");
		abort();
	}

	if (elems >= dict_size) {
		enlarge_dict();
	}

	assert(elems < dict_size);

	if (is_zero(&dict[elems])) {
		dict[elems] = *e;
		elems++;
	} else {
		printf("WARN rewritting non-zero entry\n");
		abort();
	}
}

size_t find_in_dictionary(const char *p)
{
#if 0
	printf("find_in_dictionary: %zu elems\n", elems);
#endif

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

	size_t removed_entries = 0;

	for (size_t i = 0; i < elems; ++i) {
		if (is_zero(&dict[i])) {
			printf("ERR zero entry in dictionary!\n");
		}

		dict[i].cost = calc_cost(&dict[i], p);

		assert(p >= dict[i].last_pos);
		size_t dist = p - dict[i].last_pos;
		if (dist > BACKWARD_WINDOW) {
			if (is_zero(&dict[i])) {
				printf("ERR zeroing zero entry [%zu/%zu] freq=%zu dist=%zu\n", i, elems, dict[i].freq, dist);
				printf("ERR zeroing zero entry\n");
				abort();
			} else {
				dict[i] = e0;
				removed_entries++;
			}
		}
	}

	qsort(dict, elems, sizeof(struct elem), cmp);

	assert(elems >= removed_entries);
	elems -= removed_entries;
#if 0
	printf("updated: %zu entries (removed %zu)\n", elems, removed_entries);
#endif
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

void encode_tag(size_t index)
{
	stream_size_gr += 1 + bio_sizeof_gr(opt_k, index); /* +1 due to decision */

	update_model(index);
}

void compress(char *ptr, size_t size, FILE *rawstream)
{
	char *end = ptr + size;

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

			encode_tag(index);

			dict[index].freq++;
			dict[index].last_pos = p;

			p += len;

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

int main()
{
	FILE *stream = fopen("enwik6", "r");

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

#if 0
	dump_dict();
#endif

	return 0;
}
