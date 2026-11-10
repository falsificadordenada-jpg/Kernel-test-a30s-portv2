#!/usr/bin/env python3
"""A30S built-in pthread stub table for xnu-4903.270.47.   Usage: apply_a30s_pthread.py <xnu-root>

Fixes: panic "pthread kernel extension not loaded (function table is NULL)" in pthread_init().
  1. Copies bsd/pthread/a30s_pthread_stub.c, registers it in bsd/conf/files.
  2. pthread_init(): if no kext registered a table, register the stub table first.
Idempotent: every edit is an exact-string replacement that must match once.
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def edit(root, rel, old, new, marker):
    path = os.path.join(root, rel)
    text = open(path).read()
    if marker in text:
        print("  already applied: %s" % rel)
        return
    if text.count(old) != 1:
        sys.exit("ERROR: %s: expected exactly 1 match, found %d for:\n%s" % (rel, text.count(old), old))
    open(path, "w").write(text.replace(old, new))
    print("  patched: %s" % rel)


def main(root):
    for dirpath, _, files in os.walk(os.path.join(HERE, "overlay")):
        for name in files:
            src = os.path.join(dirpath, name)
            rel = os.path.relpath(src, os.path.join(HERE, "overlay"))
            dst = os.path.join(root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            print("  copied: %s" % rel)

    edit(root, "bsd/conf/files",
         "bsd/pthread/pthread_shims.c\t\tstandard\n",
         "bsd/pthread/pthread_shims.c\t\tstandard\nbsd/pthread/a30s_pthread_stub.c\t\tstandard\n",
         "a30s_pthread_stub.c")

    edit(root, "bsd/pthread/pthread_shims.c",
         "void\npthread_init(void)\n{\n\tif (!pthread_functions) {\n",
         "extern void a30s_pthread_stub_register(void);\n\n"
         "void\npthread_init(void)\n{\n\tif (!pthread_functions) {\n"
         "\t\ta30s_pthread_stub_register(); /* A30S: no pthread.kext, use the built-in stub table */\n"
         "\t}\n\tif (!pthread_functions) {\n",
         "a30s_pthread_stub_register();")
    print("A30S pthread stub applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
