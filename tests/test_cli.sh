#!/bin/sh
set -eu

bin=${1:-./build/lpmcs}
output=$(mktemp)
child=

cleanup() {
    if [ -n "$child" ]; then
        kill -KILL "$child" 2>/dev/null || true
        wait "$child" 2>/dev/null || true
    fi
    rm -f "$output"
}
trap cleanup EXIT HUP INT TERM

yes > /dev/null &
child=$!
"$bin" --once > "$output"

cpu=$(awk -v pid="$child" '$1 == pid { print $4; exit }' "$output")
if [ -z "$cpu" ]; then
    echo "FAIL: --once omitted the CPU-burner process" >&2
    exit 1
fi

if ! awk -v cpu="$cpu" 'BEGIN { exit !(cpu > 5.0) }'; then
    echo "FAIL: --once reported $cpu% CPU for a busy process" >&2
    exit 1
fi

echo "ok - --once takes two samples and reports interval CPU"
