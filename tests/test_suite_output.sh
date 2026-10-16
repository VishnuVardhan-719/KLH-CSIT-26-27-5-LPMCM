#!/bin/sh
set -eu

testbin=${1:-./build/run_tests}
output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

"$testbin" > "$output"
headers=$(grep -c '^LPMCS test suite$' "$output")
if [ "$headers" -ne 1 ]; then
    echo "FAIL: test header appeared $headers times after fork" >&2
    exit 1
fi

echo "ok - forked tests do not duplicate buffered output"
