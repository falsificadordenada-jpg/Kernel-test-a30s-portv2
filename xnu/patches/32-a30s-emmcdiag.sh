#!/bin/bash
# eMMC diagnostics + tolerant EXT_CSD read. Runs after 30-a30s-emmc.sh. cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-emmcdiag/apply_a30s_emmcdiag.py" "$PWD"
