/*
 * Board header for the Samsung Galaxy A30s (Exynos 7904, 2x Cortex-A73 +
 * 6x Cortex-A53).  Generic ARMv8 board class, modelled on the BCM2837
 * (Raspberry Pi 3) class that ships in xnu-4903: no Apple implementation-
 * defined system registers, ARM architectural timer, GICv2 instead of AIC.
 */
#ifndef _PEXPERT_ARM_A30S_H
#define _PEXPERT_ARM_A30S_H

#ifdef A30S
#include "arm64_common.h"
#endif

#define NO_MONITOR 1
#define NO_ECORE   1

#ifndef ASSEMBLER

/* GICv2 (Exynos 7904).  Physical addresses; the ADT carries the same values
 * relative to the "arm-io" ranges base below. */
#define A30S_SOC_BASE_PHYS      0x10000000UL
#define A30S_GICD_PHYS          0x12301000UL
#define A30S_GICD_SIZE          0x1000UL
#define A30S_GICC_PHYS          0x12302000UL
#define A30S_GICC_SIZE          0x2000UL

/* ARM architectural timer interrupts (GIC PPI numbers). */
#define A30S_PPI_PHYS_TIMER     30      /* CNTP, used by xnu on arm64 */
#define A30S_PPI_VIRT_TIMER     27

#define A30S_TIMEBASE_HZ        26000000UL

/* RAM reserved for the kernel panic log (outside the memory given to xnu). */
#define PLATFORM_PANIC_LOG_PADDR        0xBFFFC000
#define PLATFORM_PANIC_LOG_SIZE         16384

#endif /* ! ASSEMBLER */
#endif /* ! _PEXPERT_ARM_A30S_H */
