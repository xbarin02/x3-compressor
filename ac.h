#ifndef AC_H
#define AC_H

#include <stddef.h>

#include "bio.h"

struct symbol {
	size_t symb;
	size_t freq;
	size_t cum_freq;
};

void count_cum_freqs(struct symbol *table, size_t symbols);

size_t calc_total_freq(struct symbol *table, size_t symbols);

struct ac {
	size_t mLow;
	size_t mHigh;

	size_t mBuffer;
	size_t mScale;
};

void ac_init(struct ac *ac);

void ac_encode_symbol(struct ac *ac, struct bio *bio, size_t symb, struct symbol *model, size_t symbols, size_t total_count);
void ac_encode_flush(struct ac *ac, struct bio *bio);

void ac_decode_init(struct ac *ac, struct bio *bio);
size_t ac_decode_symbol(struct ac *ac, struct bio *bio, struct symbol *model, size_t symbols, size_t total);

struct model {
	size_t count;
	size_t total;
	struct symbol *table; /* count entries */
};

void ac_encode_symbol_model(struct ac *ac, struct bio *bio, size_t symb, struct model *model);
float ac_encode_symbol_model_query_prob(struct ac *ac, struct bio *bio, size_t symb, struct model *model);
size_t ac_decode_symbol_model(struct ac *ac, struct bio *bio, struct model *model);
void inc_model(struct model *model, size_t symbol);

void model_create(struct model *model, size_t size);
void model_enlarge(struct model *model);
void model_destroy(struct model *model);

#endif /* AC_H */
