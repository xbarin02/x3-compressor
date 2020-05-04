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
#include "bio.h"
#include "context.h"

struct ctx *ctx0 = NULL; /* previous two tags */
struct ctx *ctx1 = NULL; /* previous tag */
struct ctx ctx2[65536];  /* last two bytes */
struct ctx ctx3[256];    /* last byte */

struct gr gr_idx1; /* for E_IDX1 */
struct gr gr_idx2; /* for E_IDX2 */

size_t stream_size_raw_str = 0;

void enlarge_ctx1()
{
	ctx1 = ctx_enlarge(ctx1, dict_get_size(), dict_get_elems());
}

void enlarge_ctx0()
{
	ctx0 = ctx_enlarge(ctx0, tag_pair_get_size(), tag_pair_get_elems());
}

/* unary codes for individual code-stream events */
#define SIZEOF_BITCODE_CTX1 1
#define SIZEOF_BITCODE_CTX0 2
#define SIZEOF_BITCODE_IDX1 3
#define SIZEOF_BITCODE_CTX2 4
#define SIZEOF_BITCODE_IDX2 5
#define SIZEOF_BITCODE_CTX3 6
#define SIZEOF_BITCODE_NEW  7
#define SIZEOF_BITCODE_EOF  8

/* list of events */
enum {
	E_CTX0 = 0, /* tag in ctx0 */
	E_CTX1 = 1, /* tag in ctx1 */
	E_CTX2 = 2, /* tag in ctx2 */
	E_CTX3 = 3, /* tag in ctx3 */
	E_IDX1 = 4, /* index in miss1 */
	E_IDX2 = 5, /* index miss2 */
	E_NEW  = 6  /* new index/tag (uncompressed) */
};

size_t events[7];

size_t sizes[7];

/* return index */
size_t decode_tag(size_t decision, struct bio *bio, size_t prev_context1, size_t context1, size_t context2, size_t pindex)
{
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

	int mode; /* for stats */
	size_t tag;
	size_t index;
	size_t size; /* for stats */
	switch (decision) {
		case SIZEOF_BITCODE_CTX0:
			tag = ctx_decode_tag_without_update(bio, c0);
			index = dict_get_index_by_tag(tag);
			mode = E_CTX0;
			size = SIZEOF_BITCODE_CTX0 + ctx_sizeof_tag(c0, tag);
			break;
		case SIZEOF_BITCODE_CTX1:
			tag = ctx_decode_tag_without_update(bio, c1);
			index = dict_get_index_by_tag(tag);
			mode = E_CTX1;
			size = SIZEOF_BITCODE_CTX1 + ctx_sizeof_tag(c1, tag);
			break;
		case SIZEOF_BITCODE_CTX2:
			tag = ctx_decode_tag_without_update(bio, c2);
			index = dict_get_index_by_tag(tag);
			mode = E_CTX2;
			size = SIZEOF_BITCODE_CTX2 + ctx_sizeof_tag(c2, tag);
			break;
		case SIZEOF_BITCODE_CTX3:
			tag = ctx_decode_tag_without_update(bio, c3);
			index = dict_get_index_by_tag(tag);
			mode = E_CTX3;
			size = SIZEOF_BITCODE_CTX3 + ctx_sizeof_tag(c3, tag);
			break;
		case SIZEOF_BITCODE_IDX1:
			index = (size_t)bio_read_gr(bio, gr_idx1.opt_k);
			tag = dict_get_tag_by_index(index);
			mode = E_IDX1;
			size = SIZEOF_BITCODE_IDX1 + gr_sizeof_symb(&gr_idx1, index);
			break;
		case SIZEOF_BITCODE_IDX2:
			index = (size_t)bio_read_gr(bio, gr_idx2.opt_k) + pindex;
			tag = dict_get_tag_by_index(index);
			mode = E_IDX2;
			size = SIZEOF_BITCODE_IDX2 + gr_sizeof_symb(&gr_idx2, index - pindex);
			break;
		default:
			abort();
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
			enlarge_ctx0();
		}
		tag_pair_add(&pair);
	}

	return index;
}

/* encode dict[index].tag in context, rather than index */
void encode_tag(struct bio *bio, size_t prev_context1, size_t context1, size_t context2, size_t index, size_t pindex)
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
			bio_write_gr(bio, 0, SIZEOF_BITCODE_CTX0 - 1);
			ctx_encode_tag_without_update(bio, c0, tag);
			break;
		case E_CTX1:
			bio_write_gr(bio, 0, SIZEOF_BITCODE_CTX1 - 1);
			ctx_encode_tag_without_update(bio, c1, tag);
			break;
		case E_CTX2:
			bio_write_gr(bio, 0, SIZEOF_BITCODE_CTX2 - 1);
			ctx_encode_tag_without_update(bio, c2, tag);
			break;
		case E_CTX3:
			bio_write_gr(bio, 0, SIZEOF_BITCODE_CTX3 - 1);
			ctx_encode_tag_without_update(bio, c3, tag);
			break;
		case E_IDX1:
			bio_write_gr(bio, 0, SIZEOF_BITCODE_IDX1 - 1);
			assert(index <= UINT32_MAX);
			bio_write_gr(bio, gr_idx1.opt_k, (uint32_t)index);
			break;
		case E_IDX2:
			bio_write_gr(bio, 0, SIZEOF_BITCODE_IDX2 - 1);
			assert(index - pindex <= UINT32_MAX);
			bio_write_gr(bio, gr_idx2.opt_k, (uint32_t)(index - pindex));
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
			enlarge_ctx0();
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
	enlarge_ctx0();
}

void encode_match(struct bio *bio, char *p, size_t len)
{
	bio_write_gr(bio, 0, SIZEOF_BITCODE_NEW - 1);

	assert(len > 0 && len <= (1 << MATCH_LOGSIZE));

	bio_write_bits(bio, (uint32_t)(len - 1), MATCH_LOGSIZE);

	for (size_t c = 0; c < len; ++c) {
		bio_write_bits(bio, (uint32_t)*(p + c), 8);
	}
}

void decode_match(struct bio *bio, char *p, size_t *p_len)
{
	*p_len = (size_t)(bio_read_bits(bio, MATCH_LOGSIZE) + 1);

	for (size_t c = 0; c < *p_len; ++c) {
		*(p + c) = (char)bio_read_bits(bio, 8);
	}
}

char *decompress(char *ptr, struct bio *bio)
{
	size_t prev_context1 = 0; /* previous context1 */
	size_t context1 = 0; /* last tag */
	size_t context2 = 0; /* last two bytes */
	size_t pindex = (size_t)-1; /* previous index */

	char *p = ptr;

	for (;;) {
		size_t decision = (size_t)(bio_read_gr(bio, 0) + 1);

		if (decision == SIZEOF_BITCODE_EOF) {
			break;
		} else if (decision == SIZEOF_BITCODE_NEW) {
			/* new match */

			size_t len;

			decode_match(bio, p, &len);

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
		} else {
			/* in dictionary */

			size_t index = decode_tag(decision, bio, prev_context1, context1, context2, pindex);

			size_t len = dict_get_len_by_index(index);

			prev_context1 = context1;
			context1 = dict_get_tag_by_index(index);

			dict_set_last_pos(index, p);

			/* put uncompressed fragment */
			const char *s = dict_get_str_by_index(index);
			for (size_t c = 0; c < len; ++c) {
				p[c] = s[c];
			}

			p += len;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			/* recalc all costs, sort */
			dict_update_costs(p);

			pindex = index;
		}
	}

	return p;
}

void compress(char *ptr, size_t size, struct bio *bio)
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

			encode_tag(bio, prev_context1, context1, context2, index, pindex);

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

			encode_match(bio, p, len);

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

	/* signal end of input */
	bio_write_gr(bio, 0, SIZEOF_BITCODE_EOF - 1);
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

enum {
	COMPRESS,
	DECOMPRESS
};


int main(int argc, char *argv[])
{
	int mode = COMPRESS;

	parse: switch (getopt(argc, argv, "zdht:w:T:")) {
		case 'z':
			mode = COMPRESS;
			goto parse;
		case 'd':
			mode = DECOMPRESS;
			goto parse;
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

	FILE *istream = NULL, *ostream = NULL;

	switch (argc - optind) {
		case 0:
			istream = fopen("enwik8", "r");
			ostream = fopen("output.bit", "w");
			break;
		case 1:
			istream = fopen(argv[optind], "r");
			ostream = fopen("output.bit", "w");
			break;
		case 2:
			istream = fopen(argv[optind + 0], "r");
			ostream = fopen(argv[optind + 1], "w");
			break;
		default:
			fprintf(stderr, "Unexpected argument\n");
			abort();
	}

	fprintf(stderr, "%s\n", mode == COMPRESS ? "Compressing..." : "Decompressing...");

	if (istream == NULL) {
		fprintf(stderr, "Cannot open input file\n");
		abort();
	}

	if (ostream == NULL) {
		fprintf(stderr, "Cannot open output file\n");
		abort();
	}

	create();

	struct bio bio;

	/* uncompressed size */
	size_t size;

	if (mode == COMPRESS) {
		printf("max match count: %i\n", get_max_match_count());
		printf("forward window: %zu\n", get_forward_window());
		printf("threads: %i\n", get_num_threads());

		size_t isize = fsize(istream);

		char *iptr = malloc(isize + get_forward_window());
		char *optr = malloc(isize * 2); /* at most 1 : 2 ratio */

		if (iptr == NULL) {
			abort();
		}

		if (optr == NULL) {
			abort();
		}

		memset(iptr + isize, 0, get_forward_window());
		fload(iptr, isize, istream);

		bio_open(&bio, optr, optr + isize * 2, BIO_MODE_WRITE);

		long start = wall_clock();

		compress(iptr, isize, &bio);

		printf("elapsed time: %f\n", (wall_clock() - start) / (float)1000000000L);

		bio_close(&bio, BIO_MODE_WRITE);

		char *end = (char *)bio.ptr;

		size = isize;
		fsave(optr, end - optr, ostream);

		free(iptr);
		free(optr);
	} else {
		size_t isize = fsize(istream);

		char *iptr = malloc(isize);
		char *optr = malloc(64 * isize); /* at most 64 : 1 ratio */

		if (iptr == NULL) {
			abort();
		}

		if (optr == NULL) {
			abort();
		}

		fload(iptr, isize, istream);

		unsigned char *iend = (unsigned char *)iptr + isize;

		bio_open(&bio, iptr, iend, BIO_MODE_READ);

		long start = wall_clock();

		char *oend = decompress((char *)optr, &bio);

		printf("elapsed time: %f\n", (wall_clock() - start) / (float)1000000000L);

		bio_close(&bio, BIO_MODE_READ);

		size = oend - optr;
		fsave(optr, size, ostream);

		free(iptr);
		free(optr);
	}

	destroy();

	fclose(istream);
	fclose(ostream);

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
