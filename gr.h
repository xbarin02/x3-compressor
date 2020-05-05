/*
 * Selects the optimum parameter for Golomb-Rice coding
 */
#ifndef GR_H
#define GR_H

#include <stddef.h>

struct gr {
	size_t opt_k;
	/* mean = symb_sum / symb_count */
	size_t symb_sum;
	size_t symb_cnt;
};

void gr_init(struct gr *gr, size_t k);

size_t gr_sizeof_symb(struct gr *gr, size_t symb);

void gr_recalc_k(struct gr *gr);

void gr_update(struct gr *gr, size_t symb);

void gr_update_model(struct gr *gr, size_t symb);

#endif /* GR_H */
