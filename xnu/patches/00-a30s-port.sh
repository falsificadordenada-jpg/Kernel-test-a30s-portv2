#!/bin/bash
# Aplica o port A30S (Galaxy A30s, Exynos 7904) na arvore do xnu (cwd = work/xnu).
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-xnu-port/apply_a30s_port.py" "$PWD"
