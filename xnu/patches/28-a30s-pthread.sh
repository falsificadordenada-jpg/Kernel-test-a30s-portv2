#!/bin/bash
# A30S built-in pthread stub table (fixes pthread_init panic). cwd = work/xnu.
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-pthread/apply_a30s_pthread.py" "$PWD"
