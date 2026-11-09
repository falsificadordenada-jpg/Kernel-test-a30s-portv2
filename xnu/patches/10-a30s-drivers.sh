#!/bin/bash
# Buttons + touch driver for the A30s (runs after 00-a30s-port.sh; cwd = work/xnu)
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-drivers/apply_a30s_drivers.py" "$PWD"
