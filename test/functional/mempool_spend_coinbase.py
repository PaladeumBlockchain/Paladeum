#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test spending coinbase transactions.

The coinbase transaction in block N can appear in block
N+10 (COINBASE_MATURITY)... so is valid in the mempool when the best block
height is N+9.
This test makes sure coinbase spends that will be mature
in the next block are accepted into the memory pool,
but less mature coinbase spends are NOT.
"""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import (assert_equal, create_tx, assert_raises_rpc_error)

COINBASE_MATURITY = 10

# Create one-input, one-output transactions that spend a coinbase (10 PLB):
class MempoolSpendCoinbaseTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-checkmempool"]]

    def run_test(self):
        assert_equal(self.nodes[0].getblockcount(), 200)
        # The last blocks of the cached chain are not node0's: mine the ones whose coinbases are spent
        self.nodes[0].generate(COINBASE_MATURITY + 1)
        chain_height = self.nodes[0].getblockcount()
        node0_address = self.nodes[0].getnewaddress()

        # Coinbase at height chain_height-COINBASE_MATURITY+1 ok in mempool, should
        # get mined. Coinbase at height chain_height-COINBASE_MATURITY+2 is
        # is too immature to spend.
        first = chain_height - COINBASE_MATURITY + 1
        b = [ self.nodes[0].getblockhash(n) for n in range(first, first + 2) ]
        coinbase_txids = [ self.nodes[0].getblock(h)['tx'][0] for h in b ]
        spends_raw = [ create_tx(self.nodes[0], txid, node0_address, 9.99) for txid in coinbase_txids ]

        spend_101_id = self.nodes[0].sendrawtransaction(spends_raw[0])

        # the second coinbase should be too immature to spend
        assert_raises_rpc_error(-26,"bad-txns-premature-spend-of-coinbase", self.nodes[0].sendrawtransaction, spends_raw[1])

        # mempool should have just spend_101:
        assert_equal(self.nodes[0].getrawmempool(), [ spend_101_id ])

        # mine a block, spend_101 should get confirmed
        self.nodes[0].generate(1)
        assert_equal(set(self.nodes[0].getrawmempool()), set())

        # ... and now the second one can be spent:
        spend_102_id = self.nodes[0].sendrawtransaction(spends_raw[1])
        assert_equal(self.nodes[0].getrawmempool(), [ spend_102_id ])

if __name__ == '__main__':
    MempoolSpendCoinbaseTest().main()
