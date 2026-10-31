/* GICv2 driver interface for the A30S board class. */
#ifndef _ARM64_A30S_GIC_H
#define _ARM64_A30S_GIC_H

#include <stdint.h>

typedef void (*a30s_irq_fn_t)(void *arg);

/* Called once on the boot CPU with the mapped (virtual) GIC bases. */
void a30s_gic_init(unsigned long gicd_virt, unsigned long gicc_virt);
/* Per-CPU banked setup (CPU interface + PPIs). Call on every CPU. */
void a30s_gic_cpu_init(void);
/* Register and enable a shared peripheral interrupt (SPI, id >= 32). */
int  a30s_gic_register(uint32_t irq, a30s_irq_fn_t fn, void *arg);

#endif
