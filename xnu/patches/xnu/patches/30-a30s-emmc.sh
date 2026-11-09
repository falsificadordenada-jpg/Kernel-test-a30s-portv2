#!/bin/bash
# eMMC root device (DW-MMC0 block driver + rd=emmcroot0 selection). Runs after 00/10/20; cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-emmc/apply_a30s_emmc.py" "$PWD"
