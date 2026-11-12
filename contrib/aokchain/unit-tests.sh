#!/bin/bash
# Run the unit test suites one by one and compare with the suites that are known to pass
# (doc/aokchain/unit-baseline.txt).
#
# The whole test program in one run stops at the first suite that crashes (bloom_tests, whose
# test data is in the Bitcoin transaction format) and never reaches the suites behind it, so
# its summary says nothing about them. Run from the top of the source tree.
#
# usage: contrib/aokchain/unit-tests.sh [path to test_aokchain]
set -u
BIN=${1:-src/test/test_aokchain}
BASELINE=doc/aokchain/unit-baseline.txt
[ -x "$BIN" ] || { echo "no test program at $BIN"; exit 2; }
passed=0; failed=0; regressions=""; news=""
for suite in $("$BIN" --list_content 2>&1 | grep '^[A-Za-z]' | tr -d '*'); do
    if timeout 1800 "$BIN" --run_test="$suite" > /dev/null 2>&1; then
        passed=$((passed + 1))
        grep -qx "$suite" "$BASELINE" || news="$news $suite"
    else
        failed=$((failed + 1))
        grep -qx "$suite" "$BASELINE" && regressions="$regressions $suite"
    fi
done
echo "suites passed: $passed, failed: $failed (the baseline has $(wc -l < "$BASELINE") passing suites)"
[ -n "$news" ] && echo "pass now and are not in the baseline (add them):$news"
if [ -n "$regressions" ]; then
    echo "REGRESSION, these suites of the baseline fail:$regressions"
    exit 1
fi
echo "every suite of the baseline passes"
