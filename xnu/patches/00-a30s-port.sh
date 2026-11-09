#!/bin/bash
# Applies the A30S port (Galaxy A30s, Exynos 7904) to the xnu tree (cwd = work/xnu).
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-xnu-port/apply_a30s_port.py" "$PWD"
