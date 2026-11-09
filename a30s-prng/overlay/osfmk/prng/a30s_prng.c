/*
 * A30S bring-up PRNG.
 *
 * On a real iOS system the kernel PRNG is supplied by the corecrypto kext, which calls
 * register_and_init_prng() while IOKit starts. This port has no kexts, so read_random()
 * dereferenced a NULL mutex (prng.lock.mutex) and the kernel took a data abort inside
 * lck_mtx_lock() from kernel_bootstrap_thread().
 *
 * This file registers a small software generator (xoshiro256**, seeded from the entropy the
 * loader passes in and the timebase). It is NOT cryptographically secure: it only exists so the
 * kernel can boot. Replace it with the real corecrypto PRNG once that is ported.
 */
#include <kern/misc_protos.h>
#include <machine/machine_routines.h>
#include <prng/random.h>
#include <corecrypto/cckprng.h>
#include <corecrypto/cc_error.h>

#if defined(ARM64_BOARD_CONFIG_A30S)

static uint64_t a30s_rng_s[4];

static uint64_t
a30s_rotl(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static uint64_t
a30s_splitmix64(uint64_t *x)
{
	uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static uint64_t
a30s_rng_next(void)
{
	uint64_t result = a30s_rotl(a30s_rng_s[1] * 5, 7) * 9;
	uint64_t t = a30s_rng_s[1] << 17;

	a30s_rng_s[2] ^= a30s_rng_s[0];
	a30s_rng_s[3] ^= a30s_rng_s[1];
	a30s_rng_s[1] ^= a30s_rng_s[2];
	a30s_rng_s[0] ^= a30s_rng_s[3];
	a30s_rng_s[2] ^= t;
	a30s_rng_s[3] = a30s_rotl(a30s_rng_s[3], 45);
	return result;
}

static void
a30s_rng_mix(size_t nbytes, const void *data)
{
	const uint8_t *p = (const uint8_t *)data;
	uint64_t x = ml_get_timebase();
	size_t i;

	for (i = 0; i < nbytes; i++) {
		x = (x << 8) ^ (x >> 56) ^ p[i];
		a30s_rng_s[i & 3] ^= a30s_splitmix64(&x);
	}
	/* never leave the all-zero state */
	if ((a30s_rng_s[0] | a30s_rng_s[1] | a30s_rng_s[2] | a30s_rng_s[3]) == 0) {
		a30s_rng_s[0] = 0x9e3779b97f4a7c15ULL;
	}
}

static int
a30s_prng_init(cckprng_ctx_t ctx, size_t nbytes, const void *seed)
{
	uint64_t x = 0x243f6a8885a308d3ULL ^ ml_get_timebase();
	int i;

	(void)ctx;
	for (i = 0; i < 4; i++) {
		a30s_rng_s[i] = a30s_splitmix64(&x);
	}
	a30s_rng_mix(nbytes, seed);
	return CCKPRNG_OK;
}

static int
a30s_prng_reseed(cckprng_ctx_t ctx, size_t nbytes, const void *seed)
{
	(void)ctx;
	a30s_rng_mix(nbytes, seed);
	return CCKPRNG_OK;
}

static int
a30s_prng_addentropy(cckprng_ctx_t ctx, size_t nbytes, const void *entropy)
{
	(void)ctx;
	a30s_rng_mix(nbytes, entropy);
	return CCKPRNG_OK;
}

static int
a30s_prng_generate(cckprng_ctx_t ctx, size_t nbytes, void *out)
{
	uint8_t *p = (uint8_t *)out;

	(void)ctx;
	while (nbytes) {
		uint64_t r = a30s_rng_next();
		size_t n = nbytes < 8 ? nbytes : 8;
		size_t i;

		for (i = 0; i < n; i++) {
			p[i] = (uint8_t)(r >> (8 * i));
		}
		p += n;
		nbytes -= n;
	}
	return CCKPRNG_OK;
}

static const struct prng_fns a30s_prng_fns = {
	.init       = a30s_prng_init,
	.reseed     = a30s_prng_reseed,
	.addentropy = a30s_prng_addentropy,
	.generate   = a30s_prng_generate,
};

void
a30s_prng_register(void)
{
	register_and_init_prng(&a30s_prng_fns);
	printf("A30S PRNG: software generator registered (bring-up only, not secure)\n");
}

#endif /* ARM64_BOARD_CONFIG_A30S */
