/*
 * GICv2 driver and interrupt entry for the A30S board class.
 *
 * xnu on Apple SoCs only understands the Apple Interrupt Controller (driven
 * by a closed kext) and delivers the timer as FIQ.  A GICv2 system has no
 * FIQ for the timer in the non-secure world, so this file installs the
 * handler that sleh_irq() calls (cpu_data->interrupt_handler), acknowledges
 * the GIC, and feeds the architectural-timer PPI into rtclock_intr() exactly
 * like sleh_fiq() does.
 */
#include <pexpert/arm64/board_config.h>

#if defined(ARM_BOARD_CLASS_A30S)

#include <kern/debug.h>
#include <kern/kern_types.h>
#include <arm/cpu_data_internal.h>
#include <arm/rtclock.h>
#include <arm64/proc_reg.h>
#include <pexpert/pexpert.h>
#include <arm64/a30s_gic.h>

/* GIC distributor */
#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_IIDR       0x008
#define GICD_IGROUPR    0x080
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xC00
/* GIC CPU interface */
#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_BPR        0x008
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010
#define GICC_IIDR       0x0FC

#define GIC_MAX_IRQ     1020
#define GIC_SPURIOUS    1020

static volatile uint32_t *gicd;
static volatile uint32_t *gicc;
static uint32_t gic_nirq;

static struct {
	a30s_irq_fn_t fn;
	void *arg;
} gic_table[GIC_MAX_IRQ];

static inline uint32_t
rd(volatile uint32_t *base, uint32_t off)
{
	return base[off / 4];
}

static inline void
wr(volatile uint32_t *base, uint32_t off, uint32_t v)
{
	base[off / 4] = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
a30s_gic_irq_handler(void *target, void *refCon, void *nub, int source)
{
#pragma unused(target, refCon, nub, source)
	for (int n = 0; n < 32; n++) {
		uint32_t iar = rd(gicc, GICC_IAR);
		uint32_t irq = iar & 0x3ff;

		if (irq >= GIC_SPURIOUS) {
			break;
		}
		if (irq == A30S_PPI_PHYS_TIMER) {
			/* Same work sleh_fiq() does for the timer. */
			getCpuDatap()->cpu_decrementer = -1;
			rtclock_intr(TRUE);
		} else if (irq < GIC_MAX_IRQ && gic_table[irq].fn != NULL) {
			gic_table[irq].fn(gic_table[irq].arg);
		} else {
			kprintf("a30s_gic: unhandled irq %u\n", irq);
		}
		wr(gicc, GICC_EOIR, iar);
	}
}

void
a30s_gic_cpu_init(void)
{
	/* PPIs/SGIs are banked per CPU. */
	wr(gicd, GICD_ICENABLER, 0xffffffff);
	wr(gicd, GICD_IGROUPR, 0xffffffff);             /* non-secure group 1 */
	for (uint32_t i = 0; i < 32; i += 4) {
		wr(gicd, GICD_IPRIORITYR + i, 0x80808080);
	}
	wr(gicd, GICD_ISENABLER, 1u << A30S_PPI_PHYS_TIMER);

	wr(gicc, GICC_PMR, 0xf0);
	wr(gicc, GICC_BPR, 0);
	wr(gicc, GICC_CTLR, 1);
}

int
a30s_gic_register(uint32_t irq, a30s_irq_fn_t fn, void *arg)
{
	if (irq < 32 || irq >= gic_nirq || irq >= GIC_MAX_IRQ) {
		return -1;
	}
	gic_table[irq].fn = fn;
	gic_table[irq].arg = arg;
	wr(gicd, GICD_IPRIORITYR + (irq & ~3u), 0x80808080);
	wr(gicd, GICD_ITARGETSR + (irq & ~3u), 0x01010101);  /* CPU interface 0 */
	wr(gicd, GICD_ISENABLER + 4 * (irq / 32), 1u << (irq % 32));
	return 0;
}

void
a30s_gic_init(unsigned long gicd_virt, unsigned long gicc_virt)
{
	cpu_data_t *cdp = getCpuDatap();

	gicd = (volatile uint32_t *)gicd_virt;
	gicc = (volatile uint32_t *)gicc_virt;

	uint32_t typer = rd(gicd, GICD_TYPER);
	gic_nirq = 32 * ((typer & 0x1f) + 1);
	if (gic_nirq > GIC_MAX_IRQ) {
		gic_nirq = GIC_MAX_IRQ;
	}
	kprintf("a30s_gic: GICD_TYPER=0x%x IIDR=0x%x GICC_IIDR=0x%x nirq=%u\n",
	    typer, rd(gicd, GICD_IIDR), rd(gicc, GICC_IIDR), gic_nirq);
	kprintf("a30s_gic: GICD_CTLR=0x%x GICC_CTLR=0x%x PMR=0x%x\n",
	    rd(gicd, GICD_CTLR), rd(gicc, GICC_CTLR), rd(gicc, GICC_PMR));

	wr(gicd, GICD_CTLR, 0);
	for (uint32_t i = 32; i < gic_nirq; i += 32) {
		wr(gicd, GICD_ICENABLER + 4 * (i / 32), 0xffffffff);
		wr(gicd, GICD_ICPENDR + 4 * (i / 32), 0xffffffff);
		wr(gicd, GICD_IGROUPR + 4 * (i / 32), 0xffffffff);
	}
	for (uint32_t i = 32; i < gic_nirq; i += 4) {
		wr(gicd, GICD_IPRIORITYR + i, 0x80808080);
		wr(gicd, GICD_ITARGETSR + i, 0x01010101);
	}
	for (uint32_t i = 32; i < gic_nirq; i += 16) {
		wr(gicd, GICD_ICFGR + i / 4, 0);        /* level-sensitive */
	}
	wr(gicd, GICD_CTLR, 1);

	a30s_gic_cpu_init();

	/* Install the handler that sleh_irq() runs (see ml_install_interrupt_handler). */
	cdp->interrupt_nub = NULL;
	cdp->interrupt_source = 0;
	cdp->interrupt_target = NULL;
	cdp->interrupt_handler = (IOInterruptHandler)a30s_gic_irq_handler;
	cdp->interrupt_refCon = NULL;
	cdp->interrupts_enabled = TRUE;

	kprintf("a30s_gic: after init GICD_CTLR=0x%x GICC_CTLR=0x%x ISENABLER0=0x%x IGROUPR0=0x%x\n",
	    rd(gicd, GICD_CTLR), rd(gicc, GICC_CTLR),
	    rd(gicd, GICD_ISENABLER), rd(gicd, GICD_IGROUPR));
}

#endif /* ARM_BOARD_CLASS_A30S */
