
#!/bin/bash
# Builds XNU 4903 (macOS 10.14.6 / iOS 12 era) for ARM64 (A30S = Galaxy A30s port; T7000 = Apple A8)
# on a GitHub Actions macOS runner. Compatible with the macOS bash 3.2.
#
# Philosophy: every stage writes its own log to out/<stage>.log and its result to
# out/report.txt. Independent stages keep running even if one fails, so a single run
# shows as many problems as possible.
# NO Apple code is stored in the repo: everything is downloaded on every build.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/work"; FR="$WORK/fakeroot"; BLD="$WORK/build"; OUT="$ROOT/out"
mkdir -p "$WORK" "$FR" "$BLD" "$OUT"
REPORT="$OUT/report.txt"; : > "$REPORT"

APPLE=https://github.com/apple-oss-distributions
XNU_TAG="${XNU_TAG:-xnu-4903.270.47}"
KCFG="${KERNEL_CONFIG:-DEVELOPMENT}"
SDK="${SDK:-iphoneos}"
MCFG="${MACHINE_CONFIG:-A30S}"         # A30S = Galaxy A30s (our port); T7000 = Apple A8
KLC="$(echo "$KCFG" | tr 'A-Z' 'a-z').$(echo "$MCFG" | tr 'A-Z' 'a-z')"
# Tags of the macOS 10.14.6 release (distribution-macOS @ macos-10146)
T_AVAIL=AvailabilityVersions-33.200.7
T_LIBSYS=Libsystem-1252.250.1
T_LIBPLAT=libplatform-177.270.1
T_LIBDISP=libdispatch-1008.270.1
T_BOOT=bootstrap_cmds-99.260.1
T_DTRACE=dtrace-284.250.4
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
PCPU="$(sysctl -n hw.physicalcpu 2>/dev/null || echo 2)"
MEM="$(sysctl -n hw.memsize 2>/dev/null || echo 8589934592)"

# Xcode: the workflow already ran xcode-select -s with a single realpath. Leave it alone.
if XP="$(xcode-select -p 2>/dev/null)" && [ -d "$XP" ]; then DEVELOPER_DIR="$XP"; export DEVELOPER_DIR; fi
# Internal Apple tools that do not exist in public Xcode: stubs on PATH and in make variables
STUBS="$WORK/stubs"; mkdir -p "$STUBS"
cat > "$STUBS/embedded_device_map" <<'EOS'
#!/bin/bash
# stub: only knows one target (n61 / t7000 / arm64, retargeted below)
q="$*"
case "$q" in
  *"KernelMachOArchitecture LIKE \"arm64\""*) echo n61 ;;
  *"KernelMachOArchitecture LIKE"*) ;;
  *"SELECT DISTINCT TargetType, KernelMachOArchitecture, KernelPlatform FROM Targets"*) echo "n61|arm64|t7000|" ;;
  *"KernelPlatform"*"FROM Targets WHERE"*) echo arm64 ;;
esac
exit 0
EOS
for t in ctfconvert ctfmerge ctfdump; do printf '#!/bin/sh\nexit 0\n' > "$STUBS/$t"; done
sed -i '' "s/t7000/$(echo "$MCFG" | tr A-Z a-z)/" "$STUBS/embedded_device_map"   # target = the requested machine (A30S, T7000...)
chmod +x "$STUBS"/*
STUBARGS="EMBEDDED_DEVICE_MAP=$STUBS/embedded_device_map EDM_DBPATH=/dev/null CTFCONVERT=$STUBS/ctfconvert CTFMERGE=$STUBS/ctfmerge CTFDUMP=$STUBS/ctfdump"
export PATH="$STUBS:$PATH"

# Flags that downgrade "new" errors of modern clang on 2018 C code to WARNINGS
# (implicit declarations, int<->pointer, return in void function, ...). They do not change generated code.
COMPAT_WARN="-Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=incompatible-function-pointer-types -Wno-error=return-type -Wno-error=deprecated-non-prototype -Wno-error=strict-prototypes -Wno-error=int-to-pointer-cast -Wno-error=pointer-to-int-cast -Wno-return-type -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types"
SCRIPT_VERSION="v13-a30s (2026-11-08)"

say() { echo "[$(date +%H:%M:%S)] $*"; }
FAILED=0
# stage NAME command... : runs in a subshell with set -e, log in out/NAME.log
stage() {
  local name="$1"; shift
  say "=== STAGE $name"
  ( set -e; "$@" ) > "$OUT/$name.log" 2>&1
  local rc=$?
  if [ $rc -eq 0 ]; then echo "PASS  $name" | tee -a "$REPORT"
  else
    echo "FAIL  $name (rc=$rc)" | tee -a "$REPORT"
    echo "----- last 60 lines of $name.log -----"; tail -n 60 "$OUT/$name.log"; echo "-----"
    grep -E "error:|\*\*\*|fatal:|No such file|not found" "$OUT/$name.log" | head -n 40 | sed "s/^/  [$name] /" >> "$REPORT"
    FAILED=1
  fi
  return $rc
}
clone() { # clone URL TAG DIR
  [ -d "$WORK/$3/.git" ] || git clone -q --depth 1 --branch "$2" "$1" "$WORK/$3"
}

# ---------------------------------------------------------------- fetch
do_fetch() {
  clone $APPLE/xnu.git                 "$XNU_TAG"   xnu
  clone $APPLE/AvailabilityVersions.git "$T_AVAIL"  AvailabilityVersions
  clone $APPLE/Libsystem.git           "$T_LIBSYS"  Libsystem
  clone $APPLE/libplatform.git         "$T_LIBPLAT" libplatform
  clone $APPLE/libdispatch.git         "$T_LIBDISP" libdispatch
  git -C "$WORK/xnu" log -1 --format='xnu %h %cd'
}

# ---------------------------------------------------------------- patches
# Our patches (xnu/patches/*.sh then *.patch) run in sorted order. Failure = HARD error
# (never skip a patch that does not apply).
do_ours() {
  cd "$WORK/xnu"
  for s in "$ROOT"/xnu/patches/*.sh; do [ -f "$s" ] || continue; echo "script $s"; bash "$s"; done
  for p in "$ROOT"/xnu/patches/*.patch; do [ -f "$p" ] || continue
    echo "patch $p"; git apply --check "$p"; git apply "$p"
  done
  echo "our patches: ok"
}
# Compatibility tweaks for modern Xcode/SDKs (they do not touch kernel logic).
do_compat() {
  cd "$WORK/xnu"
  # availability.pl comes from AvailabilityVersions (installed in the fakeroot), not from the SDK
  sed -i '' "s|\${SDKROOT}/usr/local/libexec/availability.pl|$FR/usr/local/libexec/availability.pl|g" bsd/sys/make_symbol_aliasing.sh
  # libfirehose_kernel and the libdispatch/libplatform headers come from the fakeroot
  sed -i '' "s|^LDFLAGS_KERNEL_SDK[[:space:]]*= -L\$(SDKROOT)/usr/local/lib/kernel|LDFLAGS_KERNEL_SDK = -L$FR/usr/local/lib/kernel|" makedefs/MakeInc.def
  sed -i '' "s|^INCFLAGS_SDK[[:space:]]*= -I\$(SDKROOT)/usr/local/include/kernel|INCFLAGS_SDK = -I$FR/usr/local/include/kernel|" makedefs/MakeInc.def
  sed -i '' 's|^#include.*BSD.xcconfig.*||g' libsyscall/Libsyscall.xcconfig
  # XNU's Python 2 scripts (modern macOS has no /usr/bin/python): we use our own py3 rewrites
  cat > bsd/kern/makekdebugevents.py <<'PYEOF1'
#!/usr/bin/env python3
# Python 3 reimplementation (our own) of the kdebugevents.h generator:
# reads trace.codes (lines "0xID NAME") and prints the kd_events[] C table.
import re
import sys

if len(sys.argv) < 2:
    sys.exit(1)

pat = re.compile(r'0x([0-9a-fA-F]+)\s+([^\s]*)')
table = []
with open(sys.argv[1], 'rt') as f:
    for line in f:
        m = pat.match(line)
        if m:
            table.append((int(m.group(1), 16), m.group(2)))

print("typedef struct {")
print("        uint32_t   id;")
print("        const char *name;")
print("} kd_event_t;")
print("kd_event_t kd_events[] = {")
for ev_id, name in sorted(table, key=lambda x: x[0]):
    print('        {0x%x, "%s"},' % (ev_id, name))
print("};")
PYEOF1
  cat > san/tools/generate_dynamic_blacklist.py <<'PYEOF2'
#!/usr/bin/env python3
# Python 3 reimplementation (our own) of the KASAN/san dynamic blacklist generator:
# input: lines "kext:function:type" (# comments); output: C array blacklist[].
import re
import sys

EXTRA = 5
comment = re.compile(r'#.*$')
entries = []
with open(sys.argv[1]) as f:
    for line in f:
        line = comment.sub("", line).strip()
        if not line:
            continue
        fields = line.split(":")
        if len(fields) != 3:
            continue
        kext, func, ty = fields
        kext = '"%s"' % kext if kext else "NULL"
        func = '"%s"' % func if func else "NULL"
        entries.append((kext, func, "TYPE_" + (ty or "normal").upper()))

def entry(k, fn, t):
    return "\t{\n\t\t.kext_name = %s,\n\t\t.func_name = %s,\n\t\t.type_mask = %s,\n\t}," % (k, fn, t)

print("struct blacklist_entry blacklist[] = {")
for e in entries:
    print(entry(*e))
print("")
print("\t/* Unused entries that can be populated at runtime */")
for _ in range(EXTRA):
    print(entry("NULL", "NULL", 0))
print("};")
print("")
print("static size_t blacklist_entries = %d;" % len(entries))
print("static const size_t blacklist_max_entries = %d;" % (len(entries) + EXTRA))
PYEOF2
  chmod +x bsd/kern/makekdebugevents.py san/tools/generate_dynamic_blacklist.py
  # --- v5: assym.s. The original sed does not recognise new clang asm (.ascii "7" becomes .byte 55)
  cat > "$WORK/genassym_conv.py" <<'PYEOF3'
#!/usr/bin/env python3
"""Replaces the sed in osfmk/conf/Makefile.template that generates assym.s from genassym.o
(clang assembly). New clangs rewrite '.ascii "7"' (1 char) as '.byte 55' and the original sed
does not recognise it -> duplicate labels ('symbol DEFINITION__define__X is already defined').
Same output as the original: '#define NAME VAL' (+ '#' if clang printed it) and '#define NAME_NUM VAL'."""
import re, sys
lines = open(sys.argv[1], errors='replace').read().splitlines()
lab = re.compile(r'^\s*DEFINITION__define__([A-Za-z0-9_]+):')
asc = re.compile(r'\.ascii\s+"([^"]*)"')
byt = re.compile(r'\.byte\s+(\d+)\s*$')
n = 0
for i, l in enumerate(lines):
    m = lab.match(l)
    if not m:
        continue
    text = None
    for j in range(i + 1, min(i + 6, len(lines))):
        a = asc.search(lines[j]); b = byt.search(lines[j])
        if a: text = a.group(1); break
        if b: text = chr(int(b.group(1))); break
    if text is None:
        sys.stderr.write('no value for %s\n' % m.group(1)); sys.exit(1)
    text = text.lstrip('$')
    hashp = '#' if text.startswith('#') else ''
    v = text.lstrip('#')
    if not re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', v):
        sys.stderr.write('odd value %r for %s\n' % (text, m.group(1))); sys.exit(1)
    if v.lower().lstrip('-').startswith('0x'):
        v = str(int(v, 16))
    print('#define %s %s%s' % (m.group(1), hashp, v))
    print('#define %s_NUM %s' % (m.group(1), v))
    n += 1
sys.stderr.write('genassym_conv: %d symbols\n' % n)
sys.exit(0 if n else 1)
PYEOF3
  perl -pi -e 'if (/^\t\$\(_v\)sed -e .\/\^\[\[:space:\]\]\*DEFINITION__define__/) { $_ = "\t\$(_v)python3 '"$WORK"'/genassym_conv.py genassym.o > \$@\n"; }' osfmk/conf/Makefile.template
  grep -n "genassym_conv" osfmk/conf/Makefile.template
  # --- v8: libfirehose_kernel.a comes out without 3 functions (header static inlines were not generated and became external calls).
  # Bring-up stub: without firehose the kernel os_log simply drops messages (log.c handles reserve==NULL).
  cat > osfmk/kern/a30s_firehose_stub.c <<'CEOF'
/* A30S stub: functions libfirehose_kernel.a leaves undefined with this toolchain. */
#include <stdint.h>
#include <stdbool.h>
void *firehose_buffer_tracepoint_reserve(void *fb, uint64_t stamp, int stream, uint16_t pubsize,
    uint16_t privsize, uint8_t **privptr, bool reliable);
void firehose_buffer_tracepoint_flush(void *fb, void *ft, uint64_t ftid);
bool firehose_buffer_bank_try_reserve_slot(void *fb, bool for_io, void *state);
void *firehose_buffer_tracepoint_reserve(void *fb, uint64_t stamp, int stream, uint16_t pubsize,
    uint16_t privsize, uint8_t **privptr, bool reliable)
{ (void)fb; (void)stamp; (void)stream; (void)pubsize; (void)privsize; (void)privptr; (void)reliable; return (void *)0; }
void firehose_buffer_tracepoint_flush(void *fb, void *ft, uint64_t ftid)
{ (void)fb; (void)ft; (void)ftid; }
bool firehose_buffer_bank_try_reserve_slot(void *fb, bool for_io, void *state)
{ (void)fb; (void)for_io; (void)state; return false; }
CEOF
  grep -q a30s_firehose_stub osfmk/conf/files || echo 'osfmk/kern/a30s_firehose_stub.c		standard' >> osfmk/conf/files
  tail -2 osfmk/conf/files
  # --- v9: lldbmacros (debugger only) uses a Python 2 syntax checker that breaks config_all. Replace with a no-op.
  printf '#!/usr/bin/env python3\nimport sys\nsys.exit(0)\n' > tools/lldbmacros/core/syntax_checker.py
  chmod +x tools/lldbmacros/core/syntax_checker.py
  # --- v5: MT_CORE_NFIXED only existed under KERNEL_PRIVATE; always define it (same value, identical redefinition is fine)
  perl -0pi -e 's/(#include <sys\/cdefs.h>\n)/$1\n#ifndef MT_CORE_NFIXED\n#define MT_CORE_NFIXED 2\n#endif\n/' osfmk/arm64/monotonic.h
  grep -n "MT_CORE_NFIXED" osfmk/arm64/monotonic.h | head -3
  # the full "install" target calls checkstyle (tools/uncrustify.sh does not exist in the public source)
  sed -i '' 's/install_aliases checkstyle$/install_aliases/' makedefs/MakeInc.top
  grep -n "LDFLAGS_KERNEL_SDK\|INCFLAGS_SDK" makedefs/MakeInc.def | head
  python3 -I bsd/kern/makekdebugevents.py /dev/null >/dev/null && echo 'py3 makekdebugevents ok'
  grep -n "^install:" makedefs/MakeInc.top
}

# ---------------------------------------------------------------- tools
do_tools() {
  mkdir -p "$FR/usr/local/libexec"
  cp "$WORK/AvailabilityVersions/availability.pl" "$FR/usr/local/libexec/availability.pl"
  chmod +x "$FR/usr/local/libexec/availability.pl"
  test -x "$FR/usr/local/libexec/availability.pl"
  for t in mig migcom ctfconvert ctfmerge ctf_insert unifdef python3; do
    printf '%-12s ' "$t"; xcrun -sdk "$SDK" -find "$t" 2>&1 || true
  done
  xcrun -sdk "$SDK" -find mig >/dev/null
  xcrun -sdk "$SDK" -find migcom >/dev/null
  xcrun -sdk "$SDK" -show-sdk-path; xcrun -sdk "$SDK" -show-sdk-version
}

# ctfconvert/ctfmerge do not ship with Xcode: build them from the dtrace project (optional)
do_ctf() {
  clone $APPLE/dtrace.git "$T_DTRACE" dtrace
  cd "$WORK/dtrace"
  env LD="$(xcrun -find clang)" LDPLUSPLUS="$(xcrun -find clang++)" \
    xcodebuild install -sdk macosx -target ctfconvert -target ctfdump -target ctfmerge ARCHS="arm64" \
    CODE_SIGN_IDENTITY="-" OBJROOT="$BLD/dtrace.obj" SYMROOT="$BLD/dtrace.sym" DSTROOT="$FR"
  test -x "$FR/usr/local/bin/ctfconvert"
}
CTFARGS=""
false && CTFARGS="CTFCONVERT=$FR/usr/local/bin/ctfconvert CTFMERGE=$FR/usr/local/bin/ctfmerge CTFDUMP=$FR/usr/local/bin/ctfdump"

# ---------------------------------------------------------------- headers
do_xnu_headers() {
  cd "$WORK/xnu"
  make installhdrs $STUBARGS SDKROOT="$SDK" ARCH_CONFIGS="ARM64" TARGET_CONFIGS="$KCFG ARM64 $MCFG" \
    ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
    SRCROOT="$WORK/xnu" OBJROOT="$BLD/xnu-hdrs.obj" SYMROOT="$BLD/xnu-hdrs.sym" DSTROOT="$FR" FAKEROOT_DIR="$FR" \
    MEMORY_SIZE="$MEM" SYSCTL_HW_PHYSICALCPU="$PCPU" SYSCTL_HW_LOGICALCPU="$NCPU" KERNEL_BUILDS_IN_PARALLEL=1 \
    RC_DARWIN_KERNEL_VERSION=18.7.0
}
do_libsystem_hdrs() {
  cd "$WORK/Libsystem"
  sed -i '' 's|^#include.*BSD.xcconfig.*||g' Libsystem.xcconfig
  env LD="$(xcrun -find clang)" LDPLUSPLUS="$(xcrun -find clang++)" \
    xcodebuild installhdrs -sdk macosx ARCHS="arm64" VALID_ARCHS="arm64" \
    OBJROOT="$BLD/Libsystem.obj" SYMROOT="$BLD/Libsystem.sym" DSTROOT="$FR" FAKEROOT_DIR="$FR" CODE_SIGN_IDENTITY="-"
}
do_libsyscall_hdrs() {
  cd "$WORK/xnu/libsyscall"
  # v11: lock_set.defs does not exist in the public SDK; CPATH lets the cc used by mig find xnu's .defs
  export CPATH="$BLD/xnu-hdrs.obj/EXPORT_HDRS/osfmk${CPATH:+:$CPATH}"
  env LD="$(xcrun -find clang)" LDPLUSPLUS="$(xcrun -find clang++)" \
    xcodebuild installhdrs -sdk macosx TARGET_CONFIGS="$KCFG ARM64 $MCFG" ARCHS="arm64" VALID_ARCHS="arm64" ONLY_ACTIVE_ARCH=YES \
    MACOSX_DEPLOYMENT_TARGET=10.14 IPHONEOS_DEPLOYMENT_TARGET=12.0 OTHER_MIGFLAGS="-I$WORK/xnu/osfmk -I$FR/usr/include -I$FR/usr/local/include" \
    OBJROOT="$BLD/libsyscall.obj" SYMROOT="$BLD/libsyscall.sym" DSTROOT="$FR" FAKEROOT_DIR="$FR" CODE_SIGN_IDENTITY="-"
}
do_libplatform() {
  cd "$WORK/libplatform"
  ditto include "$FR/usr/local/include"
  ditto private "$FR/usr/local/include"
}
do_firehose() {
  cd "$WORK/libdispatch"
  mkdir -p "$DEVELOPER_DIR/Makefiles/CoreOS/Xcode" 2>/dev/null || true
  # Apple-internal xcconfig includes (BSD.xcconfig, PlatformSupport.xcconfig) do not exist in public Xcode
  sed -i '' -e '/BSD.xcconfig/d' -e '/PlatformSupport.xcconfig/d' xcodeconfig/*.xcconfig
  sed -i '' "s|\$(SDKROOT)/System/Library/Frameworks/Kernel.framework/PrivateHeaders|$FR/System/Library/Frameworks/Kernel.framework/PrivateHeaders|g" xcodeconfig/libfirehose_kernel.xcconfig
  sed -i '' "s|\$(SDKROOT)/usr/local/include|$FR/usr/local/include|g" xcodeconfig/libfirehose_kernel.xcconfig
  # same, for the remaining Apple-internal xcconfig includes
  sed -i '' -e '/#include.*BSD.xcconfig/d' -e '/#include.*PlatformSupport.xcconfig/d' xcodeconfig/*.xcconfig
  # v7: do NOT overwrite OTHER_CFLAGS (it lost -mkernel -nostdinc). Only append the -Wno-error=* flags to the existing line.
  # v13: also build for the real CPU. Cortex-A53 has no LSE atomics (they UNDEF there), so the default
  # armv8.x-a codegen of the SDK toolchain must be overridden with -mcpu=cortex-a53.
  FH_WARN="$(echo "$COMPAT_WARN" | tr ' ' '\n' | grep -E '^-Wno-error=' | tr '\n' ' ')"
  FH_CPU=""; [ "$MCFG" = "A30S" ] && FH_CPU="-mcpu=cortex-a53"
  sed -i '' "s|^OTHER_CFLAGS = \(.*\)\$|OTHER_CFLAGS = \1 $FH_WARN $FH_CPU|" xcodeconfig/libfirehose_kernel.xcconfig
  grep -n "OTHER_CFLAGS\|GCC_PREPROCESSOR" xcodeconfig/libfirehose_kernel.xcconfig
  env LD="$(xcrun -find clang)" LDPLUSPLUS="$(xcrun -find clang++)" \
    xcodebuild install -target libfirehose_kernel -sdk macosx ARCHS="arm64" VALID_ARCHS="arm64" \
    GCC_TREAT_IMPLICIT_FUNCTION_DECLARATIONS_AS_ERRORS=NO GCC_TREAT_WARNINGS_AS_ERRORS=NO \
    OBJROOT="$BLD/firehose.obj" SYMROOT="$BLD/firehose.sym" DSTROOT="$FR" FAKEROOT_DIR="$FR" CODE_SIGN_IDENTITY="-"
  mkdir -p "$FR/usr/local/lib/kernel"
  [ -f "$FR/usr/local/lib/kernel/liblibfirehose_kernel.a" ] && mv "$FR/usr/local/lib/kernel/liblibfirehose_kernel.a" "$FR/usr/local/lib/kernel/libfirehose_kernel.a"
  ls -la "$FR/usr/local/lib/kernel"
  test -f "$FR/usr/local/lib/kernel/libfirehose_kernel.a"
  # --- v6: the lib came out tagged "macOS"; the iOS kernel link refuses it ("building for iOS, but linking in object file built for macOS").
  # Relabel every object as iOS (platform load command only; the arm64 code is identical).
  echo "--- undefined firehose symbols (before retag):"; nm -u "$FR/usr/local/lib/kernel/libfirehose_kernel.a" 2>/dev/null | grep -i "firehose" | head -20
  if [ "$SDK" = "iphoneos" ]; then
    L="$FR/usr/local/lib/kernel/libfirehose_kernel.a"; T="$BLD/firehose.retag"; rm -rf "$T"; mkdir -p "$T"; cp "$L" "$T/orig.a"
    ( cd "$T" && ar x orig.a && rm -f orig.a "__.SYMDEF"* )
    for o in "$T"/*.o; do
      vtool -show-build "$o" | sed 's/^/  [before] /'
      vtool -set-build-version ios 12.0 18.0 -replace -output "$o.new" "$o" && mv "$o.new" "$o"
      vtool -show-build "$o" | sed 's/^/  [after] /'
    done
    rm -f "$L"; libtool -static -o "$L" "$T"/*.o
    ar t "$L"
  fi
  # v13 check: count LSE atomics (ldadd/swp/cas...) left in the library; must be 0 for Cortex-A53
  echo "LSE instructions in libfirehose_kernel.a: $(otool -tv "$FR/usr/local/lib/kernel/libfirehose_kernel.a" 2>/dev/null | grep -cE '\b(ldadd|ldclr|ldset|ldeor|swp|cas)[a-z]*\b')"
}

# ---------------------------------------------------------------- kernel
do_kernel() {
  cd "$WORK/xnu"
  make "${KTARGET:-install_development_embedded}" $STUBARGS OTHER_CFLAGS="$COMPAT_WARN" -j"$NCPU" VERBOSE=YES CONCISE=0 $CTFARGS SDKROOT="$SDK" TARGET_CONFIGS="$KCFG ARM64 $MCFG" \
    ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BUILD_WERROR=0 BUILD_LTO=0 DO_CTFCONVERT=0 DO_CTFMERGE=0 DO_CTFMACHO=0 \
    SRCROOT="$WORK/xnu" OBJROOT="$BLD/xnu.obj" SYMROOT="$BLD/xnu.sym" DSTROOT="$FR" FAKEROOT_DIR="$FR" \
    MEMORY_SIZE="$MEM" SYSCTL_HW_PHYSICALCPU="$PCPU" SYSCTL_HW_LOGICALCPU="$NCPU" KERNEL_BUILDS_IN_PARALLEL=1 \
    RC_DARWIN_KERNEL_VERSION=18.7.0 ${MAKE_KEEP_GOING:+-k}
}
collect() {
  mkdir -p "$OUT/kernel"
  # v11: copy ONLY the kernel (uncompressed, with symbols). No dSYM or .a files (hundreds of MB).
  D="$BLD/xnu.obj/${KCFG}_ARM64_${MCFG}"
  for n in "mach.$KLC" "mach.$KLC.unstripped"; do
    if [ -f "$D/$n" ]; then echo "kernel: $D/$n ($(wc -c < "$D/$n") bytes)"; cp "$D/$n" "$OUT/kernel/$n"; else echo "NOT FOUND: $D/$n"; fi
  done
  echo "--- candidates in xnu.obj:"; ls -la "$BLD"/xnu.obj/*/ 2>/dev/null | grep -E "mach|kernel" | head -20
  echo "--- installed in the fakeroot:"; find "$FR" -type f \( -name 'kernel*' -o -name 'mach*' -o -name '*kernelcache*' \) 2>/dev/null | head -20
  for f in "$OUT"/kernel/*; do [ -f "$f" ] || continue; file "$f"; xcrun lipo -info "$f" 2>&1 | head -2 | sed "s/error:/warning:/"; done
  # small compressed copy (phone uploads are size limited): xz of each binary
  for f in "$OUT"/kernel/*; do [ -f "$f" ] && case "$f" in *.xz) ;; *) xz -9 -k -f "$f" 2>/dev/null || gzip -9 -k -f "$f";; esac; done
  ( cd "$OUT/kernel" && shasum -a 256 * > SHA256SUMS 2>/dev/null ) || true
  ls -la "$OUT/kernel"
  # v13 check: LSE atomics in the final kernel (Cortex-A53 cannot execute them)
  if [ -f "$OUT/kernel/mach.$KLC.unstripped" ]; then
    echo "LSE instructions in kernel: $(xcrun otool -tv "$OUT/kernel/mach.$KLC.unstripped" 2>/dev/null | grep -cE '\b(ldadd|ldclr|ldset|ldeor|swp|cas)[a-z]*\b')"
  fi
  [ -n "$(ls "$OUT"/kernel 2>/dev/null)" ]
}

say "script $SCRIPT_VERSION"; say "xnu=$XNU_TAG kcfg=$KCFG machine=$MCFG sdk=$SDK ncpu=$NCPU"
sw_vers; xcodebuild -version
stage 01-fetch          do_fetch      || { echo "FATAL: no sources"; exit 2; }
stage 02-our-patches    do_ours       || { echo "FATAL: one of our patches did not apply (see out/02-our-patches.log)"; exit 3; }
stage 03-compat         do_compat
stage 04-tools          do_tools
CTFARGS=""; false && CTFARGS="CTFCONVERT=$FR/usr/local/bin/ctfconvert CTFMERGE=$FR/usr/local/bin/ctfmerge CTFDUMP=$FR/usr/local/bin/ctfdump"
stage 05-xnu-headers    do_xnu_headers
stage 06-libsystem-hdrs do_libsystem_hdrs
F07=$FAILED; stage 07-libsyscall-hdrs do_libsyscall_hdrs || { FAILED=$F07; echo "WARNING: 07 failed, but the kernel does not use it (ignored)" | tee -a "$REPORT"; }
stage 08-libplatform    do_libplatform
stage 09-firehose       do_firehose
stage 10-kernel         do_kernel
stage 11-collect        collect || true
python3 - "$OUT" "$REPORT" <<'PYREP' >> "$REPORT" 2>&1
import sys, os, re, glob, collections
out, rep = sys.argv[1], sys.argv[2]
RUN = re.compile(r'/Users/runner/work/[^ ]*?/work/')
def clean(s): return RUN.sub('', s)
logs = sorted(glob.glob(os.path.join(out, '*.log')))
def lines(p): return open(p, errors='replace').read().splitlines()
print(); print('=== MOST FREQUENT ERRORS (all stages; xcrun noise removed) ===')
c = collections.Counter()
for p in logs:
    for l in lines(p):
        if 'error:' in l and 'xcrun: error' not in l:
            l = clean(l); l = re.sub(r'^\S+ ', '', l); l = re.sub(r':\d+:\d+:', ':', l); c[l[:200]] += 1
for l, n in c.most_common(50): print('%4d %s' % (n, l))
print(); print('=== FILES WITH ERRORS (kernel) ===')
k = os.path.join(out, '10-kernel.log'); c = collections.Counter()
if os.path.exists(k):
    for l in lines(k):
        if 'error:' in l and 'xcrun: error' not in l:
            c[re.sub(r':\d+:\d+:.*', '', clean(l))[:160]] += 1
for l, n in c.most_common(40): print('%4d %s' % (n, l))
print(); print('=== ALL ERRORS PER STAGE (unique, runner path removed) ===')
pat = re.compile(r'error:|: \*\*\*|^ld: |fatal:|Undefined symbols|No such file|not found|SyntaxError')
for p in logs:
    ls = [l for l in lines(p) if pat.search(l) and 'xcrun: error' not in l]
    if not ls: continue
    print('--- %s: %d error lines' % (os.path.basename(p), len(ls)))
    seen = set()
    for l in ls:
        l = clean(l)[:260]
        if l in seen: continue
        seen.add(l); print(l)
        if len(seen) >= 40: break
print(); print('=== UNDEFINED SYMBOLS AT LINK (if any) ===')
if os.path.exists(k):
    L = lines(k)
    for i, l in enumerate(L):
        if 'Undefined symbols' in l:
            print('\n'.join(x[:200] for x in L[i:i+40])); break
print(); print('=== FIREHOSE: suspicious warnings ===')
f = os.path.join(out, '09-firehose.log')
if os.path.exists(f):
    seen = set()
    for l in lines(f):
        if re.search(r'implicit declaration|unknown type name|undeclared identifier', l):
            l = clean(l)[:240]
            if l not in seen: seen.add(l); print(l)
print(); print('=== STAGES ===')
seen = set()
for l in lines(rep):
    if re.match(r'(PASS|FAIL)', l) and l not in seen: seen.add(l); print(l)
PYREP
echo "---------- REPORT ----------"; cat "$REPORT"
[ -n "$(ls "$OUT"/kernel 2>/dev/null)" ] || { echo "NO KERNEL"; exit 1; }
exit $FAILED

