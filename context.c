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

	for (size_t e = elems; e < size; ++e) {
		gr_init(&c[e].gr, 0);
	}

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

void ctx_encode_tag_without_update(struct bio *bio, struct ctx *ctx, size_t tag)
{
	if (ctx->items > 1) {
		gr_recalc_k(&ctx->gr);
		size_t item_index = ctx_query_tag_index(ctx, tag);
		assert(item_index <= UINT32_MAX);
		bio_write_gr(bio, ctx->gr.opt_k, (uint32_t)item_index);
	} else {
		/* no information needed */
	}
}

size_t ctx_decode_tag_without_update(struct bio *bio, struct ctx *ctx)
{
	size_t item_index = 0;

	if (ctx->items > 1) {
		/* decode item_index */
		gr_recalc_k(&ctx->gr);
		item_index = (size_t)bio_read_gr(bio, ctx->gr.opt_k);
	} else {
		/* there is only one */
	}

	return ctx->arr[item_index].tag;
}
