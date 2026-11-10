#!/usr/bin/env python3
"""A30S platform expert for xnu-4903.270.47.   Usage: apply_a30s_platform.py <xnu-root>

Fixes: panic "Unable to find driver for this platform: A30S,Exynos7904" (IOPanicPlatform).
  1. Copies iokit/Kernel/A30SPlatformExpert.cpp (IODTPlatformExpert subclass).
  2. Registers it in iokit/conf/files.
  3. Adds a built-in personality (IONameMatch A30S,Exynos7904, probe score 1000) to
     iokit/KernelConfigTables.cpp, so it wins over IOPanicPlatform (score 0).
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

    edit(root, "iokit/conf/files",
         "iokit/Kernel/IOPlatformExpert.cpp\t\t\toptional iokitcpp\n",
         "iokit/Kernel/IOPlatformExpert.cpp\t\t\toptional iokitcpp\n"
         "iokit/Kernel/A30SPlatformExpert.cpp\t\t\toptional iokitcpp\n",
         "A30SPlatformExpert.cpp")

    edit(root, "iokit/KernelConfigTables.cpp",
         "    \"   {\"\n"
         "    \"     'IOClass'         = IOPanicPlatform;\"\n",
         "    \"   {\"\n"
         "    \"     'IOClass'         = A30SPlatformExpert;\"\n"
         "    \"     'IOProviderClass' = IOPlatformExpertDevice;\"\n"
         "    \"     'IONameMatch'     = 'A30S,Exynos7904';\"\n"
         "    \"     'IOProbeScore'    = 1000:32;\"\n"
         "    \"   },\"\n"
         "    \"   {\"\n"
         "    \"     'IOClass'         = IOPanicPlatform;\"\n",
         "A30SPlatformExpert")
    print("A30S platform expert applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
