#!/usr/bin/env python3
"""A30S fixes recovered from the M4v50 binary patches (second batch).
Usage: apply_a30s_boot.py <xnu-root>

  1. IOPanicPlatform::start(): with no platform kext (AppleARMPE) IOKit falls back to
     IOPanicPlatform, whose start() panics ("Unable to find driver for this platform").
     M4v50 patched it to call IOPlatformExpert::start() and return true; same here.
     (IOPlatformExpert.cpp is C++: the board macro is not defined there, so this is unconditional.)
  2. XNUPOST: kernel_list_tests()/kernel_do_post() are skipped (M4v50 turned both into `ret`).
     The in-kernel self tests need services this port does not have yet.
Every edit is an exact-string replacement that must match once (idempotent).
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
    edit(root, "iokit/Kernel/IOPlatformExpert.cpp",
         "\tpanic(\"Unable to find driver for this platform: \\\"%s\\\".\\n\",\n"
         "\t    platform_name);\n\n\treturn false;\n}\n",
         "\t/* A30S: there is no platform kext; keep going with the generic platform expert. */\n"
         "\t(void)platform_name;\n"
         "\t(void)IOPlatformExpert::start(provider);\n"
         "\treturn true;\n}\n",
         "A30S: there is no platform kext")

    edit(root, "osfmk/kern/startup.c",
         "#ifdef CONFIG_XNUPOST\n",
         "#if defined(CONFIG_XNUPOST) && !defined(ARM64_BOARD_CONFIG_A30S) /* A30S: skip the self tests */\n",
         "A30S: skip the self tests")
    print("A30S boot fixes applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
