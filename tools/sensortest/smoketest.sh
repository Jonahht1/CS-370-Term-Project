#!/bin/sh
# smoketest.sh - hardware smoke test for sensortest (run on the Pi).
#
# Starts the ASan build, checks that the kernel applied each line's
# bias and debounce, then stops it with SIGINT and requires a clean,
# leak-free exit and a complete session in the log. Does the same for
# polling mode (-p), and checks that invalid options are rejected before
# any log is created. Logs to temp files, never to the real events.log.
# Does not need anyone to touch the sensors.
set -u

out=$(mktemp)
log=$(mktemp)
pout=$(mktemp)
plog=$(mktemp)
argdir=$(mktemp -d)
bin=$(pwd)/sensortest-asan
fail=0

# --- invalid options: nonzero exit, a message, and no log file created ---

# bad_args <description> [sensortest options...]
# Runs inside an empty temp dir so a default events.log would show up too.
bad_args() {
	desc=$1
	shift
	err=$(cd "$argdir" && "$bin" "$@" 2>&1 >/dev/null)
	st=$?
	if [ "$st" -ne 0 ] && [ -n "$err" ] && [ -z "$(ls -A "$argdir")" ]; then
		echo "ok   rejects $desc"
	else
		echo "FAIL should reject $desc (exit $st, stderr '$err')"
		ls -A "$argdir"
		fail=1
	fi
	rm -f "$argdir"/*
}

bad_args '-i without -p'        -i 20 x.log
bad_args 'interval 0'           -p -i 0 x.log
bad_args 'negative interval'    -p -i -5 x.log
bad_args 'non-numeric interval' -p -i abc x.log
bad_args 'trailing junk'        -p -i 10ms x.log
bad_args 'empty interval'       -p -i '' x.log
bad_args 'interval above 10000' -p -i 10001 x.log
bad_args 'overflowing interval' -p -i 99999999999999999999 x.log
bad_args 'missing -i value'     -p -i
bad_args 'unknown option'       -x x.log
bad_args 'two log files'        a.log b.log
rmdir "$argdir"

# --- default (edge-event) mode ---

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

./sensortest-asan "$log" >"$out" 2>&1 &
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

# log_check <log file> <expected count> <pattern> <description>
log_check() {
	if [ "$(grep -c -- "$3" "$1")" -eq "$2" ]; then
		echo "ok   log has $4"
	else
		echo "FAIL log should have $4"
		fail=1
	fi
}

log_check "$log" 1 ' sensortest start$' 'start marker'
log_check "$log" 1 ' sensortest mode edge-events$' 'edge-events mode line'
log_check "$log" 3 ' initial ' '3 initial sensor states'
log_check "$log" 1 ' sensortest stop$' 'stop marker'

# A nonzero exit here also catches ASan/LeakSanitizer reports.
if [ "$status" -eq 0 ]; then
	echo "ok   clean exit after SIGINT"
else
	echo "FAIL exit status $status"
	fail=1
fi

# --- polling mode, default interval ---
# No edge detection and no kernel debounce may be requested, so gpioinfo
# must show nothing between the bias and the consumer.

./sensortest-asan -p "$plog" >"$pout" 2>&1 &
pid=$!
sleep 1

check 17 'input bias=disabled consumer="sensortest"'
check 22 'input bias=disabled consumer="sensortest"'
check 27 'input bias=pull-up consumer="sensortest"'

kill -TERM "$pid"
wait "$pid"
pstatus=$?

log_check "$plog" 1 ' sensortest start$' 'start marker (polling)'
log_check "$plog" 1 ' sensortest mode polling interval=10ms ' \
	'polling mode line with 10 ms default'
log_check "$plog" 3 ' initial ' '3 initial sensor states (polling)'
log_check "$plog" 0 ' rising \| falling ' 'no edge-event lines (polling)'
log_check "$plog" 1 ' sensortest stop$' 'stop marker (polling)'

if [ "$pstatus" -eq 0 ]; then
	echo "ok   clean exit after SIGTERM (polling)"
else
	echo "FAIL exit status $pstatus (polling)"
	fail=1
fi

# --- polling mode, explicit interval (appends to the same temp log) ---

./sensortest-asan -p -i 25 "$plog" >>"$pout" 2>&1 &
pid=$!
sleep 1
kill -INT "$pid"
wait "$pid"
pstatus=$?

log_check "$plog" 1 ' sensortest mode polling interval=25ms ' \
	'polling mode line with -i 25'
log_check "$plog" 2 ' sensortest stop$' 'second session appended'

if [ "$pstatus" -eq 0 ]; then
	echo "ok   clean exit after SIGINT (polling -i 25)"
else
	echo "FAIL exit status $pstatus (polling -i 25)"
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	echo "--- sensortest output ---"
	cat "$out"
	echo "--- log file ---"
	cat "$log"
	echo "--- polling output ---"
	cat "$pout"
	echo "--- polling log file ---"
	cat "$plog"
fi
rm -f "$out" "$log" "$pout" "$plog"
exit "$fail"
