#include "bio.h"

#include <assert.h>

struct bio {
	uint32_t *ptr; /* pointer to memory */
	void *end;
	uint32_t b;    /* bit buffer */
	size_t c;      /* bit counter */
};

void bio_open(struct bio *bio, void *ptr, void *end, int mode)
{
	assert(bio != NULL);

	bio->ptr = ptr;
	bio->end = (char *)end - 3;

	if (mode == BIO_MODE_READ) {
		bio->c = 32;
	} else {
		bio->b = 0;
		bio->c = 0;
	}
}

static void bio_flush_buffer(struct bio *bio)
{
	assert(bio != NULL);
	assert(bio->ptr != NULL);

	*(bio->ptr++) = bio->b;
	bio->b = 0;
	bio->c = 0;
}

static void bio_reload_buffer(struct bio *bio)
{
	assert(bio != NULL);
	assert(bio->ptr != NULL);

	if ((void *)bio->ptr < bio->end) {
		bio->b = *(bio->ptr++);
	} else {
		bio->b = 0x80000000;
	}

	bio->c = 0;
}

static void bio_put_nonzero_bit(struct bio *bio)
{
	assert(bio != NULL);
	assert(bio->c < 32);

	bio->b |= (uint32_t)1 << bio->c;
	bio->c++;

	if (bio->c == 32) {
		bio_flush_buffer(bio);
	}
}

static size_t minsize(size_t a, size_t b)
{
	return a < b ? a : b;
}

static size_t ctzu32(uint32_t n)
{
	if (n == 0) {
		return 32;
	}

	switch (sizeof(uint32_t)) {
		static const int lut[32] = {
			0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
			31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
		};
#ifdef __GNUC__
		case sizeof(unsigned):
			return __builtin_ctz((unsigned)n);
		case sizeof(unsigned long):
			return __builtin_ctzl((unsigned long)n);
#endif
		default:
			/* http://graphics.stanford.edu/~seander/bithacks.html */
			return lut[((uint32_t)((n & -n) * 0x077CB531U)) >> 27];
	}
}

void bio_write_bits(struct bio *bio, uint32_t b, size_t n)
{
	assert(n <= 32);

	for (int i = 0; i < 2; ++i) {
		assert(bio->c < 32);

		size_t m = minsize(32 - bio->c, n);

		bio->b |= (b & (((uint32_t)1 << m) - 1)) << bio->c;
		bio->c += m;

		if (bio->c == 32) {
			bio_flush_buffer(bio);
		}

		b >>= m;
		n -= m;

		if (n == 0) {
			return;
		}
	}
}

static void bio_write_zero_bits(struct bio *bio, size_t n)
{
	assert(n <= 32);

	for (size_t m; n > 0; n -= m) {
		assert(bio->c < 32);

		m = minsize(32 - bio->c, n);

		bio->c += m;

		if (bio->c == 32) {
			bio_flush_buffer(bio);
		}
	}
}

uint32_t bio_read_bits(struct bio *bio, size_t n)
{
	if (bio->c == 32) {
		bio_reload_buffer(bio);
	}

	/* get the avail. least-significant bits */
	size_t s = minsize(32 - bio->c, n);

	uint32_t w = bio->b & (((uint32_t)1 << s) - 1);

	bio->b >>= s;
	bio->c += s;

	n -= s;

	/* need more bits? reload & get the most-significant bits */
	if (n > 0) {
		assert(bio->c == 32);

		bio_reload_buffer(bio);

		w |= (bio->b & (((uint32_t)1 << n) - 1)) << s;

		bio->b >>= n;
		bio->c += n;
	}

	return w;
}

void bio_close(struct bio *bio, int mode)
{
	assert(bio != NULL);

	if (mode == BIO_MODE_WRITE && bio->c > 0) {
		bio_flush_buffer(bio);
	}
}

static void bio_write_unary(struct bio *bio, uint32_t N)
{
	for (; N > 32; N -= 32) {
		bio_write_zero_bits(bio, 32);
	}

	bio_write_zero_bits(bio, N);

	bio_put_nonzero_bit(bio);
}

static uint32_t bio_read_unary(struct bio *bio)
{
	/* get zeros... */
	uint32_t total_zeros = 0;

	assert(bio != NULL);

	do {
		if (bio->c == 32) {
			bio_reload_buffer(bio);
		}

		/* get trailing zeros */
		size_t s = minsize(32 - bio->c, ctzu32(bio->b));

		bio->b >>= s;
		bio->c += s;

		total_zeros += s;
	} while (bio->c == 32);

	/* ...and drop non-zero bit */
	assert(bio->c < 32);

	bio->b >>= 1;
	bio->c++;

	return total_zeros;
}

/* Golomb-Rice, encode non-negative integer N, parameter M = 2^k */
void bio_write_gr(struct bio *bio, size_t k, uint32_t N)
{
	uint32_t Q = N >> k;

	bio_write_unary(bio, Q);

	assert(k <= 32);

	bio_write_bits(bio, N, k);
}

uint32_t bio_read_gr(struct bio *bio, size_t k)
{
	uint32_t Q = bio_read_unary(bio);
	uint32_t N = Q << k;

	assert(k <= 32);

	N |= bio_read_bits(bio, k);

	return N;
}

size_t bio_sizeof_gr(size_t k, uint32_t N)
{
	size_t size;
	uint32_t Q = N >> k;

	size = Q + 1;

	size += k;

	return size;
}
