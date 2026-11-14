/*
 * A30S input: buttons + Imagis touch controller (polling).
 *
 * Ported from the bare-metal loader drivers (M3a/M3b) that worked on the SM-A307G.
 * Runs on a kernel thread once the scheduler is up.  See a30s_input.h for the
 * boot-args.  The I2C engine is the Samsung S3C2410 style block.
 *
 * Lessons learned on hardware (M4v32..M4v50), now in the code:
 *   - a read of a 32-bit register needs a REPEATED START (not stop + start);
 *   - the polling thread can be preempted for a long time during boot, so the
 *     I2C waits use a 500 ms timeout (30 ms gave spurious timeouts);
 *   - registers 0x...100 and the upper bits of 0x...104 are frame counters, not
 *     touch data: they must be ignored when deciding whether something changed.
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

/* Explicit prototype: without it the 64-bit mapping address could be truncated to int. */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define PIN_BASE   0x11CB0000UL   /* ALIVE pin controller */
#define TOP_PIN    0x139B0000UL   /* TOP pin controller */
#define I2C4_BASE  0x13870000UL
#define MAP_SIZE   0x1000UL

#define GPA(n)     (0x40 + 0x20 * (n))      /* offsets in PIN_BASE */
#define GPP4       0xA0                     /* offsets in TOP_PIN */
#define GPG2       0x160

#define IICCON  0x00
#define IICSTAT 0x04
#define IICDS   0x0C
#define IICLC   0x10
#define TP_ADDR 0x50
#define HIB(n)  ((uint32_t)(n) | (0x800Bu << 16))

#define TP_CHIPID_REG   0xC0001000u   /* direct-access chip id */
#define TP_MSG_DEFAULT  0xB00B0104u   /* interrupt message */
#define TP_COORD_DEFAULT 0xB00B0108u  /* finger 0 coordinates */

#define I2C_TIMEOUT_NS  (500ull * 1000 * 1000)

#define K_DN 1u   /* VOL-  gpa1 pin 6 */
#define K_UP 2u   /* VOL+  gpa1 pin 5 */
#define K_PW 4u   /* POWER gpa1 pin 7 */

static volatile uint8_t *pin, *top, *i2c;
static uint32_t idle_lv;
static uint32_t tp_msg_reg = TP_MSG_DEFAULT, tp_coord_reg = TP_COORD_DEFAULT;
static uint32_t tp_use_irq, tp_dump_every;

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

/* ---- I2C (Samsung S3C) ---- */
static const uint32_t ctl_ack = 0xEF, ctl_nak = 0x6F;

/* Wait for the interrupt-pending bit (IICCON bit 4). */
static int i2c_pend(void)
{
	uint64_t t0 = mach_absolute_time(), lim;
	nanoseconds_to_absolutetime(I2C_TIMEOUT_NS, &lim);
	while (mach_absolute_time() - t0 < lim) {
		if (rd(i2c, IICCON) & 0x10)
			return 0;
	}
	return -1;
}

static void i2c_setup(void)
{
	wr(i2c, IICLC, 0x7);
	wr(i2c, IICCON, ctl_ack & ~0x10u);
	wr(i2c, IICSTAT, 0x10);
}

/* Send START + address byte; returns -1 on timeout or NACK. */
static int i2c_start(uint32_t addr8, uint32_t mode)
{
	wr(i2c, IICSTAT, mode | 0x10);
	wr(i2c, IICDS, addr8);
	ms(1);
	wr(i2c, IICSTAT, mode | 0x30);
	if (i2c_pend())
		return -1;
	if (rd(i2c, IICSTAT) & 1)
		return -1;
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

/* Read one 32-bit big-endian register: write 4 address bytes, repeated START, read 4 bytes. */
static int i2c_rd32(uint32_t reg, uint32_t *v)
{
	if (i2c_start(TP_ADDR << 1, 0xC0)) {
		i2c_stop(0xC0);
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		wr(i2c, IICDS, (reg >> (24 - 8 * i)) & 0xff);
		wr(i2c, IICCON, ctl_ack & ~0x10u);
		if (i2c_pend() || (rd(i2c, IICSTAT) & 1)) {
			i2c_stop(0xC0);
			return -1;
		}
	}
	/* Repeated START in master-receive mode: data first, then START, then release. */
	wr(i2c, IICDS, (TP_ADDR << 1) | 1);
	wr(i2c, IICSTAT, 0xB0);
	ms(1);
	wr(i2c, IICCON, ctl_ack & ~0x10u);
	if (i2c_pend() || (rd(i2c, IICSTAT) & 1)) {
		i2c_stop(0x80);
		return -1;
	}
	uint32_t x = 0;
	wr(i2c, IICCON, ctl_ack & ~0x10u);       /* release: first data byte */
	for (int i = 0; i < 4; i++) {
		if (i2c_pend()) {
			i2c_stop(0x80);
			return -1;
		}
		x = (x << 8) | (rd(i2c, IICDS) & 0xff);
		if (i < 3)
			wr(i2c, IICCON, (i == 2 ? ctl_nak : ctl_ack) & ~0x10u);
	}
	i2c_stop(0x80);
	*v = x;
	return 0;
}

/* The IRQ line of the touch chip (gpa0-0) is active when it differs from its idle level. */
static uint32_t tp_irq_idle;
static int tp_irq_active(void)
{
	return (gdat(0) & 1) != tp_irq_idle;
}

static void a30s_input_thread(void *arg, wait_result_t w)
{
	(void)arg; (void)w;
	uint32_t stable = 0, last = 0, errs = 0, polls = 0;
	uint64_t tlast = mach_absolute_time(), deb;
	uint32_t last_m = 0xffffffff, last_c0 = 0xffffffff;
	int touching = 0;
	nanoseconds_to_absolutetime(30 * 1000 * 1000, &deb);

	while (1) {
		/* buttons, 30 ms debounce; VOL+ and VOL- together = POWER */
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

		/* touch: message word + finger 0 coordinates */
		if (!tp_use_irq || tp_irq_active()) {
			uint32_t m = 0, c0 = 0;
			int bad = i2c_rd32(tp_msg_reg, &m);
			bad |= i2c_rd32(tp_coord_reg, &c0);
			polls++;
			if (bad) {
				if (++errs == 1 || (errs % 500) == 0)
					printf("A30S TOUCH: i2c error (%u)\n", errs);
			} else {
				/* Mainline imagis layout: m[9:0] = pressed fingers, c0 = x[23:12] y[11:0].
				 * m[15:11] is a frame counter on this chip and is ignored. */
				uint32_t pressed = m & 0x3FF;
				uint32_t x = (c0 >> 12) & 0xFFF, y = c0 & 0xFFF;
				if ((pressed || c0) && (m != last_m || c0 != last_c0)) {
					if (!touching || x != 0 || y != 0)
						printf("A30S TOUCH: pressed=0x%x x=%u y=%u raw m=%x c0=%x\n",
						    pressed, x, y, m, c0);
					touching = 1;
				} else if (!pressed && !c0 && touching) {
					printf("A30S TOUCH: released\n");
					touching = 0;
				}
				last_m = m;
				last_c0 = c0;
				if (tp_dump_every && (polls % tp_dump_every) == 0)
					printf("A30S TOUCH dump: m=%x c0=%x\n", m, c0);
			}
		}
		ms(20);
	}
}

void a30s_input_start(void)
{
	uint32_t v = 1, tp_on = 1;
	if (PE_parse_boot_argn("a30s_input", &v, sizeof(v)) && v == 0) {
		printf("A30S input: disabled (a30s_input=0)\n");
		return;
	}
	PE_parse_boot_argn("a30s_tp", &tp_on, sizeof(tp_on));
	PE_parse_boot_argn("a30s_tp_msg", &tp_msg_reg, sizeof(tp_msg_reg));
	PE_parse_boot_argn("a30s_tp_coord", &tp_coord_reg, sizeof(tp_coord_reg));
	PE_parse_boot_argn("a30s_tp_irq", &tp_use_irq, sizeof(tp_use_irq));
	PE_parse_boot_argn("a30s_tp_dump", &tp_dump_every, sizeof(tp_dump_every));

	pin = (volatile uint8_t *)ml_io_map(PIN_BASE, MAP_SIZE);
	top = (volatile uint8_t *)ml_io_map(TOP_PIN, MAP_SIZE);
	i2c = (volatile uint8_t *)ml_io_map(I2C4_BASE, MAP_SIZE);
	if (!pin || !top || !i2c) {
		printf("A30S input: ml_io_map failed\n");
		return;
	}
	/* buttons: gpa1 pins 5..7 as inputs; remember the idle level */
	wr(pin, GPA(1), rd(pin, GPA(1)) & 0x000FFFFF);
	ms(50);
	idle_lv = gdat(1) & 0xE0;
	if (!tp_on) {
		printf("A30S input: buttons active, touch disabled (a30s_tp=0)\n");
	} else {
		/* touch: LDO (gpg2 pin 1) on, i2c4 on gpp4 pins 0/1 (function 2, pull-up), IRQ gpa0-0 input */
		wr(top, GPG2, (rd(top, GPG2) & ~0xF0u) | 0x10);
		wr(top, GPG2 + 4, rd(top, GPG2 + 4) | 2);
		wr(top, GPP4, (rd(top, GPP4) & ~0xFFu) | 0x22);
		wr(top, GPP4 + 8, (rd(top, GPP4 + 8) & ~0xFFu) | 0x33);
		wr(pin, GPA(0), rd(pin, GPA(0)) & ~0xFu);
		ms(300);
		tp_irq_idle = gdat(0) & 1;
		i2c_setup();
		{
			uint32_t id = 0;
			if (i2c_rd32(TP_CHIPID_REG, &id) == 0)
				printf("A30S TOUCH: chip id %x, irq idle level %u\n", id, tp_irq_idle);
			else
				printf("A30S TOUCH: chip id read failed\n");
		}
	}

	thread_t th;
	if (kernel_thread_start((thread_continue_t)a30s_input_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
		printf("A30S input: buttons%s active\n", tp_on ? " + touch" : "");
	}
}
