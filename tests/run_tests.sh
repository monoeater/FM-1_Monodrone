#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build MONODRONE's host simulator and run its scripted tests (app/tests/*.txt).
#   sh tests/run_tests.sh
set -e
cd "$(dirname "$0")/.."
./build.sh --host >/dev/null
fail=0
for t in app/tests/*.txt; do
    out=$(mktemp -d)
    if app/build/host/monodrone_host "$t" "$out" 2>&1 | grep -q FAILED; then echo "FAIL $t"; fail=1; else echo "ok   $t"; fi
    rm -rf "$out"
done
exit $fail
