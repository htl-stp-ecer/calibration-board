#!/usr/bin/env bash
# Hardware-reset target (does not re-flash).
source "$(dirname "$(readlink -f "$0")")/_common.sh"
"$PROG_CLI" -c port=SWD freq="$SWD_FREQ_KHZ" -rst 2>&1 | filter_st_output
