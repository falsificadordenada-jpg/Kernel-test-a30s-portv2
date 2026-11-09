#!/bin/bash
# Software PRNG + panic-path fix (no corecrypto kext, no iBoot panic region). cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-prng/apply_a30s_prng.py" "$PWD"
