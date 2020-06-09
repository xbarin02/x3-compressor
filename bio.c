#include "bio.h"

#include <assert.h>

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

static size_t minsize(size_t a, size_t b)
{
	return a < b ? a : b;
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
