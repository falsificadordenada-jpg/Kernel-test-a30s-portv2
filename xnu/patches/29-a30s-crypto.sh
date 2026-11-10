#!/bin/bash
# A30S built-in crypto function table (fixes tcp_init data abort). Runs after 25-a30s-prng.sh. cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-crypto/apply_a30s_crypto.py" "$PWD"
