#!/usr/bin/env python3
"""A30S eMMC root device for xnu-4903.270.47.   Usage: apply_a30s_emmc.py <xnu-root>

What it does:
  1. Copies bsd/dev/a30s_emmc.c (DW-MMC0 polled-PIO block driver, GPT partition by name).
  2. Registers it in bsd/conf/files.
  3. Teaches IOFindBSDRoot about rd=emmcroot0 (no IOKit storage stack needed) and skips the
     30 s wait for IOBSD matching whenever rd=/rootdev= is given.
  4. Enables CONFIG_MEMDEV_INSECURE so boot-args pmd0=<phys>.<size> create a RAM disk (rd=md0).
Boot-args: rd=emmcroot0 emmcroot=<GPT partition name, default userdata> emmcw=1 emmc_force=1
Independent of the HFS part: it only needs xnu with the 00 port patch applied.
Idempotent: every edit is an exact-string replacement that must match once.
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def edit(root, rel, old, new, marker):
    path = os.path.join(root, rel)
    text = open(path).read()
    if marker in text:
        print("  already applied: %s (%s)" % (rel, marker.strip()[:40]))
        return
    if text.count(old) != 1:
        sys.exit("ERROR: %s: expected exactly 1 match, found %d for:\n%s"
                 % (rel, text.count(old), old))
    open(path, "w").write(text.replace(old, new))
    print("  patched: %s" % rel)


def main(root):
    # 1. overlay files
    for dirpath, _, files in os.walk(os.path.join(HERE, "overlay")):
        for name in files:
            src = os.path.join(dirpath, name)
            rel = os.path.relpath(src, os.path.join(HERE, "overlay"))
            dst = os.path.join(root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            print("  copied: %s" % rel)

    edit(root, "bsd/conf/files",
         "bsd/dev/memdev.c\t\t\tstandard\n",
         "bsd/dev/memdev.c\t\t\tstandard\nbsd/dev/a30s_emmc.c\t\t\tstandard\n",
         "a30s_emmc.c")

    # 2. root device selection (IOKitBSDInit.cpp is C++: the board macro is not defined there)
    edit(root, "iokit/bsddev/IOKitBSDInit.cpp",
         "static int didRam = 0;\n",
         "extern \"C\" int a30s_emmc_rootdev(dev_t * dev);\n\nstatic int didRam = 0;\n",
         "a30s_emmc_rootdev(dev_t * dev)")
    edit(root, "iokit/bsddev/IOKitBSDInit.cpp",
         "\t// stall here for anyone matching on the IOBSD resource to finish (filesystems)\n"
         "\tmatching = IOService::serviceMatching(gIOResourcesKey);\n"
         "\tassert(matching);\n"
         "\tmatching->setObject(gIOResourceMatchedKey, gIOBSDKey);\n\n"
         "\tif ((service = IOService::waitForMatchingService(matching, 30ULL * kSecondScale))) {\n"
         "\t\tservice->release();\n"
         "\t} else {\n"
         "\t\tIOLog(\"!BSD\\n\");\n"
         "\t}\n"
         "\tmatching->release();\n"
         "\tmatching = NULL;\n\n"
         "\tif (mountAttempts++) {\n"
         "\t\tIOLog(\"mount(%d) failed\\n\", mountAttempts);\n"
         "\t\tIOSleep( 5 * 1000 );\n"
         "\t}\n",
         "\t/* A30S: an explicit rd=/rootdev= means a built-in driver supplies the root: no kext\n"
         "\t * publishes IOBSD here, so do not wait 30 s for it. */\n"
         "\tchar a30s_rd[kMaxBootVar];\n"
         "\tbool a30s_have_rd = PE_parse_boot_argn(\"rd\", a30s_rd, sizeof(a30s_rd))\n"
         "\t    || PE_parse_boot_argn(\"rootdev\", a30s_rd, sizeof(a30s_rd));\n\n"
         "\tif (!a30s_have_rd) {\n"
         "\t\t// stall here for anyone matching on the IOBSD resource to finish (filesystems)\n"
         "\t\tmatching = IOService::serviceMatching(gIOResourcesKey);\n"
         "\t\tassert(matching);\n"
         "\t\tmatching->setObject(gIOResourceMatchedKey, gIOBSDKey);\n\n"
         "\t\tif ((service = IOService::waitForMatchingService(matching, 30ULL * kSecondScale))) {\n"
         "\t\t\tservice->release();\n"
         "\t\t} else {\n"
         "\t\t\tIOLog(\"!BSD\\n\");\n"
         "\t\t}\n"
         "\t\tmatching->release();\n"
         "\t\tmatching = NULL;\n"
         "\t}\n\n"
         "\tif (mountAttempts++) {\n"
         "\t\tIOLog(\"mount(%d) failed\\n\", mountAttempts);\n"
         "\t\tIOSleep( 5 * 1000 );\n"
         "\t}\n\n"
         "\t/* A30S: rd=emmcroot0 selects the DW-MMC0 eMMC block device directly (no IOMedia). */\n"
         "\tif (a30s_have_rd && strcmp(a30s_rd, \"emmcroot0\") == 0) {\n"
         "\t\tint a30s_err = ENXIO;\n\n"
         "\t\tfor (int a30s_try = 0; a30s_try < 3 && a30s_err; a30s_try++) {\n"
         "\t\t\tif (a30s_try) {\n"
         "\t\t\t\tIOSleep( 2 * 1000 );\n"
         "\t\t\t}\n"
         "\t\t\ta30s_err = a30s_emmc_rootdev(root);\n"
         "\t\t}\n"
         "\t\tif (a30s_err) {\n"
         "\t\t\tpanic(\"IOFindBSDRoot: A30S eMMC root device unavailable (%d)\", a30s_err);\n"
         "\t\t}\n"
         "\t\tstrlcpy(rootName, a30s_rd, rootNameSize);\n"
         "\t\tIOLog(\"BSD root: %s, major %d, minor %d\\n\", rootName, major(*root), minor(*root));\n"
         "\t\t*oflags = 0;\n"
         "\t\treturn kIOReturnSuccess;\n"
         "\t}\n",
         "a30s_have_rd")
    # the old standalone mountAttempts block is now part of the replacement above
    print("  (IOFindBSDRoot edit done)")

    # 3. RAM disk from boot-args (pmd0=<phys>.<size>, then rd=md0)
    path = os.path.join(root, "bsd/dev/memdev.c")
    text = open(path).read()
    if "A30S: enable boot-arg RAM disks" in text:
        print("  already applied: bsd/dev/memdev.c")
    else:
        idx = text.index("#include")
        text = (text[:idx] +
                "/* A30S: enable boot-arg RAM disks (pmd0=<phys>.<size>), used with rd=md0. */\n"
                "#if defined(ARM64_BOARD_CONFIG_A30S) && !defined(CONFIG_MEMDEV_INSECURE)\n"
                "#define CONFIG_MEMDEV_INSECURE 1\n"
                "#endif\n\n" + text[idx:])
        open(path, "w").write(text)
        print("  patched: bsd/dev/memdev.c")
    print("A30S eMMC root support applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
