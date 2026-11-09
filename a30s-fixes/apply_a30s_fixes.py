#!/usr/bin/env python3
"""Source-level versions of the binary patches proven on the device (M4v32..M4v50).

Usage: apply_a30s_fixes.py <xnu-root>      (runs after apply_a30s_port.py)

Every edit is an exact-string replacement that must match exactly once, and
is skipped when its marker is already present (idempotent).

  1. ml_parse_cpu_topology(): call ml_init_max_cpus(1) so ml_get_max_cpus() works
     when no AppleARMPE kext is present (commpage population asserted on it).
  2. StartIOKit(): do not set kIOWaitQuietPanics (waitQuiet timeouts panic when no
     kext matches).
  3. kdp_init(): skipped, so panic text goes to the screen instead of a KDP serial wait.
  4. vc_enable(): skipped, so the console does not build the font cache (it hung).
  5. Boot framebuffer mapping: write-combined instead of device memory (fast scrolling).
"""
import os
import sys


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
    edit(root, "osfmk/arm64/machine_routines.c",
         "\tif (avail_cpus == 0) {\n\t\tpanic(\"No cpus found!\");\n\t}\n",
         "\tif (avail_cpus == 0) {\n\t\tpanic(\"No cpus found!\");\n\t}\n"
         "#if defined(ARM64_BOARD_CONFIG_A30S)\n"
         "\t/* No AppleARMPE kext on this board: set the CPU limits here. */\n"
         "\tml_init_max_cpus(1);\n"
         "#endif\n",
         "ml_init_max_cpus(1);")

    edit(root, "iokit/Kernel/IOStartIOKit.cpp",
         "#if DEVELOPMENT || DEBUG\n\telse {\n\t\tgIOKitDebug |= kIOWaitQuietPanics;\n\t}\n#endif /* DEVELOPMENT || DEBUG */\n",
         "#if 0 /* A30S: do not set kIOWaitQuietPanics (no kexts to match) */\n"
         "\telse {\n\t\tgIOKitDebug |= kIOWaitQuietPanics;\n\t}\n#endif\n",
         "A30S: do not set kIOWaitQuietPanics")

    edit(root, "osfmk/kern/startup.c",
         "#if MACH_KDP\n\tkernel_bootstrap_log(\"kdp_init\");\n",
         "#if MACH_KDP && !defined(ARM64_BOARD_CONFIG_A30S)\n\tkernel_bootstrap_log(\"kdp_init\");\n",
         "MACH_KDP && !defined(ARM64_BOARD_CONFIG_A30S)")

    edit(root, "osfmk/console/video_console.c",
         "vc_enable(boolean_t enable)\n{\n\tvc_render_font(enable ? vinfo.v_depth : 0);\n}\n",
         "vc_enable(boolean_t enable)\n{\n"
         "#if defined(ARM64_BOARD_CONFIG_A30S)\n"
         "\t/* A30S: skip the font cache (vc_render_font); it hung early boot. */\n"
         "\t(void)enable;\n"
         "#else\n"
         "\tvc_render_font(enable ? vinfo.v_depth : 0);\n"
         "#endif\n}\n",
         "A30S: skip the font cache")

    edit(root, "osfmk/console/video_console.c",
         "\t\t\t    unsigned int flags = VM_WIMG_IO;\n",
         "#if defined(ARM64_BOARD_CONFIG_A30S)\n"
         "\t\t\t    unsigned int flags = VM_WIMG_WCOMB; /* A30S: fast scrolling */\n"
         "#else\n"
         "\t\t\t    unsigned int flags = VM_WIMG_IO;\n"
         "#endif\n",
         "A30S: fast scrolling")
    print("A30S fixes applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
