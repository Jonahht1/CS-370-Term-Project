#!/bin/sh
# smoketest.sh - hardware smoke test for sensortest (run on the Pi).
#
# Starts the ASan build, checks that the kernel applied each line's
# bias and debounce, then stops it with SIGINT and requires a clean,
# leak-free exit. Does not need anyone to touch the sensors.
set -u

out=$(mktemp)
fail=0

# check <gpio> <text gpioinfo must show for that line>
check() {
	if gpioinfo -c gpiochip0 "$1" | grep -qF "$2"; then
		echo "ok   GPIO$1: $2"
	else
		echo "FAIL GPIO$1: expected '$2'"
		gpioinfo -c gpiochip0 "$1"
		fail=1
	fi
}

./sensortest-asan >"$out" 2>&1 &
pid=$!
sleep 1

check 17 'bias=disabled edges=both consumer="sensortest"'
check 22 'bias=disabled edges=both consumer="sensortest"'
check 27 'bias=pull-up edges=both debounce-period=50ms consumer="sensortest"'

kill -INT "$pid"
wait "$pid"
status=$?

if [ "$(grep -c ' initial ' "$out")" -eq 3 ]; then
	echo "ok   initial state printed for all 3 sensors"
else
	echo "FAIL expected 3 initial-state lines"
	fail=1
fi

# A nonzero exit here also catches ASan/LeakSanitizer reports.
if [ "$status" -eq 0 ]; then
	echo "ok   clean exit after SIGINT"
else
	echo "FAIL exit status $status"
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	echo "--- sensortest output ---"
	cat "$out"
fi
rm -f "$out"
exit "$fail"
