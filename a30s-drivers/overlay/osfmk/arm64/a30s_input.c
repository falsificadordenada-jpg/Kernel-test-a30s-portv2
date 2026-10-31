/*
 * A30S input: botoes + touch (polling). Portado do loader bare-metal (M3a/M3b),
 * que funcionou no SM-A307G. Roda numa kernel thread depois do scheduler estar de pe.
 * Boot-arg a30s_input=0 desliga tudo (para isolar panics).
 */
#include <mach/mach_types.h>
#include <kern/kern_types.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/misc_protos.h>
#include <arm/machine_routines.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include "a30s_input.h"

#define PIN_BASE   0x11CB0000UL   /* ALIVE pin controller */
#define TOP_PIN    0x139B0000UL   /* TOP pin controller */
#define I2C4_BASE  0x13870000UL
#define MAP_SIZE   0x1000UL

#define GPA(n)     (0x40 + 0x20 * (n))      /* offset em PIN_BASE */
#define GPP4       0xA0                     /* offsets em TOP_PIN */
#define GPG2       0x160

#define IICCON 0x00
#define IICSTAT 0x04
#define IICDS 0x0C
#define IICLC 0x10
#define TP_ADDR 0x50
#define HIB(n) ((uint32_t)(n) | (0x800Bu << 16))

#define K_DN 1u   /* VOL-  gpa1 pino 6 */
#define K_UP 2u   /* VOL+  gpa1 pino 5 */
#define K_PW 4u   /* POWER gpa1 pino 7 */

static volatile uint8_t *pin, *top, *i2c;
static uint32_t idle_lv;

static inline uint32_t rd(volatile uint8_t *b, uint32_t o) { return *(volatile uint32_t *)(b + o); }
static inline void wr(volatile uint8_t *b, uint32_t o, uint32_t v)
{
	*(volatile uint32_t *)(b + o) = v;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void ms(uint32_t n) { delay_for_interval(n, 1000 * 1000); }

static uint32_t gdat(int n) { return rd(pin, GPA(n) + 4) & 0xff; }

static uint32_t raw_keys(void)
{
	uint32_t d = gdat(1) ^ idle_lv, k = 0;
	if (d & (1u << 6)) k |= K_DN;
	if (d & (1u << 5)) k |= K_UP;
	if (d & (1u << 7)) k |= K_PW;
	return k;
}

/* ---- i2c (Samsung S3C), igual ao loader ---- */
static const uint32_t ctl_ack = 0xEF, ctl_nak = 0x6F;

static int i2c_pend(void)
{
	uint64_t t0 = mach_absolute_time(), lim;
	nanoseconds_to_absolutetime(30 * 1000 * 1000, &lim);
	while (mach_absolute_time() - t0 < lim)
		if (rd(i2c, IICCON) & 0x10)
			return 0;
	return -1;
}
static void i2c_setup(void)
{
	wr(i2c, IICLC, 0x7);
	wr(i2c, IICCON, ctl_ack & ~0x10u);
	wr(i2c, IICSTAT, 0x10);
}
static int i2c_addr(uint32_t a8, uint32_t mode, int rep)
{
	wr(i2c, IICSTAT, mode | 0x10);
	wr(i2c, IICDS, a8);
	ms(1);
	if (rep)
		wr(i2c, IICCON, ctl_ack & ~0x10u);
	wr(i2c, IICSTAT, mode | 0x30);
	if (i2c_pend()) return -1;
	if (rd(i2c, IICSTAT) & 1) return -1;
	return 0;
}
static void i2c_stop(uint32_t mode)
{
	wr(i2c, IICSTAT, mode | 0x10);
	wr(i2c, IICCON, ctl_nak & ~0x10u);
	for (int i = 0; i < 20 && (rd(i2c, IICSTAT) & 0x20); i++)
		ms(1);
	wr(i2c, IICSTAT, 0x10);
}
static int i2c_rd32(uint32_t reg, uint32_t *v)
{
	if (i2c_addr(TP_ADDR << 1, 0xC0, 0)) { i2c_stop(0xC0); return -1; }
	for (int i = 0; i < 4; i++) {
		wr(i2c, IICDS, (reg >> (24 - 8 * i)) & 0xff);
		wr(i2c, IICCON, ctl_ack & ~0x10u);
		if (i2c_pend() || (rd(i2c, IICSTAT) & 1)) { i2c_stop(0xC0); return -1; }
	}
	if (i2c_addr((TP_ADDR << 1) | 1, 0x80, 1)) { i2c_stop(0x80); return -1; }
	uint32_t x = 0;
	for (int i = 0; i < 4; i++) {
		wr(i2c, IICCON, (i == 3 ? ctl_nak : ctl_ack) & ~0x10u);
		if (i2c_pend()) { i2c_stop(0x80); return -1; }
		x = (x << 8) | (rd(i2c, IICDS) & 0xff);
	}
	i2c_stop(0x80);
	*v = x;
	return 0;
}

static void a30s_input_thread(void *arg, wait_result_t w)
{
	(void)arg; (void)w;
	uint32_t stable = 0, last = 0, errs = 0;
	uint64_t tlast = mach_absolute_time(), deb;
	uint32_t lx = 0xffffffff, ly = 0xffffffff, tcnt_last = 0;
	nanoseconds_to_absolutetime(30 * 1000 * 1000, &deb);

	while (1) {
		/* botoes, debounce 30 ms; VOL+ e VOL- juntos = POWER */
		uint32_t r = raw_keys();
		uint64_t t = mach_absolute_time();
		if (r != last) { last = r; tlast = t; }
		if (r != stable && t - tlast > deb) {
			uint32_t ev = r & ~stable;
			stable = r;
			if ((r & (K_UP | K_DN)) == (K_UP | K_DN) && (ev & (K_UP | K_DN)))
				ev = K_PW;
			if (ev & K_UP) printf("A30S KEY: VOL+\n");
			if (ev & K_DN) printf("A30S KEY: VOL-\n");
			if (ev & K_PW) printf("A30S KEY: POWER\n");
		}

		/* touch: status/msg/coord0 */
		uint32_t msg = 0, c0 = 0, st = 0;
		int bad = i2c_rd32(HIB(0x30000100), &st);
		bad |= i2c_rd32(HIB(0x30000104), &msg);
		bad |= i2c_rd32(HIB(0x30000108), &c0);
		if (bad) {
			if (++errs == 1 || (errs % 500) == 0)
				printf("A30S TOUCH: erro i2c (%u)\n", errs);
		} else {
			uint32_t cnt = (msg >> 12) & 0xF;
			uint32_t x = (c0 >> 12) & 0xFFF, y = c0 & 0xFFF;
			if (cnt && cnt < 6 && (x != lx || y != ly || cnt != tcnt_last))
				printf("A30S TOUCH: n=%u x=%u y=%u\n", cnt, x, y);
			if (!cnt && tcnt_last)
				printf("A30S TOUCH: soltou\n");
			lx = x; ly = y; tcnt_last = (cnt && cnt < 6) ? cnt : 0;
		}
		ms(20);
	}
}

void a30s_input_start(void)
{
	uint32_t off = 1;
	if (PE_parse_boot_argn("a30s_input", &off, sizeof(off)) && off == 0) {
		printf("A30S input: desligado (a30s_input=0)\n");
		return;
	}
	pin = (volatile uint8_t *)ml_io_map(PIN_BASE, MAP_SIZE);
	top = (volatile uint8_t *)ml_io_map(TOP_PIN, MAP_SIZE);
	i2c = (volatile uint8_t *)ml_io_map(I2C4_BASE, MAP_SIZE);
	if (!pin || !top || !i2c) {
		printf("A30S input: ml_io_map falhou\n");
		return;
	}
	/* botoes: gpa1 pinos 5..7 como entrada; guarda nivel ocioso */
	wr(pin, GPA(1), rd(pin, GPA(1)) & 0x000FFFFF);
	ms(50);
	idle_lv = gdat(1) & 0xE0;
	/* touch: LDO (gpg2 pino 1) ligado, i2c4 nos pinos gpp4 0/1 (funcao 2, pull-up), irq gpa0-0 entrada */
	wr(top, GPG2, (rd(top, GPG2) & ~0xF0u) | 0x10);
	wr(top, GPG2 + 4, rd(top, GPG2 + 4) | 2);
	wr(top, GPP4, (rd(top, GPP4) & ~0xFFu) | 0x22);
	wr(top, GPP4 + 8, (rd(top, GPP4 + 8) & ~0xFFu) | 0x33);
	wr(pin, GPA(0), rd(pin, GPA(0)) & ~0xFu);
	ms(300);
	i2c_setup();

	thread_t th;
	if (kernel_thread_start((thread_continue_t)a30s_input_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
		printf("A30S input: botoes + touch ativos\n");
	}
}
