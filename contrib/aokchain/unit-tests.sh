#!/bin/bash
# Run the unit test suites one by one and compare with the suites that are known to pass
# (doc/aokchain/unit-baseline.txt).
#
# The whole test program in one run stops at the first suite that crashes (bloom_tests, whose
# test data is in the Bitcoin transaction format) and never reaches the suites behind it, so
# its summary says nothing about them. Run from the top of the source tree.
#
# Test cases listed in doc/aokchain/unit-not-run.txt (suite/case) are left out: they are
# marked disabled in the source, but naming their suite on the command line would run them.
# A suite without any test case (miner_tests) is reported as empty, not as failed.
#
# usage: contrib/aokchain/unit-tests.sh [path to test_aokchain]
set -u
BIN=${1:-src/test/test_aokchain}
BASELINE=doc/aokchain/unit-baseline.txt
NOTRUN=doc/aokchain/unit-not-run.txt
[ -x "$BIN" ] || { echo "no test program at $BIN"; exit 2; }
# A suite that hangs is stopped after 30 minutes. macOS has no GNU timeout (Homebrew's coreutils
# installs it as gtimeout); perl, which macOS has, does the same there. Like timeout, it ends the
# whole process group: a child left behind would keep the output open and the $(...) waiting.
if command -v timeout >/dev/null; then
    limit() { timeout 1800 "$@"; }
elif command -v gtimeout >/dev/null; then
    limit() { gtimeout 1800 "$@"; }
elif command -v perl >/dev/null; then
    limit() {
        perl -e '
            my $secs = shift;
            my $pid = fork // die "fork: $!\n";
            if ($pid == 0) { setpgrp(0, 0); exec { $ARGV[0] } @ARGV or die "exec $ARGV[0]: $!\n"; }
            setpgrp($pid, $pid);
            $SIG{ALRM} = sub { kill "KILL", -$pid };
            alarm $secs;
            waitpid($pid, 0);
            exit($? & 127 ? 128 + ($? & 127) : $? >> 8);
        ' 1800 "$@"
    }
else
    echo "neither timeout, gtimeout nor perl: a suite that hangs is not stopped"
    limit() { "$@"; }
fi
passed=0; failed=0; regressions=""; news=""; empty=""
for suite in $("$BIN" --list_content 2>&1 | grep '^[A-Za-z]' | tr -d '*'); do
    filter="$suite"
    for case in $(grep "^$suite/" "$NOTRUN" 2>/dev/null); do filter="$filter:!$case"; done
    out=$(limit "$BIN" --run_test="$filter" 2>&1); rc=$?
    if echo "$out" | grep -q "no test cases matching filter or all test cases were disabled"; then
        empty="$empty $suite"
        continue
    fi
    if [ $rc -eq 0 ]; then
        passed=$((passed + 1))
        grep -qx "$suite" "$BASELINE" || news="$news $suite"
    else
        failed=$((failed + 1))
        grep -qx "$suite" "$BASELINE" && regressions="$regressions $suite"
    fi
done
echo "suites passed: $passed, failed: $failed (the baseline has $(wc -l < "$BASELINE") passing suites)"
[ -n "$empty" ] && echo "without test cases:$empty"
[ -n "$news" ] && echo "pass now and are not in the baseline (add them):$news"
if [ -n "$regressions" ]; then
    echo "REGRESSION, these suites of the baseline fail:$regressions"
    exit 1
fi
echo "every suite of the baseline passes"
