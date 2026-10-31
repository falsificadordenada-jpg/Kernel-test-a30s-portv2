# a30s-xnu-port

Port of Apple's XNU (xnu-4903.270.47, the iOS 12 kernel) to the Samsung Galaxy A30s
(Exynos 7904: 2x Cortex-A73 + 6x Cortex-A53, ARMv8.0, GICv2, 26 MHz arch timer).

This pack is the **second test kernel**: a real source-level port instead of binary
patches on a T7000 (iPhone 6) kernel. It adds a new board class, `A30S`, derived from the
generic-ARMv8 `BCM2837` (Raspberry Pi 3) class that already ships in xnu-4903, so none of
Apple's implementation-defined system registers (the S3_x_C11/C15 ones) are touched.

## Contents
| Path | What |
|---|---|
| `apply_a30s_port.py` | Applies every source change to an xnu-4903 tree (exact-match, idempotent) |
| `overlay/pexpert/pexpert/arm64/A30S.h` | Board header (addresses, timer PPI, panic-log RAM) |
| `overlay/osfmk/arm64/a30s_gic.{c,h}` | GICv2 driver + IRQ entry (timer PPI -> `rtclock_intr`) |
| `overlay/config/MASTER.arm64.a30s` | Kernel config (same as BCM2837) |
| `tools/mkadt.py` | Generates the DeviceTree (ADT) blob; `--selftest` checks every kernel lookup |
| `docs/BRINGUP.md` | Build steps, boot-flow checklist, known risks |

## What changed in xnu (all done by `apply_a30s_port.py`)
- `makedefs/MakeInc.def`: `A30S` machine config, `-DARM64_BOARD_CONFIG_A30S`
- `pexpert/pexpert/arm64/board_config.h`: `A30S` class (no `APPLE_ARM64_ARCH_FAMILY`)
- `osfmk/arm/proc_reg.h`, `osfmk/arm64/machine_routines.c`: treat `A30S` like `BCM2837`
- `pexpert/arm/pe_identify_machine.c`: maps GICD + GICC from the ADT, calls `a30s_gic_init()`
- `osfmk/conf/files.arm64`: builds `a30s_gic.c`

## Status
Not built yet (needs the macOS CI). Patch application was tested against the real
xnu-4903.270.47 tree and `a30s_gic.c` passes a clang syntax check against stub headers.
Expect a few CI rounds to fix compile errors.

## License
Overlay and tools: MIT. XNU is under the Apple Public Source License 2.0.
