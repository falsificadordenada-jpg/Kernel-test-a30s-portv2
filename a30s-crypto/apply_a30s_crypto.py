#!/usr/bin/env python3
"""A30S built-in crypto function table for xnu-4903.270.47.   Usage: apply_a30s_crypto.py <xnu-root>

Fixes the tcp_init() data abort (far=0x78): g_crypto_funcs is NULL without the corecrypto kext.
  1. Copies libkern/crypto/a30s_crypto.c and registers it in libkern/conf/files.
  2. Calls a30s_crypto_register() right after a30s_prng_register() in kernel_bootstrap_thread()
     (needs 25-a30s-prng.sh applied first).
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

    edit(root, "libkern/conf/files",
         "libkern/crypto/corecrypto_aes.c\t\t\toptional crypto\n",
         "libkern/crypto/corecrypto_aes.c\t\t\toptional crypto\nlibkern/crypto/a30s_crypto.c\t\t\toptional crypto\n",
         "a30s_crypto.c")

    edit(root, "osfmk/kern/startup.c",
         "\t\ta30s_prng_register();\n\t}\n#endif\n",
         "\t\ta30s_prng_register();\n\t}\n"
         "\t{\n\t\textern void a30s_crypto_register(void);\n\t\ta30s_crypto_register();\n\t}\n#endif\n",
         "a30s_crypto_register")
    print("A30S crypto table applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
