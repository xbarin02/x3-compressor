#ifndef BIO_H_
#define BIO_H_

#include <stddef.h>
#include <stdint.h>

enum {
	BIO_MODE_READ,
	BIO_MODE_WRITE
};

struct bio {
	uint32_t *ptr; /* pointer to memory */
	void *end;
	uint32_t b;    /* bit buffer */
	size_t c;      /* bit counter */
};

void bio_open(struct bio *bio, void *ptr, void *end, int mode);

void bio_close(struct bio *bio, int mode);

size_t bio_sizeof_gr(size_t k, uint32_t N);

void bio_write_gr(struct bio *bio, size_t k, uint32_t N);

uint32_t bio_read_gr(struct bio *bio, size_t k);

void bio_write_bits(struct bio *bio, uint32_t b, size_t n);

uint32_t bio_read_bits(struct bio *bio, size_t n);

#endif /* BIO_H_ */
