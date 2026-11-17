#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test the -alertnotify, -blocknotify and -walletnotify options.
"""

import os
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import ToHex
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, wait_until, connect_nodes_bi

# A version above the one the nodes expect (0x30000000 with tokens, and the offline staking
# bit 0x2000), as blocks that signal an unknown soft fork with bit 27 would have it
UNKNOWN_VERSION = 0x30000000 | 0x2000 | (1 << 27)

class NotificationsTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        self.block_filename = os.path.join(self.options.tmpdir, "blocks.txt")
        self.tx_filename = os.path.join(self.options.tmpdir, "transactions.txt")

        # -alertnotify and -blocknotify on node0, walletnotify on node1
        self.extra_args = [["-alertnotify=echo %%s >> %s" % self.alert_filename,
                            "-blocknotify=echo %%s >> %s" % self.block_filename],
                           ["-rescan",
                            "-walletnotify=echo %%s >> %s" % self.tx_filename]]
        super().setup_network()

    def submit_blocks_with_version(self, node, count, version):
        """Add count blocks of the given version to the chain of node. (The miner of the node
        always uses the version it computes, so the test builds these blocks itself.)"""
        tip = node.getbestblockhash()
        height = node.getblockcount()
        block_time = node.getblockheader(tip)["time"] + 1
        for _ in range(count):
            height += 1
            block = create_block(int(tip, 16), create_coinbase(height), block_time)
            block.nVersion = version
            block.solve()
            assert_equal(node.submitblock(ToHex(block)), None)
            tip = block.hash
            block_time += 1

    def run_test(self):
        self.log.info("test -blocknotify")
        block_count = 10
        blocks = self.nodes[1].generate(block_count)

        # wait at most 10 seconds for expected file size before reading the content
        wait_until(lambda: os.path.isfile(self.block_filename) and os.stat(self.block_filename).st_size >= (block_count * 65), err_msg="Wait for FileSize", timeout=10)

        # file content should equal the generated blocks hashes
        with open(self.block_filename, 'r') as f:
            assert_equal(sorted(blocks), sorted(f.read().splitlines()))

        self.log.info("test -walletnotify")
        # wait at most 10 seconds for expected file size before reading the content
        wait_until(lambda: os.path.isfile(self.tx_filename) and os.stat(self.tx_filename).st_size >= (block_count * 65), err_msg="Wait for FileSize", timeout=10)

        # file content should equal the generated transaction hashes
        txids_rpc = list(map(lambda t: t['txid'], self.nodes[1].listtransactions("*", block_count)))
        with open(self.tx_filename, 'r') as f:
            assert_equal(sorted(txids_rpc), sorted(f.read().splitlines()))
        os.remove(self.tx_filename)

        self.log.info("test -walletnotify after rescan")
        # restart node to rescan to force wallet notifications
        self.restart_node(1)
        connect_nodes_bi(self.nodes, 0, 1)

        wait_until(lambda: os.path.isfile(self.tx_filename) and os.stat(self.tx_filename).st_size >= (block_count * 65), err_msg="Wait for FileSize", timeout=10)

        # file content should equal the generated transaction hashes
        txids_rpc = list(map(lambda t: t['txid'], self.nodes[1].listtransactions("*", block_count)))
        with open(self.tx_filename, 'r') as f:
            assert_equal(sorted(txids_rpc), sorted(f.read().splitlines()))

        # Add 51 blocks of an unknown version. -alertnotify should trigger on the 51st
        # (more than half of the last 100 blocks have an unexpected version).
        self.log.info("test -alertnotify")
        self.submit_blocks_with_version(self.nodes[1], 51, UNKNOWN_VERSION)
        self.sync_all()

        # Give paladeumd 10 seconds to write the alert notification
        wait_until(lambda: os.path.isfile(self.alert_filename) and os.path.getsize(self.alert_filename), err_msg="Wait for FileSize", timeout=10)

        with open(self.alert_filename, 'r', encoding='utf8') as f:
            alert_text = f.read()

        # More blocks of the unknown version should not give more alerts:
        self.submit_blocks_with_version(self.nodes[1], 2, UNKNOWN_VERSION)
        self.sync_all()

        with open(self.alert_filename, 'r', encoding='utf8') as f:
            alert_text2 = f.read()

        self.log.info("-alertnotify should not continue notifying for more unknown version blocks")
        assert_equal(alert_text, alert_text2)

if __name__ == '__main__':
    NotificationsTest().main()
