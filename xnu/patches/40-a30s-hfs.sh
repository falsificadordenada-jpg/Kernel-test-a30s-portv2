#!/bin/bash
# Built-in HFS+ (Apple OSS hfs-522.100.5, fetched at build time). Runs after 30; cwd = work/xnu.
# Needs network access to github.com (or set A30S_HFS_SRC to a local hfs checkout).
set -e
D="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$D/a30s-hfs/apply_a30s_hfs.py" "$PWD"
