# Bring-up guide

## Build (GitHub Actions, macOS runner)
1. Fetch xnu-4903.270.47 as before, then run
   `python3 a30s-xnu-port/apply_a30s_port.py <xnu dir>` **before** the kernel stage.
2. Build with `MACHINE_CONFIG=A30S KERNEL_CONFIG=DEVELOPMENT` (the old `T7000` value must go).
3. Collect `mach.development.a30s` (+ `.unstripped` for symbols).

## ADT
`python3 tools/mkadt.py -o adt.bin --mpidr <MPIDR_EL1 & 0xff> --timebase <CNTFRQ_EL0>`
The loader must read MPIDR_EL1 and CNTFRQ_EL0 at runtime and pass them in (the v16 test image
prints both on screen: fields `MP` and `FQ`). Place `adt.bin` after the kernel and put its
address/length in `boot_args` (`deviceTreeP`, `deviceTreeLength`).

## Boot-flow checklist (what each step needs)
| Step in `arm_init` | Needs from the port |
|---|---|
| `PE_init_platform` | ADT `arm-io` with `device_type "a30s-io"` and non-zero `ranges[1]` |
| `ml_parse_cpu_topology` | `/cpus/cpu0` with `state=running`, `reg`, `cpu-id` |
| `arm_vm_init` | `chosen/memory-map` node (assert at arm64/arm_vm_init.c:1045) |
| `pmap_bootstrap` | `/defaults` node (assert at arm/pmap.c:2825) |
| entropy | `chosen/random-seed`, 64 bytes |
| `PE_init_platform(TRUE)` | `interrupt-controller=master` (GICD, GICC) and a `timer` node |
| timer | CNTFRQ_EL0 == `timebase-frequency` (26 MHz); CNTP PPI 30 enabled in the GIC |

## Known risks / things to verify on hardware
1. GIC security state: if the Samsung bootloader leaves the GIC secure-only, `GICD_CTLR`
   and `IGROUPR0` writes can be ignored. `a30s_gic_init()` prints before/after values with
   `kprintf` (look for `a30s_gic:` lines) so this is visible.
2. MMIO is only reachable through the kernel's own mappings after `arm_vm_init`; the early
   framebuffer console must be mapped through `PE_create_console` / the video console.
3. Single CPU only. Secondary cores need PSCI `CPU_ON` and `a30s_gic_cpu_init()` per CPU.
4. A73 vs A53 cache geometry is read from CLIDR/CCSIDR by xnu; the compile-time cache
   macros inherited from BCM2837 only matter for set/way loops.
5. `PLATFORM_PANIC_LOG_PADDR` (0xBFFFC000) must stay outside RAM handed to the kernel.
6. Earlier binary tests stalled right after `arm_vm_init`. Likely suspects: the two
   asserts above (missing `chosen/memory-map` / `/defaults`) — the new ADT has both.
