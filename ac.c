#include "ac.h"

#include <assert.h>
#include <stdlib.h>

void count_cum_freqs(struct symbol *table, size_t symbols)
{
// 	assert(symbols > 0);

	if (symbols > 0) {
		table[0].cum_freq = 0;

		for (size_t i = 1; i < symbols; i++) {
			table[i].cum_freq =
				table[i-1].cum_freq +
				table[i-1].freq;
		}

	}
}

size_t calc_total_freq(struct symbol *table, size_t symbols)
{
	size_t total = 0;

	for (size_t i = 0; i < symbols; i++) {
		total += table[i].freq;
	}

	return total;
}

const size_t g_FirstQuarter = 0x20000000;
const size_t g_ThirdQuarter = 0x60000000;
const size_t g_Half         = 0x40000000;

void ac_init(struct ac *ac)
{
	ac->mLow  = 0x00000000;
	ac->mHigh = 0x7FFFFFFF;

	ac->mScale = 0;
}

#define put_bit(bio, b) bio_write_bits((bio), (b), 1)
#define get_bit(bio) bio_read_bits((bio), 1)

void ac_encode_scale(struct ac *ac, struct bio *bio)
{
	// E1/E2
	while ((ac->mHigh < g_Half) || (ac->mLow >= g_Half)) {
		if (ac->mHigh < g_Half) {
			put_bit(bio, 0); // bw_put(ac->bw, 0);
			ac->mLow  = 2 * ac->mLow;
			ac->mHigh = 2 * ac->mHigh + 1;

			for (; ac->mScale > 0; ac->mScale--) {
				put_bit(bio, 1); // bw_put(ac->bw, 1);
			}
		} else if (ac->mLow >= g_Half) {
			put_bit(bio, 1); // bw_put(ac->bw, 1);
			ac->mLow  = 2 * (ac->mLow  - g_Half);
			ac->mHigh = 2 * (ac->mHigh - g_Half) + 1;

			for (; ac->mScale > 0; ac->mScale--) {
				put_bit(bio, 0); // bw_put(ac->bw, 0);
			}
		}
	}

	// E3
	while ((g_FirstQuarter <= ac->mLow) && (ac->mHigh < g_ThirdQuarter)) {
		ac->mScale++;
		ac->mLow  = 2 * (ac->mLow  - g_FirstQuarter);
		ac->mHigh = 2 * (ac->mHigh - g_FirstQuarter) + 1;
	}
}

void ac_encode(struct ac *ac, struct bio *bio, size_t low_freq, size_t high_freq, size_t total)
{
	size_t mStep = (ac->mHigh - ac->mLow + 1) / total;

	ac->mHigh = ac->mLow + mStep * high_freq - 1;
	ac->mLow  = ac->mLow + mStep * low_freq;

	ac_encode_scale(ac, bio);
}

size_t index_of_symbol(size_t symb, struct symbol *model, size_t symbols)
{
	for (size_t i = 0; i < symbols; i++) {
		if (symb == model[i].symb) {
			return i;
		}
	}

	abort();
}

void ac_encode_symbol(struct ac *ac, struct bio *bio, size_t symb, struct symbol *model, size_t symbols, size_t total_count)
{
	size_t index = index_of_symbol(symb, model, symbols);

	size_t low_freq  = model[index].cum_freq;
	size_t high_freq = model[index].cum_freq + model[index].freq;

	ac_encode(ac, bio, low_freq, high_freq, total_count);
}

void ac_encode_flush(struct ac *ac, struct bio *bio)
{
	if (ac->mLow < g_FirstQuarter) {
		put_bit(bio, 0); // bw_put(ac->bw, 0);

		for (size_t i=0; i < ac->mScale + 1; i++) {
			put_bit(bio, 1); // bw_put(ac->bw, 1);
		}
	} else {
		put_bit(bio, 1); // bw_put(ac->bw, 1);
	}

	// bio_close(bio, BIO_MODE_WRITE); // bw_flush(ac->bw);
}

size_t ac_decode_target(struct ac *ac, size_t mStep)
{
	return (ac->mBuffer - ac->mLow) / mStep;
}

void ac_decode_init(struct ac *ac, struct bio *bio)
{
	ac->mBuffer = 0;

	for (size_t i = 0; i < 31; i++) {
		ac->mBuffer = (ac->mBuffer << 1) | get_bit(bio); // br_get(ac->br);
	}
}

void ac_decode_scale(struct ac *ac, struct bio *bio)
{
	// E1/E2
	while ((ac->mHigh < g_Half) || (ac->mLow >= g_Half)) {
		if (ac->mHigh < g_Half) {
			ac->mLow    = 2 * ac->mLow;
			ac->mHigh   = 2 * ac->mHigh + 1;
			ac->mBuffer = 2 * ac->mBuffer + get_bit(bio); // br_get(ac->br);
		} else if (ac->mLow >= g_Half) {
			ac->mLow    = 2 * (ac->mLow    - g_Half);
			ac->mHigh   = 2 * (ac->mHigh   - g_Half) + 1;
			ac->mBuffer = 2 * (ac->mBuffer - g_Half) + get_bit(bio); // br_get(ac->br);
		}
		ac->mScale = 0;
	}

	// E3
	while ((g_FirstQuarter <= ac->mLow) && (ac->mHigh < g_ThirdQuarter)) {
		ac->mScale++;
		ac->mLow    = 2 * (ac->mLow    - g_FirstQuarter);
		ac->mHigh   = 2 * (ac->mHigh   - g_FirstQuarter) + 1;
		ac->mBuffer = 2 * (ac->mBuffer - g_FirstQuarter) + get_bit(bio); // br_get(ac->br);
	}
}

size_t index_of_value(size_t value, struct symbol *model, size_t symbols)
{
	for (size_t i = 0; i < symbols; i++) {
		size_t low_freq  = model[i].cum_freq;
		size_t high_freq = model[i].cum_freq + model[i].freq;

		if (value >= low_freq && value < high_freq) {
			return i;
		}
	}

	abort();
}

size_t ac_decode_symbol(struct ac *ac, struct bio *bio, struct symbol *model, size_t symbols, size_t total)
{
	size_t mStep = (ac->mHigh - ac->mLow + 1) / total;

	size_t value = ac_decode_target(ac, mStep);

	size_t index = index_of_value(value, model, symbols);

	size_t low_freq  = model[index].cum_freq;
	size_t high_freq = model[index].cum_freq + model[index].freq;

	ac->mHigh = ac->mLow + mStep * high_freq - 1;
	ac->mLow  = ac->mLow + mStep * low_freq;

	ac_decode_scale(ac, bio);

	return model[index].symb;
}

void ac_encode_symbol_model(struct ac *ac, struct bio *bio, size_t symb, struct model *model)
{
	ac_encode_symbol(ac, bio, symb, model->table, model->count, model->total);
}

size_t ac_decode_symbol_model(struct ac *ac, struct bio *bio, struct model *model)
{
	return ac_decode_symbol(ac, bio, model->table, model->count, model->total);
}

void inc_model(struct model *model, size_t symbol)
{
	const size_t index = symbol;

	const size_t increment = 1;

	model->table[index].freq += increment;
	count_cum_freqs(model->table, model->count);
	model->total += increment;
}

void model_create(struct model *model, size_t size)
{
	assert(model != NULL);

	model->count = size;
	model->table = malloc(model->count * sizeof(struct symbol));

	if (model->table == NULL) {
		abort();
	}

	for (size_t i = 0; i < model->count; ++i) {
		model->table[i].symb = i;
		model->table[i].freq = 1;
	}

	count_cum_freqs(model->table, model->count);
	model->total = calc_total_freq(model->table, model->count);
}

void model_enlarge(struct model *model)
{
	assert(model != NULL);

	model->count++;
	model->table = realloc(model->table, model->count * sizeof(struct symbol));

	if (model->table == NULL) {
		abort();
	}

	model->table[model->count - 1].symb = model->count - 1;
	model->table[model->count - 1].freq = 1;

	count_cum_freqs(model->table, model->count);
	model->total = calc_total_freq(model->table, model->count);
}
