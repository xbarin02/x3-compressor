#include "context.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct ctx *ctx_enlarge(struct ctx *c, size_t size, size_t elems)
{
	c = realloc(c, size * sizeof(struct ctx));

	if (c == NULL) {
		abort();
	}

	memset(c + elems, 0, (size - elems) * sizeof(struct ctx));

	return c;
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
#if 0
	qsort(ctx->arr, ctx->items, sizeof(struct item), item_compar);

	if (ctx->items > 1) {
		assert(ctx->arr[0].freq >= ctx->arr[1].freq);
	}
#else
	(void)ctx;
#endif
}

void ctx_item_inc_freq(struct ctx *ctx, size_t tag)
{
	struct item *item = ctx_query_tag_item(ctx, tag);

	item->freq++;
}

void ctx_encode_tag_without_update_ac(struct bio *bio, struct ac *ac, struct ctx *ctx, size_t tag)
{
	struct model model;
	model_create(&model, ctx->items);

	for (size_t i = 0; i < ctx->items; ++i) {
		model.table[i].freq = ctx->arr[i].freq;
	}

	count_cum_freqs(model.table, model.count);
	model.total = calc_total_freq(model.table, model.count);

	size_t item_index = ctx_query_tag_index(ctx, tag);

	ac_encode_symbol_model(ac, bio, item_index, &model);

	model_destroy(&model);
}

float ctx_encode_tag_without_update_ac_query_prob(struct ctx *ctx, size_t tag)
{
	struct model model;
	model_create(&model, ctx->items);

	for (size_t i = 0; i < ctx->items; ++i) {
		model.table[i].freq = ctx->arr[i].freq;
	}

	count_cum_freqs(model.table, model.count);
	model.total = calc_total_freq(model.table, model.count);

	size_t item_index = ctx_query_tag_index(ctx, tag);

	float prob = ac_encode_symbol_model_query_prob(item_index, &model);

	model_destroy(&model);

	return prob;
}

size_t ctx_decode_tag_without_update_ac(struct bio *bio, struct ac *ac, struct ctx *ctx)
{
	struct model model;
	model_create(&model, ctx->items);

	for (size_t i = 0; i < ctx->items; ++i) {
		model.table[i].freq = ctx->arr[i].freq;
	}

	count_cum_freqs(model.table, model.count);
	model.total = calc_total_freq(model.table, model.count);

	size_t item_index = ac_decode_symbol_model(ac, bio, &model);

	model_destroy(&model);

	return ctx->arr[item_index].tag;
}
