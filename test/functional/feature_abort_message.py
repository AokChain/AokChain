#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that a node that ends on a failed internal check leaves that in its log.

A failed check (assert) ends the program at once; what failed is written to the standard error
output by the C library. The node adds a line to its debug log, and a node that runs in the
background (-daemon), which has no standard error output, gets the debug log for it: the check,
its file and its line are then in the log as well.

-testabortat (-regtest only) makes a check fail at a named point.
"""

import os
import signal
import subprocess

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, wait_until

LINE = "The program ends here on the signal SIGABRT"
CHECK = "Assertion `!\"-testabortat\"' failed"


class AbortMessageTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def log_text(self):
        with open(os.path.join(self.nodes[0].datadir, "regtest", "debug.log"), encoding="utf8", errors="replace") as log:
            return log.read()

    def run_test(self):
        node = self.nodes[0]
        node.generate(5)
        tip = node.getbestblockhash()
        self.stop_node(0)

        self.log.info("In the foreground: the check is on the standard error output, and the log has a line")
        err_path = os.path.join(self.options.tmpdir, "node0.stderr")
        with open(err_path, "w") as err:
            node.start(["-testabortat=running-mark"], stderr=err)
            assert_equal(node.process.wait(timeout=240), -6)
        node.running = False
        node.process = None
        with open(err_path) as err:
            assert CHECK in err.read()
        text = self.log_text()
        assert_equal(text.count(LINE), 1)
        assert_equal(text.count(CHECK), 0)

        self.log.info("In the background: the check itself is in the log too")
        subprocess.check_call(node.args + ["-daemon", "-testabortat=running-mark"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=240)
        wait_until(lambda: self.log_text().count(LINE) == 2, timeout=240)
        text = self.log_text()
        assert_equal(text.count(CHECK), 1)
        assert text.index(CHECK) < text.rindex(LINE)
        # (the program in the background is gone when its process is: it holds the data directory until then)
        with open(os.path.join(node.datadir, "regtest", "aokchaind.pid")) as f:
            pid = int(f.read())
        wait_until(lambda: not os.path.exists("/proc/%d" % pid), timeout=240)

        self.log.info("The node starts again")
        self.start_node(0)
        assert_equal(self.nodes[0].getbestblockhash(), tip)

        self.log.info("The signal sent from outside ends the program too, with the line in its log")
        node = self.nodes[0]
        lines = self.log_text().count(LINE)
        os.kill(node.process.pid, signal.SIGABRT)
        assert_equal(node.process.wait(timeout=60), -6)
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None
        assert_equal(self.log_text().count(LINE), lines + 1)
        self.start_node(0)
        assert_equal(self.nodes[0].getbestblockhash(), tip)


if __name__ == '__main__':
    AbortMessageTest().main()
