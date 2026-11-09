#!/usr/bin/env python3
"""A30S early-boot fixes found on the first run of the from-source kernel.
Usage: apply_a30s_prng.py <xnu-root>

  1. Software PRNG (osfmk/prng/a30s_prng.c): there is no corecrypto kext, so nothing called
     register_and_init_prng() and read_random() crashed in lck_mtx_lock() (NULL mutex).
     It is registered in kernel_bootstrap_thread() right before the first read_random().
  2. SavePanicInfo(): the A30S has no iBoot panic region, so panic_info->eph_panic_log_offset is 0
     and the assert turned EVERY panic into a nested panic that hid the real message.
     Under A30S it now returns early, so the original panic text stays on screen.
Every edit is an exact-string replacement that must match once (idempotent).
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def edit(root, rel, old, new, marker):
    path = os.path.join(root, rel)
    text = open(path).read()
    if marker in text:
        print("  already applied: %s (%s)" % (rel, marker))
        return
    if text.count(old) != 1:
        sys.exit("ERROR: %s: expected exactly 1 match, found %d for:\n%s"
                 % (rel, text.count(old), old))
    open(path, "w").write(text.replace(old, new))
    print("  patched: %s (%s)" % (rel, marker))


def main(root):
    rel = "osfmk/prng/a30s_prng.c"
    dst = os.path.join(root, rel)
    shutil.copyfile(os.path.join(HERE, "overlay", rel), dst)
    print("  copied: %s" % rel)

    edit(root, "osfmk/conf/files",
         "osfmk/prng/prng_random.c\t\tstandard\n",
         "osfmk/prng/prng_random.c\t\tstandard\nosfmk/prng/a30s_prng.c\t\tstandard\n",
         "a30s_prng.c")

    edit(root, "osfmk/kern/startup.c",
         "\tread_random(&vm_kernel_addrperm, sizeof(vm_kernel_addrperm));\n",
         "#if defined(ARM64_BOARD_CONFIG_A30S)\n"
         "\t{\n\t\textern void a30s_prng_register(void);\n\t\ta30s_prng_register();\n\t}\n"
         "#endif\n"
         "\tread_random(&vm_kernel_addrperm, sizeof(vm_kernel_addrperm));\n",
         "a30s_prng_register")

    edit(root, "osfmk/arm/model_dep.c",
         "\tassert(panic_info->eph_panic_log_offset != 0);\n\n\tif (panic_options & DEBUGGER_OPTION_PANICLOGANDREBOOT) {\n",
         "#if defined(ARM64_BOARD_CONFIG_A30S)\n"
         "\t/* A30S: no iBoot panic region, so there is nothing to save; keep the original panic text visible. */\n"
         "\tif (panic_info == NULL || panic_info->eph_panic_log_offset == 0) {\n\t\treturn;\n\t}\n"
         "#else\n"
         "\tassert(panic_info->eph_panic_log_offset != 0);\n"
         "#endif\n\n\tif (panic_options & DEBUGGER_OPTION_PANICLOGANDREBOOT) {\n",
         "A30S: no iBoot panic region")
    print("A30S early-boot fixes applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
