#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the old peer cutoff.

From a height of the active chain on (-oldpeercutoffheight on regtest), peers
that announce a protocol version below 70021 are not kept: a new connection
is refused with an "obsolete" reject, and a peer that is connected when the
chain reaches the height is disconnected. Nobody is banned. Below the height
nothing changes, and peers at 70021 are never affected.
"""

from test_framework.mininode import NetworkThread, NodeConn, NodeConnCB, mininode_lock, msg_version
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, p2p_port, wait_until

CUTOFF_HEIGHT = 10
OLD_VERSION = 70020
NEW_VERSION = 70021


class Peer(NodeConnCB):
    """A peer that announces the protocol version it is given."""

    def __init__(self, version):
        super().__init__()
        self.version = version
        self.closed = False
        self.reject = None

    def on_close(self, conn):
        super().on_close(conn)
        self.closed = True

    def on_reject(self, conn, message):
        self.reject = message

    def connect(self, node):
        conn = NodeConn('127.0.0.1', p2p_port(0), node, self, send_version=False)
        self.add_connection(conn)
        version = msg_version()
        version.nVersion = self.version
        version.addrTo.ip = '127.0.0.1'
        version.addrTo.port = p2p_port(0)
        version.addrFrom.ip = "0.0.0.0"
        version.addrFrom.port = 0
        conn.send_message(version, True)
        return conn


class OldPeerCutoffTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-oldpeercutoffheight=%d" % CUTOFF_HEIGHT]]

    def versions(self):
        return sorted(peer["version"] for peer in self.nodes[0].getpeerinfo())

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getnetworkinfo()["protocolversion"], NEW_VERSION)
        node.generate(CUTOFF_HEIGHT - 5)

        self.log.info("Below the height an old peer and a new peer are both accepted")
        old, new = Peer(OLD_VERSION), Peer(NEW_VERSION)
        old.connect(node)
        new.connect(node)
        NetworkThread().start()
        wait_until(lambda: old.message_count["verack"] and new.message_count["verack"], timeout=20, lock=mininode_lock)
        wait_until(lambda: self.versions() == [OLD_VERSION, NEW_VERSION], timeout=20)

        self.log.info("One block below the height: still both")
        node.generate(4)
        assert_equal(node.getblockcount(), CUTOFF_HEIGHT - 1)
        old.sync_with_ping()
        new.sync_with_ping()
        assert_equal(self.versions(), [OLD_VERSION, NEW_VERSION])

        self.log.info("At the height the old peer is disconnected, the new one stays")
        node.generate(1)
        assert_equal(node.getblockcount(), CUTOFF_HEIGHT)
        wait_until(lambda: old.closed, timeout=30, lock=mininode_lock)
        wait_until(lambda: self.versions() == [NEW_VERSION], timeout=20)
        new.sync_with_ping()
        assert not new.closed
        assert_equal(node.listbanned(), [])

        self.log.info("After the height an old peer is refused with an obsolete reject, a new one is accepted")
        late_old, late_new = Peer(OLD_VERSION), Peer(NEW_VERSION)
        late_old.connect(node)
        late_new.connect(node)
        wait_until(lambda: late_old.closed, timeout=30, lock=mininode_lock)
        wait_until(lambda: late_new.message_count["verack"], timeout=20, lock=mininode_lock)
        with mininode_lock:
            assert_equal(late_old.message_count["verack"], 0)
            assert late_old.reject is not None
            assert_equal(late_old.reject.code, 0x11)   # REJECT_OBSOLETE
            assert b"70021" in late_old.reject.reason
        wait_until(lambda: self.versions() == [NEW_VERSION, NEW_VERSION], timeout=20)
        assert_equal(node.listbanned(), [])

        self.log.info("It goes on with the chain")
        node.generate(3)
        new.sync_with_ping()
        late_new.sync_with_ping()
        assert_equal(self.versions(), [NEW_VERSION, NEW_VERSION])

        self.log.info("Without the option nothing is cut off")
        new.connection.disconnect_node()
        late_new.connection.disconnect_node()
        wait_until(lambda: len(node.getpeerinfo()) == 0, timeout=20)
        self.restart_node(0, [])
        node = self.nodes[0]
        assert node.getblockcount() > CUTOFF_HEIGHT
        again = Peer(OLD_VERSION)
        again.connect(node)
        NetworkThread().start()  # the network thread ended with the last connection
        wait_until(lambda: again.message_count["verack"], timeout=20, lock=mininode_lock)
        node.generate(1)
        again.sync_with_ping()
        assert_equal(self.versions(), [OLD_VERSION])
        again.connection.disconnect_node()
        wait_until(lambda: len(node.getpeerinfo()) == 0, timeout=20)


if __name__ == '__main__':
    OldPeerCutoffTest().main()
