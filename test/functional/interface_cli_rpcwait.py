#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test aokchain-cli -rpcwait while the node has not written its cookie yet.

 - without -rpcwait, and with no node, the CLI says that it cannot connect
 - started with -rpcwait before the node, while there is no authentication
   cookie yet, the CLI waits until the node is up and answers
 - with the node up and no cookie to read, the CLI says that it found no
   credentials, also with -rpcwait (it does not wait for ever)
"""

import os
import subprocess
import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal


class CliRpcWaitTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def cli_process(self, *args):
        node = self.nodes[0]
        return subprocess.Popen([node.cli.binary, "-datadir=" + node.datadir] + list(args),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    def run_test(self):
        node = self.nodes[0]
        node.generate(3)
        cookie = os.path.join(node.datadir, "regtest", ".cookie")
        assert os.path.exists(cookie)
        self.stop_node(0)
        assert not os.path.exists(cookie)  # the node removes it when it stops

        self.log.info("Without -rpcwait and without a node the CLI cannot connect")
        cli = self.cli_process("getblockcount")
        out, err = cli.communicate(timeout=60)
        assert cli.returncode != 0
        assert "couldn't connect to server" in err, err

        self.log.info("With -rpcwait it waits for the node, and for its cookie")
        cli = self.cli_process("-rpcwait", "getblockcount")
        time.sleep(3)
        assert cli.poll() is None, cli.communicate()  # still waiting, without a cookie
        self.start_node(0)
        out, err = cli.communicate(timeout=120)
        assert_equal((cli.returncode, out.strip(), err), (0, "3", ""))

        self.log.info("With the node up and no cookie to read, the error names the credentials")
        missing = "-rpccookiefile=" + os.path.join(node.datadir, "nosuchcookie")
        for args in ([missing], ["-rpcwait", missing]):
            cli = self.cli_process(*(args + ["getblockcount"]))
            out, err = cli.communicate(timeout=60)
            assert cli.returncode != 0
            assert "Could not locate RPC credentials" in err, err
        assert_equal(node.getblockcount(), 3)


if __name__ == '__main__':
    CliRpcWaitTest().main()
