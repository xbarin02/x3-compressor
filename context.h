#ifndef CONTEXT_H
#define CONTEXT_H

#include <stddef.h>
#include "gr.h"
#include "bio.h"
#include "ac.h"

struct item {
	size_t tag;
	size_t freq; /* used n-times */
};

struct ctx {
	size_t items; /* allocated elements */
	struct item *arr; /* pointer to the first item */
};

struct ctx *ctx_enlarge(struct ctx *c, size_t size, size_t elems);

struct item *ctx_query_tag_item(struct ctx *c, size_t tag);

size_t ctx_query_tag_index(struct ctx *c, size_t tag);

void ctx_add_tag(struct ctx *c, size_t tag);

void ctx_sort(struct ctx *ctx);

void ctx_item_inc_freq(struct ctx *ctx, size_t tag);

void ctx_encode_tag_without_update_ac(struct bio *bio_a, struct ac *ac, struct ctx *ctx, size_t tag);
float ctx_encode_tag_without_update_ac_query_prob(struct ctx *ctx, size_t tag);

size_t ctx_decode_tag_without_update_ac(struct bio *bio_a, struct ac *ac, struct ctx *ctx);

#endif
