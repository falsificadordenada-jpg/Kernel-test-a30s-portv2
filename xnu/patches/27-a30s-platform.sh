#!/bin/bash
# A30S platform expert (replaces AppleARMPE; fixes IOPanicPlatform panic). cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-platform/apply_a30s_platform.py" "$PWD"
