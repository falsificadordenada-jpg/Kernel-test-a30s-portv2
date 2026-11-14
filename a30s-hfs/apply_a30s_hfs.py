#!/usr/bin/env python3
"""Built-in Apple OSS HFS+ for the A30S kernel (xnu-4903.270.47).
Usage: apply_a30s_hfs.py <xnu-root>

  1. Stages Apple's open-source HFS (hfs-522.100.5, kernel part only) from
     github.com/apple-oss-distributions at build time into bsd/a30s_hfs/.
     No Apple source text is stored in this repository.
  2. Copies the overlay: HFS compat header + registration glue.
  3. Wires it into the xnu config/build files; registers the "hfs" filesystem after vfsinit().
Independent of the eMMC part. Set A30S_HFS_SRC=<dir of an hfs checkout> for offline builds.
Idempotent: every edit is an exact-string replacement that must match once.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HFS_REPO = "https://github.com/apple-oss-distributions/hfs"
HFS_TAG = "hfs-522.100.5"
HFS_SHA = "129eb27875dc8197b55314672b97e72dffdc9c49"
HFS_DIR = "bsd/a30s_hfs"


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


def stage_hfs(root):
    target = os.path.join(root, HFS_DIR)
    if os.path.isdir(os.path.join(target, "core")):
        print("  HFS already staged")
        return
    local = os.environ.get("A30S_HFS_SRC")
    tmp = None
    if local:
        src = local
    else:
        tmp = tempfile.mkdtemp()
        src = tmp
        subprocess.check_call(["git", "init", "-q", tmp])
        subprocess.check_call(["git", "-C", tmp, "fetch", "-q", "--depth", "1", HFS_REPO, HFS_SHA])
        subprocess.check_call(["git", "-C", tmp, "checkout", "-q", "FETCH_HEAD"])
        head = subprocess.check_output(["git", "-C", tmp, "rev-parse", "HEAD"]).decode().strip()
        if head != HFS_SHA:
            sys.exit("ERROR: fetched %s, expected %s" % (head, HFS_SHA))
    try:
        os.makedirs(target, exist_ok=True)
        for sub in ("core", "hfs_encodings"):
            shutil.copytree(os.path.join(src, sub), os.path.join(target, sub))
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)

    # hfs_iokit.h pulls in AppleKeyStore headers; they are only needed for the
    # key-wrapping prototypes, which exist only with content protection.
    path = os.path.join(target, "core", "hfs_iokit.h")
    text = open(path).read()
    include = "#include <AppleKeyStore/AppleKeyStoreFSServices.h>\n"
    first, end = "int hfs_unwrap_key(", "__END_DECLS"
    if text.count(include) != 1 or text.count(first) != 1 or text.count(end) != 1:
        sys.exit("ERROR: unexpected layout of %s" % path)
    text = text.replace(include, "#if CONFIG_PROTECT\n" + include + "#endif\n")
    i, j = text.index(first), text.index(end)
    text = text[:i] + "#if CONFIG_PROTECT\n" + text[i:j].rstrip("\n") + "\n#endif\n\n" + text[j:]
    open(path, "w").write(text)
    print("  staged %s (%s)" % (HFS_TAG, HFS_SHA))


def hfs_sources(root):
    """C sources of the staged HFS (relative to the xnu root), plus the glue."""
    out = []
    for sub in ("core", "hfs_encodings"):
        d = os.path.join(root, HFS_DIR, sub)
        for name in sorted(os.listdir(d)):
            if name.endswith(".c"):
                out.append("%s/%s/%s" % (HFS_DIR, sub, name))
    out.append("%s/a30s_hfs_glue.c" % HFS_DIR)
    return out


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

    # 2. stage Apple's HFS
    stage_hfs(root)
    srcs = hfs_sources(root)
    objs = [os.path.splitext(os.path.basename(s))[0] + ".o" for s in srcs]
    if len(set(objs)) != len(objs):
        sys.exit("ERROR: duplicate object names in the HFS file list")

    # 3. config options
    edit(root, "config/MASTER",
         "options\t\tMOCKFS\t\t# Boot from an executable\t# <mockfs>\n",
         "options\t\tMOCKFS\t\t# Boot from an executable\t# <mockfs>\n"
         "options\t\tA30S_HFS\t# Built-in Apple OSS HFS (A30S)\t# <a30s_hfs>\n",
         "A30S_HFS")
    edit(root, "config/MASTER.arm64.a30s",
         "FILESYS_BASE =   [ devfs fifo fs_compression ",
         "FILESYS_BASE =   [ devfs fifo a30s_hfs fs_compression ",
         "fifo a30s_hfs")

    # 4. file lists
    lines = "".join("%s\t\toptional a30s_hfs\n" % s for s in srcs)
    edit(root, "bsd/conf/files",
         "bsd/dev/memdev.c\t\t\tstandard\n",
         "bsd/dev/memdev.c\t\t\tstandard\n"
         "\n# Apple OSS %s, staged by apply_a30s_hfs.py\n%s" % (HFS_TAG, lines),
         "Apple OSS " + HFS_TAG)

    # 5. per-object flags: compile HFS the way the kext is built
    flags = (
        "\n# Apple OSS HFS built in (%s): compiled like the kext, see a30s_hfs_compat.h.\n"
        "# The compat header must come after the kernel's -D options, hence _CFLAGS_ADD.\n"
        "A30S_HFS_ROOT = $(SRCROOT)/%s\n"
        "A30S_HFS_OBJS = %s\n"
        "$(foreach o,$(A30S_HFS_OBJS),$(eval $(o)_CFLAGS_ADD += -include $(A30S_HFS_ROOT)/a30s_hfs_compat.h"
        " -I$(A30S_HFS_ROOT) -I$(A30S_HFS_ROOT)/core -Wno-unused-parameter -Wno-missing-prototypes))\n"
        "# HFSEncodings is a separate kext on Apple with its own allocation counter.\n"
        "hfs_encodings.o_CFLAGS_ADD += -Dhfs_allocated=hfs_encodings_allocated\n"
        "hfs_encodinghint.o_CFLAGS_ADD += -Dhfs_allocated=hfs_encodings_allocated\n"
        % (HFS_TAG, HFS_DIR, " ".join(objs)))
    edit(root, "bsd/conf/Makefile.template",
         "pthread_support.o_CFLAGS_ADD\t\t+= -Wno-unused-parameter -Wno-missing-prototypes\n",
         "pthread_support.o_CFLAGS_ADD\t\t+= -Wno-unused-parameter -Wno-missing-prototypes\n" + flags,
         "A30S_HFS_ROOT")

    # 6. register HFS before the root filesystem is mounted
    edit(root, "bsd/kern/bsd_init.c",
         "\tbsd_init_kprintf(\"calling vfsinit\\n\");\n\tvfsinit();\n",
         "\tbsd_init_kprintf(\"calling vfsinit\\n\");\n\tvfsinit();\n\n"
         "#if A30S_HFS\n"
         "\t/* HFS is built in on the A30S: register it like its kext would, before mounting root. */\n"
         "\t{\n\t\textern void a30s_hfs_register(void);\n\t\ta30s_hfs_register();\n\t}\n"
         "#endif\n",
         "a30s_hfs_register")

    print("A30S HFS support applied to", root)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
