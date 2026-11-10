#!/bin/bash
# IOPanicPlatform fallback + skip XNUPOST self tests (recovered from M4v50). cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-boot/apply_a30s_boot.py" "$PWD"
