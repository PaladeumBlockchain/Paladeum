#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing token mempool use cases"""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, disconnect_all_nodes, connect_all_nodes_bi

class TokenMempoolTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2


    def activate_tokens(self):
        self.log.info("Generating PLB and activating tokens...")
        n0, n1 = self.nodes[0], self.nodes[1]

        n0.generate(1)
        self.sync_all()
        n0.generate(216)
        self.sync_all()
        n1.generate(216)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['tokens']['status'])


    def issue_mempool_test(self):
        self.log.info("Testing (issue_mempool_test) mempool state after token issuance on two chains(only one is mined, the other is in the mempool), and then connecting the nodes together")
        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)

        token_name = "MEMPOOL"

        # Issue token on chain 1 and mine it into the blocks
        n0.issue(token_name)
        n0.generate(15)

        # Issue token on chain 2 but keep it in the mempool. No mining
        n1.issue(token_name)

        connect_all_nodes_bi(self.nodes, True)

        # Assert that the reorg was successful
        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())

        # Verify that the mempool are empty ( No extra transactions from the old chain made it into the mempool)
        assert_equal(0, n1.getmempoolinfo()['size'])
        assert_equal(0, n0.getmempoolinfo()['size'])

    def issue_mempool_test_extended(self):
        self.log.info("Testing (issue_mempool_test_extended) mempool state after, token issuance, reissue, transfer, issue sub token, getting reorged, with token override on other chain")
        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)
        # Create new token for testing
        token_name = "MEMPOOL_2"
        n0.issue(token_name)
        n0.generate(15)

        # Reissue that token
        address1 = n0.getnewaddress()
        n0.reissue(token_name=token_name, qty=2000, to_address=address1, change_address='', reissuable=True, new_units=-1)
        n0.generate(15)

        # Get a transfer address
        n1_address = n1.getnewaddress()

        # Transfer the token
        n0.transfer(token_name, 2, n1_address)

        # Issue sub token
        n0.issue(token_name + '/SUB')
        n0.generate(15)

        # Issue the same token on the other node
        n1.issue(token_name)

        # Create enough blocks so that n0 will reorg to n1 chain
        n1.generate(55)

        # Connect the nodes, a reorg should occur
        connect_all_nodes_bi(self.nodes, True)

        # Token the reorg occurred
        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())

        # Verify that the mempool are empty ( No extra transactions from the old chain made it into the mempool)
        assert_equal(0, n1.getmempoolinfo()['size'])
        assert_equal(0, n0.getmempoolinfo()['size'])

    def issue_mempool_test_extended_sub(self):
        self.log.info("Testing (issue_mempool_test) mempool state after token issuance on two chains(only one is mined, the other is in the mempool), and then connecting the nodes together")
        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)

        token_name = "MEMPOOL_3"

        # Issue token on chain 1 and mine it into the blocks
        n0.issue(token_name)
        n0.generate(15)

        # Issue token on chain 2 but keep it in the mempool. No mining
        n1.issue(token_name)

        # Mine a block on n1 chain
        n1.generate(1)

        # Issue sub tokens and unique tokens but only have it in the mempool
        n1.issue(token_name + '/SUB')
        n1.issue(token_name + '/SUB/1')
        n1.issue(token_name + '/SUB/2')
        n1.issue(token_name + '/SUB/3')
        n1.issue(token_name + '/SUB/4')
        n1.issue(token_name + '/SUB/5')
        n1.issue(token_name + '/SUB/6')
        n1.issue(token_name + '/SUB#1')
        n1.issue(token_name + '/SUB#2')
        n1.issue(token_name + '/SUB#3')
        n1.issue(token_name + '/SUB#4')
        n1.issue(token_name + '/SUB#5')
        n1.issue(token_name + '/SUB#6')
        n1.issue(token_name + '/SUB#7')
        n1.issue(token_name + '/SUB/SUB')
        n1.issue(token_name + '/SUB/SUB/SUB')
        n1.issue(token_name + '/SUB/SUB/SUB/SUB')
        assert_equal(17, n1.getmempoolinfo()['size'])

        connect_all_nodes_bi(self.nodes, True)

        # Assert that the reorg was successful
        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())

        # Verify that the mempools are empty ( No extra transactions from the old chain made it into the mempool)
        assert_equal(0, n1.getmempoolinfo()['size'])
        assert_equal(0, n0.getmempoolinfo()['size'])

    def run_test(self):
        self.activate_tokens()
        self.issue_mempool_test()
        self.issue_mempool_test_extended()
        self.issue_mempool_test_extended_sub()

if __name__ == '__main__':
    TokenMempoolTest().main()