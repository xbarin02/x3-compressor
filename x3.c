#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include "backend.h"
#include "file.h"
#include "dict.h"
#include "gr.h"
#include "tag_pair.h"
#include "utils.h"

struct item {
	size_t tag;
	size_t freq; /* used n-times */
};

struct ctx {
	size_t items; /* allocated elements */
	struct item *arr; /* pointer to the first item */

	struct gr gr;
};

struct ctx *ctx0 = NULL; /* previous two tags */
struct ctx *ctx1 = NULL; /* previous tag */
struct ctx ctx2[65536];  /* last two bytes */
struct ctx ctx3[256];    /* last byte */

struct gr gr_idx1; /* for E_IDX1 */
struct gr gr_idx2; /* for E_IDX2 */

size_t stream_size_raw_str = 0;

void enlarge_ctx1()
{
	ctx1 = realloc(ctx1, dict_get_size() * sizeof(struct ctx));

	if (ctx1 == NULL) {
		abort();
	}

	/* dict_get_elems has been already incremented */

	memset(ctx1 + dict_get_elems(), 0, (dict_get_size() - dict_get_elems()) * sizeof(struct ctx));

	for (size_t e = dict_get_elems(); e < dict_get_size(); ++e) {
		gr_init(&ctx1[e].gr, 0);
	}
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
		size += gr_sizeof_symb(&ctx->gr, item_index);
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
		size += gr_sizeof_symb(&ctx->gr, item_index);
	} else {
		size += 0; /* no information needed */
	}

	return size;
}

void ctx0_realloc()
{
	ctx0 = realloc(ctx0, tag_pair_get_size() * sizeof(struct ctx));

	if (ctx0 == NULL) {
		abort();
	}

	memset(ctx0 + tag_pair_get_elems(), 0, (tag_pair_get_size() - tag_pair_get_elems()) * sizeof(struct ctx));
}

#define SIZEOF_BITCODE_CTX0 2
#define SIZEOF_BITCODE_CTX1 1
#define SIZEOF_BITCODE_CTX2 4
#define SIZEOF_BITCODE_CTX3 6
#define SIZEOF_BITCODE_IDX1 3
#define SIZEOF_BITCODE_IDX2 5
#define SIZEOF_BITCODE_NEW  6

/* list of events */
enum {
	E_CTX0 = 0, /* tag in ctx0 */
	E_CTX1 = 1, /* tag in ctx1 */
	E_CTX2 = 2, /* tag in ctx2 */
	E_CTX3 = 3, /* tag in ctx3 */
	E_IDX1 = 4, /* index in miss1 */
	E_IDX2 = 5, /* index miss2 */
	E_NEW = 6   /* new index/tag (uncompressed) */
};

size_t events[7];

size_t sizes[7];

/* encode dict[index].tag in context, rather than index */
void encode_tag(size_t prev_context1, size_t context1, size_t context2, size_t index, size_t pindex)
{
	assert(ctx1 != NULL);

	size_t tag = dict_get_tag_by_index(index);

	// order of tags is (prev_context1, context1, tag)
	struct tag_pair ctx_pair = make_tag_pair(prev_context1, context1); /* previous two tags */

	size_t ctx0_id = tag_pair_query(&ctx_pair); /* convert (prev_context1, context1) to linear id */
	if (ctx0_id == (size_t)-1) {
		// not found context id, default to 0
		ctx0_id = 0;
	}

	struct ctx *c0 = ctx0 + ctx0_id;
	struct ctx *c1 = ctx1 + context1;
	struct ctx *c2 = ctx2 + context2;
	struct ctx *c3 = ctx3 + (context2 & 255);

	// find the best option

	int mode = E_IDX1;
	size_t size = SIZEOF_BITCODE_IDX1 + gr_sizeof_symb(&gr_idx1, index);

	if (ctx_query_tag_item(c0, tag) != NULL && SIZEOF_BITCODE_CTX0 + ctx_sizeof_tag(c0, tag) < size) {
		mode = E_CTX0;
		size = SIZEOF_BITCODE_CTX0 + ctx_sizeof_tag(c0, tag);
	}
	if (ctx_query_tag_item(c1, tag) != NULL && SIZEOF_BITCODE_CTX1 + ctx_sizeof_tag(c1, tag) < size) {
		mode = E_CTX1;
		size = SIZEOF_BITCODE_CTX1 + ctx_sizeof_tag(c1, tag);
	}
	if (ctx_query_tag_item(c2, tag) != NULL && SIZEOF_BITCODE_CTX2 + ctx_sizeof_tag(c2, tag) < size) {
		mode = E_CTX2;
		size = SIZEOF_BITCODE_CTX2 + ctx_sizeof_tag(c2, tag);
	}
	if (ctx_query_tag_item(c3, tag) != NULL && SIZEOF_BITCODE_CTX3 + ctx_sizeof_tag(c3, tag) < size) {
		mode = E_CTX3;
		size = SIZEOF_BITCODE_CTX3 + ctx_sizeof_tag(c3, tag);
	}
	if (pindex != (size_t)-1 && index >= pindex && SIZEOF_BITCODE_IDX2 + gr_sizeof_symb(&gr_idx2, index - pindex) < size) {
		mode = E_IDX2;
		size = SIZEOF_BITCODE_IDX2 + gr_sizeof_symb(&gr_idx2, index - pindex);
	}

	// encode

	switch (mode) {
		case E_CTX0:
			break;
		case E_CTX1:
			break;
		case E_CTX2:
			break;
		case E_CTX3:
			break;
		case E_IDX1:
			break;
		case E_IDX2:
			break;
	}

	events[mode]++;
	sizes[mode] += size;

	// update Golomb-Rice models

	// mode = E_CTX0
	if (ctx_query_tag_item(c0, tag) != NULL) {
		ctx_encode_tag(c0, tag);
	}
	// mode = E_CTX1
	if (ctx_query_tag_item(c1, tag) != NULL) {
		ctx_encode_tag(c1, tag);
	}
	// mode = E_CTX2
	if (ctx_query_tag_item(c2, tag) != NULL) {
		ctx_encode_tag(c2, tag);
	}
	// mode = E_CTX3
	if (ctx_query_tag_item(c3, tag) != NULL) {
		ctx_encode_tag(c3, tag);
	}
	// mode = E_IDX1
	if (mode == E_IDX1) {
		gr_update_model(&gr_idx1, index);
	}
	// mode = E_IDX2
	if (mode == E_IDX2) {
		gr_update_model(&gr_idx2, index - pindex);
	}

	// update contexts

	if (ctx_query_tag_item(c0, tag) == NULL) {
		ctx_add_tag(c0, tag);
	} else {
		ctx_item_inc_freq(c0, tag);
	}
	ctx_sort(c0);

	if (ctx_query_tag_item(c1, tag) == NULL) {
		ctx_add_tag(c1, tag);
	} else {
		ctx_item_inc_freq(c1, tag);
	}
	ctx_sort(c1);

	if (ctx_query_tag_item(c2, tag) == NULL) {
		ctx_add_tag(c2, tag);
	} else {
		ctx_item_inc_freq(c2, tag);
	}
	ctx_sort(c2);

	if (ctx_query_tag_item(c3, tag) == NULL) {
		ctx_add_tag(c3, tag);
	} else {
		ctx_item_inc_freq(c3, tag);
	}
	ctx_sort(c3);

	/* (context1, tag) constitutes new pair of tags */

	struct tag_pair pair = make_tag_pair(context1, tag);

	if (tag_pair_query(&pair) == (size_t)-1) {
		// add new context
		if (!tag_pair_can_add()) {
			tag_pair_enlarge();
			ctx0_realloc();
		}
		tag_pair_add(&pair);
	}
}

size_t make_context2(char *p)
{
	return (unsigned char)p[1] | (((unsigned char)p[0]) << 8);
}

void create()
{
	dict_enlarge();
	enlarge_ctx1();

	gr_init(&gr_idx1, 6);
	gr_init(&gr_idx2, 0);

	for (size_t e = 0; e < 65536; ++e) {
		gr_init(&ctx2[e].gr, 0);
	}

	for (size_t e = 0; e < 256; ++e) {
		gr_init(&ctx3[e].gr, 0);
	}

	tag_pair_enlarge();
	ctx0_realloc();
}

void compress(char *ptr, size_t size)
{
	char *end = ptr + size;

	size_t prev_context1 = 0; /* previous context1 */
	size_t context1 = 0; /* last tag */
	size_t context2 = 0; /* last two bytes */
	size_t pindex = (size_t)-1; /* previous index */

	for (char *p = ptr; p < end; ) {
		/* (1) look into dictionary */
		size_t index = dict_find_match(p);

		if (index != (size_t)-1 && dict_get_len_by_index(index) >= find_best_match(p)) {
			/* found in dictionary */
			size_t len = dict_get_len_by_index(index);

#if 0
			printf("[DEBUG] (match size %zu) incrementing [%zu] freq %zu\n", len, index, dict[index].freq);
#endif

			encode_tag(prev_context1, context1, context2, index, pindex);

			prev_context1 = context1;
			context1 = dict_get_tag_by_index(index);

			dict_set_last_pos(index, p);

			p += len;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			/* recalc all costs, sort */
			dict_update_costs(p);

			pindex = index;
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
			elem_fill(&e, p, len);

			assert(dict_query_elem(&e) == 0);

			if (!dict_can_insert_elem()) {
				dict_enlarge();
				enlarge_ctx1();
			}

			dict_insert_elem(&e);

			p += len;

			prev_context1 = 0;
			context1 = 0;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			dict_update_costs(p);

			pindex = (size_t)-1;

			events[E_NEW]++;

			sizes[E_NEW] += SIZEOF_BITCODE_NEW + MATCH_LOGSIZE + 8 * len;
			stream_size_raw_str += 8 * len;
		}
	}
}

void destroy()
{
#if 0
	dict_dump();
#endif

	for (size_t e = 0; e < dict_get_elems(); ++e) {
		free(ctx1[e].arr);
	}
	free(ctx1);
	dict_destroy();

	for (size_t e = 0; e < 65536; ++e) {
		free(ctx2[e].arr);
	}

	for (size_t e = 0; e < 256; ++e) {
		free(ctx3[e].arr);
	}

	for (size_t e = 0; e < tag_pair_get_elems(); ++e) {
		free(ctx0[e].arr);
	}
	free(ctx0);
	tag_pair_destroy();
}

int main(int argc, char *argv[])
{
	parse: switch (getopt(argc, argv, "ht:w:T:")) {
		case 'h':
			// print_help(argv[0]);
			return 0;
		case 't':
			set_max_match_count(atoi(optarg));
			goto parse;
		case 'w':
			set_forward_window(atoi(optarg) * 1024);
			goto parse;
		case 'T':
			set_num_threads(atoi(optarg));
			goto parse;
		default:
			abort();
		case -1:
		;
	}

	char *path;

	switch (argc - optind) {
		case 0:
			path = "enwik6";
			break;
		case 1:
			path = argv[optind];
			break;
		default:
			fprintf(stderr, "Unexpected argument\n");
			abort();
	}

	printf("max match count: %i\n", get_max_match_count());
	printf("forward window: %zu\n", get_forward_window());
	printf("threads: %i\n", get_num_threads());

	printf("path: %s\n", path);

	FILE *stream = fopen(path, "r");

	if (stream == NULL) {
		abort();
	}

	size_t size = fsize(stream);

	char *ptr = malloc(size + get_forward_window());

	if (ptr == NULL) {
		abort();
	}

	memset(ptr + size, 0, get_forward_window());

	fload(ptr, size, stream);

	fclose(stream);

	create();

	long start = wall_clock();

	compress(ptr, size);

	printf("elapsed time: %f\n", (wall_clock() - start) / (float)1000000000L);

	destroy();

	free(ptr);

	size_t dict_hit_count = events[E_CTX0] + events[E_CTX1] + events[E_CTX2] + events[E_CTX3] + events[E_IDX1] + events[E_IDX2];

	size_t stream_size_gr = sizes[E_CTX0] + sizes[E_CTX1] + sizes[E_CTX2] + sizes[E_CTX3] + sizes[E_IDX1] + sizes[E_IDX2];
	size_t stream_size = sizes[E_CTX0] + sizes[E_CTX1] + sizes[E_CTX2] + sizes[E_CTX3] + sizes[E_IDX1] + sizes[E_IDX2] + sizes[E_NEW];

	printf("input stream size: %zu\n", size);
	printf("output stream size: %zu\n", (stream_size + 7) / 8);
	printf("dictionary: hit %zu, miss %zu\n", dict_hit_count, events[E_NEW]);

	printf("codestream size: dictionary %zu / %f%% (Golomb-Rice), new %zu / %f%% (of which text %zu / %f%%)\n",
		(stream_size_gr + 7) / 8, 100.f * stream_size_gr / stream_size,
		(sizes[E_NEW] + 7) / 8, 100.f * sizes[E_NEW] / stream_size,
		(stream_size_raw_str + 7) / 8, 100.f * stream_size_raw_str / stream_size
	);

#if 1
	printf("\x1b[37;1mcompression ratio: %f\x1b[0m\n", size / (float)((stream_size_gr + sizes[E_NEW] + 7) / 8));
#else
	printf("compression ratio: %f\n", size / (float)((stream_size_gr + sizes[E_NEW] + 7) / 8));
#endif

	printf("number of events: ctx0 %zu, ctx1 %zu, ctx2 %zu, ctx3 %zu, miss1 %zu, miss2 %zu, new %zu\n",
		events[E_CTX0], events[E_CTX1], events[E_CTX2], events[E_CTX3], events[E_IDX1], events[E_IDX2], events[E_NEW]);
	printf("contexts size: ctx0 %f%%, ctx1 %f%%, ctx2 %f%%, ctx3 %f%%, miss1 %f%%, miss2 %f%%, new %f%%\n",
		100.f * sizes[E_CTX0] / stream_size,
		100.f * sizes[E_CTX1] / stream_size,
		100.f * sizes[E_CTX2] / stream_size,
		100.f * sizes[E_CTX3] / stream_size,
		100.f * sizes[E_IDX1] / stream_size,
		100.f * sizes[E_IDX2] / stream_size,
		100.f * sizes[E_NEW] / stream_size
	);

	printf("context entries: ctx0 %zu, ctx1 %zu, ctx2 %zu, ctx3 %zu\n", tag_pair_get_elems(), dict_get_elems(), (size_t)65536, (size_t)256);

	return 0;
}
