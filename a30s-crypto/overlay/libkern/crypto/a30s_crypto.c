/*
 * A30S: built-in minimal crypto function table (bring-up only).
 *
 * The real table is registered by the corecrypto kext, which is absent here. Without it
 * g_crypto_funcs stays NULL and tcp_init() -> tcp_sysctl_fastopenkey() -> aes_encrypt_key128()
 * data-aborts (far=0x78). This registers:
 *   - SHA-1 / SHA-256 digests and HMAC from the xnu-internal corecrypto sources,
 *   - a small, table-free-at-build-time software AES (CBC enc/dec, 128/192/256 bit).
 * Everything else stays NULL. NOT constant-time, NOT hardened: bring-up only.
 */
#include <libkern/libkern.h>
#include <libkern/crypto/register_crypto.h>
#include <libkern/crypto/crypto_internal.h>
#include <corecrypto/ccdigest.h>
#include <corecrypto/cchmac.h>
#include <corecrypto/ccmode.h>

/* ------------------------------------------------------------------ AES core */
static uint8_t a30s_sbox[256], a30s_isbox[256];
static int a30s_aes_tables_ready;

static uint8_t a30s_rotl8(uint8_t x, int s) { return (uint8_t)((x << s) | (x >> (8 - s))); }
static uint8_t a30s_xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static uint8_t a30s_mul(uint8_t a, uint8_t b)
{
	uint8_t r = 0;
	while (b) {
		if (b & 1) { r ^= a; }
		a = a30s_xtime(a);
		b >>= 1;
	}
	return r;
}

static void a30s_aes_tables(void)
{
	uint8_t p = 1, q = 1;
	if (a30s_aes_tables_ready) { return; }
	do {
		p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
		q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4);
		if (q & 0x80) { q ^= 0x09; }
		a30s_sbox[p] = (uint8_t)(q ^ a30s_rotl8(q, 1) ^ a30s_rotl8(q, 2) ^ a30s_rotl8(q, 3) ^ a30s_rotl8(q, 4) ^ 0x63);
	} while (p != 1);
	a30s_sbox[0] = 0x63;
	for (int i = 0; i < 256; i++) { a30s_isbox[a30s_sbox[i]] = (uint8_t)i; }
	a30s_aes_tables_ready = 1;
}

struct a30s_aes_ctx {
	uint32_t rounds;
	uint8_t rk[16 * 15];
};

static int a30s_aes_expand(struct a30s_aes_ctx *c, size_t key_len, const void *key)
{
	const uint8_t *k = key;
	int nk, nr, i;
	uint8_t rcon = 1;

	if (key_len >= 128) { key_len /= 8; }       /* accept bits or bytes */
	if (key_len != 16 && key_len != 24 && key_len != 32) { return -1; }
	a30s_aes_tables();
	nk = (int)(key_len / 4);
	nr = nk + 6;
	c->rounds = (uint32_t)nr;
	for (i = 0; i < (int)key_len; i++) { c->rk[i] = k[i]; }
	for (i = nk; i < 4 * (nr + 1); i++) {
		uint8_t t[4];
		for (int j = 0; j < 4; j++) { t[j] = c->rk[4 * (i - 1) + j]; }
		if (i % nk == 0) {
			uint8_t t0 = t[0];
			t[0] = (uint8_t)(a30s_sbox[t[1]] ^ rcon);
			t[1] = a30s_sbox[t[2]]; t[2] = a30s_sbox[t[3]]; t[3] = a30s_sbox[t0];
			rcon = a30s_xtime(rcon);
		} else if (nk > 6 && i % nk == 4) {
			for (int j = 0; j < 4; j++) { t[j] = a30s_sbox[t[j]]; }
		}
		for (int j = 0; j < 4; j++) { c->rk[4 * i + j] = (uint8_t)(c->rk[4 * (i - nk) + j] ^ t[j]); }
	}
	return 0;
}

static void a30s_add_rk(uint8_t *s, const uint8_t *rk) { for (int i = 0; i < 16; i++) { s[i] ^= rk[i]; } }

static void a30s_aes_enc_block(const struct a30s_aes_ctx *c, const uint8_t *in, uint8_t *out)
{
	uint8_t s[16], t[16];
	for (int i = 0; i < 16; i++) { s[i] = in[i]; }
	a30s_add_rk(s, c->rk);
	for (uint32_t r = 1; r <= c->rounds; r++) {
		for (int i = 0; i < 16; i++) { s[i] = a30s_sbox[s[i]]; }
		for (int col = 0; col < 4; col++) {            /* ShiftRows */
			for (int row = 0; row < 4; row++) { t[4 * col + row] = s[4 * ((col + row) & 3) + row]; }
		}
		if (r != c->rounds) {                           /* MixColumns */
			for (int col = 0; col < 4; col++) {
				uint8_t *a = &t[4 * col];
				uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
				s[4 * col + 0] = (uint8_t)(a30s_xtime(a0) ^ (a30s_xtime(a1) ^ a1) ^ a2 ^ a3);
				s[4 * col + 1] = (uint8_t)(a0 ^ a30s_xtime(a1) ^ (a30s_xtime(a2) ^ a2) ^ a3);
				s[4 * col + 2] = (uint8_t)(a0 ^ a1 ^ a30s_xtime(a2) ^ (a30s_xtime(a3) ^ a3));
				s[4 * col + 3] = (uint8_t)((a30s_xtime(a0) ^ a0) ^ a1 ^ a2 ^ a30s_xtime(a3));
			}
		} else {
			for (int i = 0; i < 16; i++) { s[i] = t[i]; }
		}
		a30s_add_rk(s, c->rk + 16 * r);
	}
	for (int i = 0; i < 16; i++) { out[i] = s[i]; }
}

static void a30s_aes_dec_block(const struct a30s_aes_ctx *c, const uint8_t *in, uint8_t *out)
{
	uint8_t s[16], t[16];
	for (int i = 0; i < 16; i++) { s[i] = in[i]; }
	a30s_add_rk(s, c->rk + 16 * c->rounds);
	for (int r = (int)c->rounds - 1; r >= 0; r--) {
		for (int col = 0; col < 4; col++) {            /* InvShiftRows */
			for (int row = 0; row < 4; row++) { t[4 * ((col + row) & 3) + row] = s[4 * col + row]; }
		}
		for (int i = 0; i < 16; i++) { s[i] = a30s_isbox[t[i]]; }
		a30s_add_rk(s, c->rk + 16 * r);
		if (r != 0) {                                   /* InvMixColumns */
			for (int col = 0; col < 4; col++) {
				uint8_t a0 = s[4 * col], a1 = s[4 * col + 1], a2 = s[4 * col + 2], a3 = s[4 * col + 3];
				s[4 * col + 0] = (uint8_t)(a30s_mul(a0, 14) ^ a30s_mul(a1, 11) ^ a30s_mul(a2, 13) ^ a30s_mul(a3, 9));
				s[4 * col + 1] = (uint8_t)(a30s_mul(a0, 9) ^ a30s_mul(a1, 14) ^ a30s_mul(a2, 11) ^ a30s_mul(a3, 13));
				s[4 * col + 2] = (uint8_t)(a30s_mul(a0, 13) ^ a30s_mul(a1, 9) ^ a30s_mul(a2, 14) ^ a30s_mul(a3, 11));
				s[4 * col + 3] = (uint8_t)(a30s_mul(a0, 11) ^ a30s_mul(a1, 13) ^ a30s_mul(a2, 9) ^ a30s_mul(a3, 14));
			}
		}
	}
	for (int i = 0; i < 16; i++) { out[i] = s[i]; }
}

/* ------------------------------------------------------------------ ccmode_cbc glue */
static int a30s_cbc_init(const struct ccmode_cbc *mode, cccbc_ctx *ctx, size_t key_len, const void *key)
{
	(void)mode;
	return a30s_aes_expand((struct a30s_aes_ctx *)ctx, key_len, key);
}

static int a30s_cbc_enc(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out)
{
	const struct a30s_aes_ctx *c = (const struct a30s_aes_ctx *)ctx;
	uint8_t *ivb = (uint8_t *)iv;
	const uint8_t *ip = in;
	uint8_t *op = out;
	uint8_t x[16];
	while (nblocks--) {
		for (int i = 0; i < 16; i++) { x[i] = (uint8_t)(ip[i] ^ ivb[i]); }
		a30s_aes_enc_block(c, x, op);
		for (int i = 0; i < 16; i++) { ivb[i] = op[i]; }
		ip += 16; op += 16;
	}
	return 0;
}

static int a30s_cbc_dec(const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks, const void *in, void *out)
{
	const struct a30s_aes_ctx *c = (const struct a30s_aes_ctx *)ctx;
	uint8_t *ivb = (uint8_t *)iv;
	const uint8_t *ip = in;
	uint8_t *op = out;
	uint8_t save[16], x[16];
	while (nblocks--) {
		for (int i = 0; i < 16; i++) { save[i] = ip[i]; }
		a30s_aes_dec_block(c, save, x);
		for (int i = 0; i < 16; i++) { op[i] = (uint8_t)(x[i] ^ ivb[i]); ivb[i] = save[i]; }
		ip += 16; op += 16;
	}
	return 0;
}

static const struct ccmode_cbc a30s_cbc_encrypt_mode = {
	.size = sizeof(struct a30s_aes_ctx), .block_size = 16,
	.init = a30s_cbc_init, .cbc = a30s_cbc_enc, .custom = NULL,
};
static const struct ccmode_cbc a30s_cbc_decrypt_mode = {
	.size = sizeof(struct a30s_aes_ctx), .block_size = 16,
	.init = a30s_cbc_init, .cbc = a30s_cbc_dec, .custom = NULL,
};

/* ------------------------------------------------------------------ digests / hmac */
extern const struct ccdigest_info ccsha1_eay_di;
extern const struct ccdigest_info ccsha256_ltc_di;

static void a30s_digest_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, void *digest)
{
	di->final(di, ctx, digest);
}

static void a30s_digest_oneshot(const struct ccdigest_info *di, unsigned long len, const void *data, void *digest)
{
	ccdigest_di_decl(di, ctx);
	ccdigest_init(di, ctx);
	ccdigest_update(di, ctx, len, data);
	di->final(di, ctx, digest);
	ccdigest_di_clear(di, ctx);
}

static struct crypto_functions a30s_crypto_table;

void a30s_crypto_register(void);

void
a30s_crypto_register(void)
{
	if (g_crypto_funcs != NULL) {
		return;
	}
	a30s_crypto_table.ccdigest_init_fn = (ccdigest_init_fn_t)ccdigest_init;
	a30s_crypto_table.ccdigest_update_fn = (ccdigest_update_fn_t)ccdigest_update;
	a30s_crypto_table.ccdigest_final_fn = a30s_digest_final;
	a30s_crypto_table.ccdigest_fn = a30s_digest_oneshot;
	a30s_crypto_table.ccsha1_di = &ccsha1_eay_di;
	a30s_crypto_table.ccsha256_di = &ccsha256_ltc_di;
	a30s_crypto_table.cchmac_init_fn = (cchmac_init_fn_t)cchmac_init;
	a30s_crypto_table.cchmac_update_fn = (cchmac_update_fn_t)cchmac_update;
	a30s_crypto_table.cchmac_final_fn = (cchmac_final_fn_t)cchmac_final;
	a30s_crypto_table.cchmac_fn = (cchmac_fn_t)cchmac;
	a30s_crypto_table.ccaes_cbc_encrypt = &a30s_cbc_encrypt_mode;
	a30s_crypto_table.ccaes_cbc_decrypt = &a30s_cbc_decrypt_mode;
	register_crypto_functions(&a30s_crypto_table);
	printf("A30S crypto: built-in table registered (SHA1/SHA256/HMAC/AES-CBC, bring-up only)\n");
}
