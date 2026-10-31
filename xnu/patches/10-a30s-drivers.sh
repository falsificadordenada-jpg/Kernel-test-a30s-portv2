#!/bin/bash
# Botoes + touch do A30s (roda depois do 00-a30s-port.sh; cwd = work/xnu)
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-drivers/apply_a30s_drivers.py" "$PWD"
