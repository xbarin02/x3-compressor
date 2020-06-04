#define _POSIX_C_SOURCE 2
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include "backend.h"
#include "file.h"
#include "dict.h"
#include "tag_pair.h"
#include "utils.h"
#include "bio.h"
#include "context.h"
#include "ac.h"

struct ctx *ctx0 = NULL; /* previous two tags */
struct ctx *ctx1 = NULL; /* previous tag */
struct ctx ctx2[65536];  /* last two bytes */

void enlarge_ctx1()
{
	ctx1 = ctx_enlarge(ctx1, dict_get_size(), dict_get_elems());
}

void enlarge_ctx0()
{
	ctx0 = ctx_enlarge(ctx0, tag_pair_get_size(), tag_pair_get_elems());
}

/* list of events */
enum {
	E_CTX0 = 0, /* tag in ctx0 */
	E_CTX1 = 1, /* tag in ctx1 */
	E_CTX2 = 2, /* tag in ctx2 */
	E_IDX1 = 3, /* index in miss1 */
	E_NEW  = 4, /* new index/tag (uncompressed) */
	E_EOF  = 5  /* end of stream */
};

size_t events[6];
float sizes[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };

struct ac ac;

struct model model_events;
struct model model_match_size;
struct model model_chars;
struct model model_index1;

float prob_to_bits(float prob)
{
	return -log2f(prob);
}

/* return index */
size_t decode_tag(size_t decision, struct bio *bio, size_t prev_context1, size_t context1, size_t context2)
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

	size_t tag;
	size_t index;
	float size; /* for stats */
	switch (decision) {
		case E_CTX0:
			tag = ctx_decode_tag_without_update_ac(bio, &ac, c0);
			size = prob_to_bits(ctx_encode_tag_without_update_ac_query_prob(c0, tag));
			index = dict_get_index_by_tag(tag);
			break;
		case E_CTX1:
			tag = ctx_decode_tag_without_update_ac(bio, &ac, c1);
			size = prob_to_bits(ctx_encode_tag_without_update_ac_query_prob(c1, tag));
			index = dict_get_index_by_tag(tag);
			break;
		case E_CTX2:
			tag = ctx_decode_tag_without_update_ac(bio, &ac, c2);
			size = prob_to_bits(ctx_encode_tag_without_update_ac_query_prob(c2, tag));
			index = dict_get_index_by_tag(tag);
			break;
		case E_IDX1:
			index = ac_decode_symbol_model(&ac, bio, &model_index1);
			size = prob_to_bits(ac_encode_symbol_model_query_prob(index, &model_index1));
			inc_model(&model_index1, index);
			tag = dict_get_tag_by_index(index);
			break;
		default:
			abort();
	}

	events[decision]++;
	sizes[decision] += size;

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
void encode_tag(struct bio *bio, size_t prev_context1, size_t context1, size_t context2, size_t index)
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

	// find the best option

	float prob_ctx0 = 0;
	if (ctx_query_tag_item(c0, tag) != NULL) {
		prob_ctx0 = ac_encode_symbol_model_query_prob(E_CTX0, &model_events) * ctx_encode_tag_without_update_ac_query_prob(c0, tag);
	}
	float prob_ctx1 = 0;
	if (ctx_query_tag_item(c1, tag) != NULL) {
		prob_ctx1 = ac_encode_symbol_model_query_prob(E_CTX1, &model_events) * ctx_encode_tag_without_update_ac_query_prob(c1, tag);
	}
	float prob_ctx2 = 0;
	if (ctx_query_tag_item(c2, tag) != NULL) {
		prob_ctx2 = ac_encode_symbol_model_query_prob(E_CTX2, &model_events) * ctx_encode_tag_without_update_ac_query_prob(c2, tag);
	}
	float prob_idx1 = ac_encode_symbol_model_query_prob(E_IDX1, &model_events) * ac_encode_symbol_model_query_prob(index, &model_index1);

	int mode = E_IDX1;
	float prob = prob_idx1;

	if (prob_ctx0 > prob) {
		mode = E_CTX0;
		prob = prob_ctx0;
	}
	if (prob_ctx1 > prob) {
		mode = E_CTX1;
		prob = prob_ctx1;
	}
	if (prob_ctx2 > prob) {
		mode = E_CTX2;
		prob = prob_ctx2;
	}

	// encode

	ac_encode_symbol_model(&ac, bio, mode, &model_events);
	inc_model(&model_events, mode);

	switch (mode) {
		case E_CTX0:
			ctx_encode_tag_without_update_ac(bio, &ac, c0, tag);
			break;
		case E_CTX1:
			ctx_encode_tag_without_update_ac(bio, &ac, c1, tag);
			break;
		case E_CTX2:
			ctx_encode_tag_without_update_ac(bio, &ac, c2, tag);
			break;
		case E_IDX1:
			ac_encode_symbol_model(&ac, bio, index, &model_index1);
			inc_model(&model_index1, index);
			break;
	}

	events[mode]++;
	sizes[mode] += prob_to_bits(prob);

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

	tag_pair_enlarge();
	enlarge_ctx0();

	/* initialize AC models */
	model_create(&model_events, 6);

	/* initial frequencies in model_events */
	model_events.table[E_CTX0].freq = 1024;
	model_events.table[E_CTX1].freq = 1024;
	model_events.table[E_CTX2].freq = 1;
	model_events.table[E_IDX1].freq = 1;
	model_events.table[E_NEW ].freq = 1;
	count_cum_freqs(model_events.table, model_events.count);
	model_events.total = calc_total_freq(model_events.table, model_events.count);

	model_create(&model_match_size, 1 << MATCH_LOGSIZE);
	model_create(&model_chars, 256);
	model_create(&model_index1, 0);
}

void encode_match(struct bio *bio, char *p, size_t len)
{
	sizes[E_NEW] += prob_to_bits(ac_encode_symbol_model_query_prob(E_NEW, &model_events));
	ac_encode_symbol_model(&ac, bio, E_NEW, &model_events);
	inc_model(&model_events, E_NEW);

	assert(len > 0 && len <= (1 << MATCH_LOGSIZE));

	sizes[E_NEW] += prob_to_bits(ac_encode_symbol_model_query_prob(len - 1, &model_match_size));
	ac_encode_symbol_model(&ac, bio, len - 1, &model_match_size);
	inc_model(&model_match_size, len - 1);

	for (size_t c = 0; c < len; ++c) {
		sizes[E_NEW] += prob_to_bits(ac_encode_symbol_model_query_prob((unsigned char)p[c], &model_chars));
		ac_encode_symbol_model(&ac, bio, (unsigned char)p[c], &model_chars);
		inc_model(&model_chars, (unsigned char)p[c]);
	}

	events[E_NEW]++;
}

void decode_match(struct bio *bio, char *p, size_t *p_len)
{
	*p_len = ac_decode_symbol_model(&ac, bio, &model_match_size) + 1;
	sizes[E_NEW] += prob_to_bits(ac_encode_symbol_model_query_prob(*p_len - 1, &model_match_size));
	inc_model(&model_match_size, *p_len - 1);

	for (size_t c = 0; c < *p_len; ++c) {
		p[c] = (char)ac_decode_symbol_model(&ac, bio, &model_chars);
		sizes[E_NEW] += prob_to_bits(ac_encode_symbol_model_query_prob((unsigned char)p[c], &model_chars));
		inc_model(&model_chars, (unsigned char)p[c]);
	}
}

char *decompress(char *ptr, struct bio *bio)
{
	size_t prev_context1 = 0; /* previous context1 */
	size_t context1 = 0; /* last tag */
	size_t context2 = 0; /* last two bytes */

	char *p = ptr;

	for (;;) {
		size_t decision = ac_decode_symbol_model(&ac, bio, &model_events);
		sizes[decision] += prob_to_bits(ac_encode_symbol_model_query_prob(decision, &model_events));
		inc_model(&model_events, decision);

		if (decision == E_EOF) {
			break;
		} else if (decision == E_NEW) {
			/* new match */

			size_t len;

			decode_match(bio, p, &len);

			struct elem e;
			elem_fill(&e, p, len);

			if (dict_query_elem(&e) == 0) {
				if (!dict_can_insert_elem()) {
					dict_enlarge();
					enlarge_ctx1();
				}

				dict_insert_elem(&e);
				model_enlarge(&model_index1);
			}

			p += len;

			prev_context1 = 0;
			context1 = 0;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			dict_update_costs(p);

			events[E_NEW]++;
		} else {
			/* in dictionary */

			size_t index = decode_tag(decision, bio, prev_context1, context1, context2);

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

	for (char *p = ptr; p < end; ) {
		/* (1) look into dictionary */
		size_t index = dict_find_match(p);

		if (index != (size_t)-1 && dict_get_len_by_index(index) >= find_best_match(p) && p + dict_get_len_by_index(index) <= end) {
			/* found in dictionary */
			size_t len = dict_get_len_by_index(index);

			encode_tag(bio, prev_context1, context1, context2, index);

			prev_context1 = context1;
			context1 = dict_get_tag_by_index(index);

			dict_set_last_pos(index, p);

			p += len;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			/* recalc all costs, sort */
			dict_update_costs(p);
		} else {
			/* (2) else find best match and insert it into dictionary */
			size_t len = find_best_match(p);

			if (p + len > end) {
				len = end - p;
			}

			encode_match(bio, p, len);

			struct elem e;
			elem_fill(&e, p, len);

			/* close to the 'end', the alg. tries to insert matches already stored in the dictionary */
			if (dict_query_elem(&e) == 0) {
				if (!dict_can_insert_elem()) {
					dict_enlarge();
					enlarge_ctx1();
				}

				dict_insert_elem(&e);
				model_enlarge(&model_index1);
			}

			p += len;

			prev_context1 = 0;
			context1 = 0;

			if (p >= ptr + 2) {
				context2 = make_context2(p - 2);
			}

			dict_update_costs(p);
		}
	}

	/* signal end of input */
	ac_encode_symbol_model(&ac, bio, E_EOF, &model_events);
	inc_model(&model_events, E_EOF);
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

	for (size_t e = 0; e < tag_pair_get_elems(); ++e) {
		free(ctx0[e].arr);
	}
	free(ctx0);
	tag_pair_destroy();

	model_destroy(&model_events);
	model_destroy(&model_match_size);
	model_destroy(&model_chars);
	model_destroy(&model_index1);
}

enum {
	COMPRESS,
	DECOMPRESS
};

void print_help(char *path)
{
	fprintf(stderr, "Usage :\n\t%s [arguments] [input-file] [output-file]\n\n", path);
	fprintf(stderr, "Arguments :\n");
	fprintf(stderr, " -d     : force decompression\n");
	fprintf(stderr, " -z     : force compression\n");
	fprintf(stderr, " -f     : overwrite existing output file\n");
	fprintf(stderr, " -k     : keep (don't delete) input file (default)\n");
	fprintf(stderr, " -h     : print this message\n");
	fprintf(stderr, " -t NUM : maximum number of matches (affects compression ratio and speed)\n");
	fprintf(stderr, " -w NUM : window size (in kilobytes, affects compression ratio and speed)\n");
	fprintf(stderr, " -T NUM : spawns NUM compression threads\n");
}

int main(int argc, char *argv[])
{
	int mode = COMPRESS;
	int force = 0;

	parse: switch (getopt(argc, argv, "zdfkht:w:T:")) {
		case 'z':
			mode = COMPRESS;
			goto parse;
		case 'd':
			mode = DECOMPRESS;
			goto parse;
		case 'f':
			force = 1;
			goto parse;
		case 'k':
			goto parse;
		case 'h':
			print_help(argv[0]);
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
			istream = stdin;
			ostream = stdout;
			break;
		case 1:
			istream = fopen(argv[optind], "r");
			/* guess output file name */
			if (mode == COMPRESS) {
				char path[4096];
				sprintf(path, "%s.x3", argv[optind]); /* add .x suffix */
				ostream = force_fopen(path, "w", force);
			} else {
				if (strrchr(argv[optind], '.') != NULL) {
					*strrchr(argv[optind], '.') = 0; /* remove suffix */
				}
				ostream = force_fopen(argv[optind], "w", force);
			}
			break;
		case 2:
			istream = fopen(argv[optind + 0], "r");
			ostream = force_fopen(argv[optind + 1], "w", force);
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
	/* compressed size */
	size_t asize;

	if (mode == COMPRESS) {
		fprintf(stderr, "max match count: %i\n", get_max_match_count());
		fprintf(stderr, "forward window: %zu\n", get_forward_window());
		fprintf(stderr, "threads: %i\n", get_num_threads());

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

		ac_init(&ac);

		long start = wall_clock();

		compress(iptr, isize, &bio);

		fprintf(stderr, "elapsed time: %f\n", (wall_clock() - start) / (float)1000000000L);

		ac_encode_flush(&ac, &bio);
		bio_close(&bio, BIO_MODE_WRITE);

		char *end = (char *)bio.ptr;

		size = isize;
		asize = end - optr;

		fsave(optr, asize, ostream);

		free(iptr);
		free(optr);
	} else {
		size_t isize = fsize(istream);

		asize = isize;

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

		ac_init(&ac);

		ac_decode_init(&ac, &bio);

		long start = wall_clock();

		char *oend = decompress((char *)optr, &bio);

		fprintf(stderr, "elapsed time: %f\n", (wall_clock() - start) / (float)1000000000L);

		bio_close(&bio, BIO_MODE_READ);

		size = oend - optr;

		fsave(optr, size, ostream);

		free(iptr);
		free(optr);
	}

	destroy();

	fclose(istream);
	fclose(ostream);

	size_t dict_hit_count = events[E_CTX0] + events[E_CTX1] + events[E_CTX2] + events[E_IDX1];

	size_t stream_size_gr = (size_t)ceil(sizes[E_CTX0] + sizes[E_CTX1] + sizes[E_CTX2] + sizes[E_IDX1]);
	size_t stream_size    = (size_t)ceil(sizes[E_CTX0] + sizes[E_CTX1] + sizes[E_CTX2] + sizes[E_IDX1] + sizes[E_NEW]);

	fprintf(stderr, "input stream size: %zu\n", size);
	fprintf(stderr, "output stream size: %zu\n", (stream_size + 7) / 8);
	fprintf(stderr, "dictionary: hit %zu, miss %zu\n", dict_hit_count, events[E_NEW]);

	fprintf(stderr, "codestream size: dictionary %zu / %f%%, new fragment %zu / %f%%\n",
		(stream_size_gr + 7) / 8, 100.f * stream_size_gr / stream_size,
		((size_t)ceil(sizes[E_NEW]) + 7) / 8, 100.f * (size_t)ceil(sizes[E_NEW]) / stream_size
	);

#if 1
	fprintf(stderr, "\x1b[37;1mGR compression ratio: %f\x1b[0m\n", size / (float)((stream_size + 7) / 8));
	fprintf(stderr, "\x1b[37;1mAC compression ratio: %f\x1b[0m\n", size / (float)asize);
#else
	fprintf(stderr, "GR compression ratio: %f\n", size / (float)((stream_size + 7) / 8));
	fprintf(stderr, "AC compression ratio: %f\n", size / (float)asize);
#endif

	fprintf(stderr, "number of events: ctx0 %zu, ctx1 %zu, ctx2 %zu, ctx3 %zu, miss1 %zu, miss2 %zu, new %zu\n",
		events[E_CTX0], events[E_CTX1], events[E_CTX2], (size_t)0, events[E_IDX1], (size_t)0, events[E_NEW]);
	fprintf(stderr, "contexts sizes: ctx0 %f%%, ctx1 %f%%, ctx2 %f%%, ctx3 %f%%, miss1 %f%%, miss2 %f%%, new %f%%\n",
		100.f * (size_t)ceil(sizes[E_CTX0]) / stream_size,
		100.f * (size_t)ceil(sizes[E_CTX1]) / stream_size,
		100.f * (size_t)ceil(sizes[E_CTX2]) / stream_size,
		0.f,
		100.f * (size_t)ceil(sizes[E_IDX1]) / stream_size,
		0.f,
		100.f * (size_t)ceil(sizes[E_NEW] ) / stream_size
	);

	fprintf(stderr, "context entries: ctx0 %zu, ctx1 %zu, ctx2 %zu, ctx3 %zu\n", tag_pair_get_elems(), dict_get_elems(), (size_t)65536, (size_t)256);

#if 0
	fprintf(stderr, "float PROB_CTX0 = %f;\n", ac_encode_symbol_model_query_prob(E_CTX0, &model_events));
	fprintf(stderr, "float PROB_CTX1 = %f;\n", ac_encode_symbol_model_query_prob(E_CTX1, &model_events));
	fprintf(stderr, "float PROB_CTX2 = %f;\n", ac_encode_symbol_model_query_prob(E_CTX2, &model_events));
	fprintf(stderr, "float PROB_IDX1 = %f;\n", ac_encode_symbol_model_query_prob(E_IDX1, &model_events));
#endif

	return 0;
}
