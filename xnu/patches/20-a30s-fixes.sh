#!/bin/bash
# Source versions of the proven binary patches (runs after 00 and 10; cwd = work/xnu).
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-fixes/apply_a30s_fixes.py" "$PWD"
