#include "gr.h"

/* recompute Golomb-Rice codes after... */
#define RESET_INTERVAL 256

void gr_init(struct gr *gr, size_t k)
{
	gr->opt_k = k;
	gr->symb_sum = 0;
	gr->symb_cnt = 0;
}

size_t bio_sizeof_gr(size_t k, size_t N)
{
	size_t size;
	size_t Q = N >> k;

	size = Q + 1;

	size += k;

	return size;
}

size_t get_opt_k(size_t symb_sum, size_t symb_cnt)
{
	if (symb_cnt == 0) {
		return 0;
	}

	int k;

	for (k = 1; (symb_cnt << k) <= symb_sum; ++k)
		;

	return (size_t)(k - 1);
}

void gr_recalc_k(struct gr *gr)
{
	gr->opt_k = get_opt_k(gr->symb_sum, gr->symb_cnt);
}

void gr_update(struct gr *gr, size_t symb)
{
	gr->symb_sum += symb;
	gr->symb_cnt++;
}

void gr_update_model(struct gr *gr, size_t symb)
{
	if (gr->symb_cnt == RESET_INTERVAL) {
		gr_recalc_k(gr);

		gr_init(gr, gr->opt_k);
	}

	gr_update(gr, symb);
}

size_t gr_sizeof_symb(struct gr *gr, size_t symb)
{
	return bio_sizeof_gr(gr->opt_k, symb);
}
