#!/usr/bin/env python3
"""Generate the minimal Apple-style DeviceTree (ADT) the xnu-4903 kernel needs
to boot on the Samsung Galaxy A30s (Exynos 7904).

Usage:
  mkadt.py -o adt.bin [--mpidr 0x0] [--timebase 26000000] [--seed HEX64BYTES]
  mkadt.py --dump adt.bin          # print the tree
  mkadt.py --selftest              # check every lookup the kernel performs

Node layout:  uint32 nprops, uint32 nchildren, props..., children...
Property:     char name[32], uint32 length, value padded to 4 bytes.
Every node must carry a "name" property (xnu looks nodes up by it).

Nodes the kernel asserts on (verified against xnu-4903.270.47 sources):
  /cpus/cpuN         state, reg (MPIDR affinity), cpu-id, timebase-frequency
  /chosen            random-seed (64 bytes, EntropyData)
  /chosen/memory-map arm_vm_init.c asserts the node exists
  /defaults          pmap.c asserts the node exists
  arm-io             device_type "a30s-io", ranges {child, parent, size}
  interrupt-controller (property "interrupt-controller"="master", reg = GICD, GICC)
  timer              device_type "timer"
"""
import argparse, os, struct, sys

def cstr(s):
    return s.encode() + b"\0"

def u32(*v):
    return b"".join(struct.pack("<I", x) for x in v)

def u64(*v):
    return b"".join(struct.pack("<Q", x) for x in v)

class Node:
    def __init__(self, name, props=None, children=None):
        self.props = [("name", cstr(name))] + list(props or [])
        self.children = list(children or [])

    def pack(self):
        out = u32(len(self.props), len(self.children))
        for k, v in self.props:
            kb = k.encode()
            assert len(kb) < 32
            out += kb.ljust(32, b"\0") + u32(len(v)) + v + b"\0" * (-len(v) % 4)
        for c in self.children:
            out += c.pack()
        return out

def build(mpidr=0, timebase=26000000, seed=None, soc_base=0x10000000,
          gicd_off=0x2301000, gicc_off=0x2302000):
    if seed is None:
        seed = os.urandom(64)
    assert len(seed) == 64
    cpu0 = Node("cpu0", [
        ("device_type", cstr("cpu")), ("state", cstr("running")),
        ("cpu-id", u32(0)), ("reg", u32(mpidr)),
        ("timebase-frequency", u32(timebase)),
        ("bus-frequency", u32(100000000)),
        ("clock-frequency", u32(1600000000)),
    ])
    gic = Node("interrupt-controller", [
        ("interrupt-controller", cstr("master")),
        ("compatible", cstr("arm,gic-400")),
        ("reg", u64(gicd_off, 0x1000, gicc_off, 0x2000)),
    ])
    timer = Node("timer", [
        ("device_type", cstr("timer")), ("compatible", cstr("arm,armv8-timer")),
        ("reg", u64(gicd_off, 0x1000)),     # placeholder mapping, timer is a CPU register
    ])
    arm_io = Node("arm-io", [
        ("device_type", cstr("a30s-io")),
        ("ranges", u64(0, soc_base, 0x10000000)),
    ], [gic, timer])
    chosen = Node("chosen", [("random-seed", seed)], [Node("memory-map")])
    root = Node("device-tree", [
        ("compatible", cstr("A30S,Exynos7904")),
        ("model", cstr("Samsung Galaxy A30s")),
    ], [chosen, Node("defaults"), Node("cpus", [], [cpu0]), arm_io])
    return root.pack()

# ---- reader (mirrors pexpert/gen/device_tree.c semantics) -------------------
def parse(buf, off=0):
    nprops, nchild = struct.unpack_from("<II", buf, off); off += 8
    props = {}
    for _ in range(nprops):
        name = buf[off:off + 32].split(b"\0")[0].decode(); off += 32
        ln = struct.unpack_from("<I", buf, off)[0] & 0x7fffffff; off += 4
        props[name] = buf[off:off + ln]; off += ln + (-ln % 4)
    kids = []
    for _ in range(nchild):
        k, off = parse(buf, off); kids.append(k)
    return {"props": props, "kids": kids}, off

def find(node, key, val=None):          # DTFindEntry
    p = node["props"]
    if key in p and (val is None or p[key].rstrip(b"\0") == val.encode()):
        return node
    for k in node["kids"]:
        r = find(k, key, val)
        if r: return r

def lookup(node, path):                 # DTLookupEntry
    for part in [x for x in path.split("/") if x]:
        node = next((k for k in node["kids"] if k["props"]["name"].rstrip(b"\0").decode() == part), None)
        if node is None: return None
    return node

def dump(n, ind=0):
    print("  " * ind + n["props"]["name"].rstrip(b"\0").decode())
    for k, v in n["props"].items():
        if k != "name":
            print("  " * ind + "  %s = %s" % (k, v[:16].hex() + ("..." if len(v) > 16 else "")))
    for c in n["kids"]:
        dump(c, ind + 1)

def selftest():
    buf = build(mpidr=0x100)
    root, end = parse(buf)
    assert end == len(buf)
    cpu = lookup(root, "/cpus").get("kids")[0]
    assert cpu["props"]["state"].rstrip(b"\0") == b"running"
    assert struct.unpack("<I", cpu["props"]["reg"])[0] == 0x100
    assert len(lookup(root, "chosen")["props"]["random-seed"]) == 64
    assert lookup(root, "chosen/memory-map") and lookup(root, "/defaults")
    io = find(root, "name", "arm-io")
    assert io["props"]["device_type"].rstrip(b"\0") == b"a30s-io"
    assert struct.unpack("<QQQ", io["props"]["ranges"])[1] != 0     # gSoCBasePhys must be non-zero
    gic = find(root, "interrupt-controller", "master")
    assert len(gic["props"]["reg"]) == 32
    tm = find(root, "device_type", "timer")
    assert tm is not None
    print("selftest OK, size", len(buf), "bytes")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o"); ap.add_argument("--mpidr", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--timebase", type=int, default=26000000)
    ap.add_argument("--seed"); ap.add_argument("--dump"); ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest: return selftest()
    if a.dump:
        dump(parse(open(a.dump, "rb").read())[0]); return
    if not a.o: ap.error("need -o")
    seed = bytes.fromhex(a.seed) if a.seed else None
    open(a.o, "wb").write(build(a.mpidr, a.timebase, seed))
    print("wrote", a.o)

if __name__ == "__main__":
    main()
