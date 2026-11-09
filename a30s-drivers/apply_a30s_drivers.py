#!/usr/bin/env python3
"""Applies the A30S buttons + touch driver to the xnu tree. Usage: apply_a30s_drivers.py <xnu-root>"""
import os, shutil, sys
root = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
for rel in ("osfmk/arm64/a30s_input.c", "osfmk/arm64/a30s_input.h"):
    dst = os.path.join(root, rel); shutil.copyfile(os.path.join(here, "overlay", rel), dst); print("copied:", rel)

def edit(rel, old, new, marker):
    p = os.path.join(root, rel); s = open(p).read()
    if marker in s: print("already applied:", rel); return
    assert s.count(old) == 1, "anchor not found (%d) in %s: %r" % (s.count(old), rel, old)
    open(p, "w").write(s.replace(old, new)); print("patched:", rel)

edit("osfmk/conf/files.arm64", "osfmk/arm64/a30s_gic.c",
     "osfmk/arm64/a30s_input.c\tstandard\nosfmk/arm64/a30s_gic.c", "a30s_input.c")
# start the driver right before bsd_init() in kernel_bootstrap_thread
edit("osfmk/kern/startup.c", "\tbsd_init();\n",
     "#if defined(ARM64_BOARD_CONFIG_A30S)\n\t{ extern void a30s_input_start(void); a30s_input_start(); }\n#endif\n\tbsd_init();\n",
     "a30s_input_start")
print("A30S input driver applied to", root)
