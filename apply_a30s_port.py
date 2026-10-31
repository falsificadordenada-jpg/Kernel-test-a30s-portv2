#!/usr/bin/env python3
"""Apply the A30S (Samsung Galaxy A30s / Exynos 7904) board port to an
xnu-4903.270.47 source tree.

Usage: apply_a30s_port.py /path/to/xnu

Idempotent: re-running on an already patched tree changes nothing.  Every
edit is an exact-string replacement that must match exactly once; if the
tree differs from xnu-4903.270.47 the script stops with an error.
Build afterwards with:  MACHINE_CONFIG=A30S  (see docs/BRINGUP.md).
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MARK = "A30S"

def edit(root, rel, old, new, once_marker=None):
    p = os.path.join(root, rel)
    s = open(p).read()
    if once_marker and once_marker in s:
        print("  skip (already applied):", rel, "-", once_marker[:40].strip())
        return
    if s.count(old) != 1:
        sys.exit("ERROR: %s: expected exactly 1 match, found %d for:\n%s"
                 % (rel, s.count(old), old))
    open(p, "w").write(s.replace(old, new))
    print("  patched:", rel)

def main(root):
    # 1. new files
    for dp, _, fs in os.walk(os.path.join(HERE, "overlay")):
        for f in fs:
            src = os.path.join(dp, f)
            rel = os.path.relpath(src, os.path.join(HERE, "overlay"))
            dst = os.path.join(root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            print("  copied:", rel)

    # 2. machine config registration
    edit(root, "makedefs/MakeInc.def",
         "T8010 T8011 BCM2837\n", "T8010 T8011 BCM2837 A30S\n",
         once_marker="BCM2837 A30S")
    edit(root, "makedefs/MakeInc.def",
         "MACHINE_FLAGS_ARM64_BCM2837 = -DARM64_BOARD_CONFIG_BCM2837\n",
         "MACHINE_FLAGS_ARM64_BCM2837 = -DARM64_BOARD_CONFIG_BCM2837\n"
         "MACHINE_FLAGS_ARM64_A30S = -DARM64_BOARD_CONFIG_A30S\n",
         once_marker="MACHINE_FLAGS_ARM64_A30S")
    edit(root, "pexpert/pexpert/arm64/Makefile",
         "\tBCM2837.h\n", "\tBCM2837.h \\\n\tA30S.h\n", once_marker="A30S.h")
    edit(root, "pexpert/pexpert/arm64/board_config.h",
         "#endif  /* ARM64_BOARD_CONFIG_BCM2837 */\n",
         "#endif  /* ARM64_BOARD_CONFIG_BCM2837 */\n\n"
         "#ifdef ARM64_BOARD_CONFIG_A30S\n"
         "#define A30S\n"
         "#define A30S_BRINGUP\n"
         "#define ARM_ARCH_TIMER\n"
         "#include <pexpert/arm64/A30S.h>\n"
         "#define __ARM_L2CACHE_SIZE_LOG__ 19\n"
         "#define ARM_BOARD_CLASS_A30S\n"
         "#define CPU_COUNT 8\n"
         "#endif  /* ARM64_BOARD_CONFIG_A30S */\n",
         once_marker="ARM64_BOARD_CONFIG_A30S")

    # 3. CPU/cache definitions: treat A30S like the generic BCM2837 class
    pr = "osfmk/arm/proc_reg.h"
    edit(root, pr, "#elif defined (BCM2837)\n#define __ARM_ARCH__    8",
         "#elif defined (BCM2837) || defined (A30S)\n#define __ARM_ARCH__    8",
         once_marker="defined (BCM2837) || defined (A30S)\n#define __ARM_ARCH__")
    edit(root, pr, "#if defined (ARMA7) || defined (APPLE_ARM64_ARCH_FAMILY) || defined (BCM2837)\n",
         "#if defined (ARMA7) || defined (APPLE_ARM64_ARCH_FAMILY) || defined (BCM2837) || defined (A30S)\n",
         once_marker="defined (BCM2837) || defined (A30S)\n\n#define TEST_FSR")
    edit(root, pr, "#elif defined (BCM2837) /* Raspberry Pi 3 */",
         "#elif defined (BCM2837) || defined (A30S) /* generic Cortex-A53/A73 */",
         once_marker="|| defined (A30S) /* generic")
    edit(root, "osfmk/arm64/machine_routines.c",
         "#if defined(APPLE_ARM64_ARCH_FAMILY) || defined(BCM2837)\n\t/* Could fill in our own ops here",
         "#if defined(APPLE_ARM64_ARCH_FAMILY) || defined(BCM2837) || defined(A30S)\n\t/* Could fill in our own ops here",
         once_marker="|| defined(A30S)\n\t/* Could fill")
    edit(root, "osfmk/conf/files.arm64",
         "osfmk/arm64/machine_routines.c\t\tstandard\n",
         "osfmk/arm64/machine_routines.c\t\tstandard\nosfmk/arm64/a30s_gic.c\t\t\tstandard\n",
         once_marker="a30s_gic.c")

    # 4. platform expert: GIC mapping + timer/interrupt bring-up
    pe = "pexpert/arm/pe_identify_machine.c"
    edit(root, pe,
         "#if defined(ARM_BOARD_CLASS_BCM2837)\nstatic struct tbd_ops    bcm2837_funcs = {NULL, NULL, NULL};\n#endif /* defined(ARM_BOARD_CLASS_BCM2837) */\n",
         "#if defined(ARM_BOARD_CLASS_BCM2837)\nstatic struct tbd_ops    bcm2837_funcs = {NULL, NULL, NULL};\n#endif /* defined(ARM_BOARD_CLASS_BCM2837) */\n\n"
         "#if defined(ARM_BOARD_CLASS_A30S)\n"
         "static struct tbd_ops    a30s_funcs = {NULL, NULL, NULL};\n"
         "extern void a30s_gic_init(unsigned long gicd_virt, unsigned long gicc_virt);\n"
         "extern void a30s_gic_cpu_init(void);\n"
         "vm_offset_t gPicCpuBase;\n"
         "#endif /* defined(ARM_BOARD_CLASS_A30S) */\n",
         once_marker="a30s_funcs = {NULL")
    edit(root, pe,
         "\t\tkprintf(\"pe_arm_map_interrupt_controller: gPicBase: 0x%lx\\n\", (unsigned long)gPicBase);\n",
         "\t\tkprintf(\"pe_arm_map_interrupt_controller: gPicBase: 0x%lx\\n\", (unsigned long)gPicBase);\n"
         "#if defined(ARM_BOARD_CLASS_A30S)\n"
         "\t\t/* GICv2: second reg pair is the CPU interface. */\n"
         "\t\tgPicCpuBase = ml_io_map(soc_phys + *(reg_prop + 2), *(reg_prop + 3));\n"
         "\t\tkprintf(\"pe_arm_map_interrupt_controller: gPicCpuBase: 0x%lx\\n\", (unsigned long)gPicCpuBase);\n"
         "#endif\n",
         once_marker="gPicCpuBase: 0x")
    edit(root, pe,
         "#if defined(ARM_BOARD_CLASS_BCM2837)\n\tif (!strcmp(gPESoCDeviceType, \"bcm2837-io\")) {\n\t\ttbd_funcs = &bcm2837_funcs;\n\t} else\n#endif\n",
         "#if defined(ARM_BOARD_CLASS_BCM2837)\n\tif (!strcmp(gPESoCDeviceType, \"bcm2837-io\")) {\n\t\ttbd_funcs = &bcm2837_funcs;\n\t} else\n#endif\n"
         "#if defined(ARM_BOARD_CLASS_A30S)\n"
         "\tif (!strcmp(gPESoCDeviceType, \"a30s-io\")) {\n"
         "\t\ttbd_funcs = &a30s_funcs;\n"
         "\t\tif (args != NULL) {\n"
         "\t\t\ta30s_gic_init(gPicBase, gPicCpuBase);\n"
         "\t\t} else {\n"
         "\t\t\ta30s_gic_cpu_init();\n"
         "\t\t}\n"
         "\t} else\n#endif\n",
         once_marker="a30s_funcs;\n\t\tif (args")
    print("A30S port applied to", root)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
