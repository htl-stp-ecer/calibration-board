#!/usr/bin/env bash
# Full chip erase. Useful for recovering a bricked target.
source "$(dirname "$(readlink -f "$0")")/_common.sh"
"$PROG_CLI" -c port=SWD freq="$SWD_FREQ_KHZ" mode=UR -e all 2>&1 | filter_st_output
