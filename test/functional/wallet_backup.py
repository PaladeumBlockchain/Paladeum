#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test the wallet backup features.

Test case is:
4 nodes. 1 2 and 3 send transactions between each other,
fourth node is a miner.
1 mines a block to start (block 1 pays 1,000,000,000 PLB), 2 and 3
mine 5 blocks each (10 PLB a block), then the miner creates 10
blocks (the coinbase maturity) so 1 2 3 have mature coins to spend.
Then 5 iterations of 1/2/3 sending coins amongst
themselves to get transactions in the wallets,
and the miner mining one block.

Wallets are backed up using dumpwallet/backupwallet.
Then 5 more iterations of transactions and mining a block.

Miner then generates 11 more blocks, so any
transaction fees paid mature.

Sanity check:
  Sum(1,2,3,4 balances) == the rewards of the mature blocks

1/2/3 are shutdown, and their wallets erased.
Then restore using wallet.dat backup. And
confirm 1/2/3/4 balances are same as before.

Shutdown again, restore using importwallet,
and confirm again balances are correct.
"""

from random import randint
import shutil
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import connect_nodes, Decimal, sync_mempools, sync_blocks, os, assert_equal, wallet_file

# A coinbase output can be spent once it has COINBASE_MATURITY + 1 confirmations
COINBASE_MATURITY = 10

class WalletBackupTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # nodes 1, 2,3 are spenders, let's give them a keypool=100
        self.extra_args = [["-keypool=100"], ["-keypool=100"], ["-keypool=100"], []]

    def setup_network(self, split=False):
        self.setup_nodes()
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)
        self.sync_all()

    def one_send(self, from_node, to_address):
        if randint(1, 2) == 1:
            amount = Decimal(randint(1,10)) / Decimal(10)
            self.nodes[from_node].sendtoaddress(to_address, amount)

    def do_one_round(self):
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        a2 = self.nodes[2].getnewaddress()

        self.one_send(0, a1)
        self.one_send(0, a2)
        self.one_send(1, a0)
        self.one_send(1, a2)
        self.one_send(2, a0)
        self.one_send(2, a1)

        # Have the miner (node3) mine a block.
        # Must sync mempools before mining.
        sync_mempools(self.nodes)
        self.nodes[3].generate(1)
        sync_blocks(self.nodes)

    # As above, this mirrors the original bash test.
    def start_three(self):
        self.start_node(0)
        self.start_node(1)
        self.start_node(2)
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)

    def stop_three(self):
        self.stop_node(0)
        self.stop_node(1)
        self.stop_node(2)

    def erase_three(self):
        for wallet in self.wallet_files:
            os.remove(wallet)

    def run_test(self):
        # the database files of the wallets of 1/2/3 (the default wallet is the directory
        # regtest/wallet.dat with the file wallet.dat in it)
        self.wallet_files = [wallet_file(self.nodes[i].datadir) for i in range(3)]

        self.log.info("Generating initial blockchain")
        self.nodes[0].generate(1)
        sync_blocks(self.nodes)
        # (10 PLB a block: 5 blocks each, enough for the 10 rounds of sends below)
        self.nodes[1].generate(5)
        sync_blocks(self.nodes)
        self.nodes[2].generate(5)
        sync_blocks(self.nodes)
        self.nodes[3].generate(COINBASE_MATURITY)
        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), 1000000000)
        assert_equal(self.nodes[1].getbalance(), 50)
        assert_equal(self.nodes[2].getbalance(), 50)
        assert_equal(self.nodes[3].getbalance(), 0)

        self.log.info("Creating transactions")
        # Five rounds of sending each other transactions.
        for _ in range(5):
            self.do_one_round()

        self.log.info("Backing up")
        tmpdir = self.options.tmpdir
        self.nodes[0].backupwallet(tmpdir + "/node0/wallet.bak")
        self.nodes[0].dumpwallet(tmpdir + "/node0/wallet.dump")
        self.nodes[1].backupwallet(tmpdir + "/node1/wallet.bak")
        self.nodes[1].dumpwallet(tmpdir + "/node1/wallet.dump")
        self.nodes[2].backupwallet(tmpdir + "/node2/wallet.bak")
        self.nodes[2].dumpwallet(tmpdir + "/node2/wallet.dump")

        self.log.info("More transactions")
        for _ in range(5):
            self.do_one_round()

        # Generate 11 more blocks, so any fees paid mature
        self.nodes[3].generate(COINBASE_MATURITY + 1)
        self.sync_all()

        balance0 = self.nodes[0].getbalance()
        balance1 = self.nodes[1].getbalance()
        balance2 = self.nodes[2].getbalance()
        balance3 = self.nodes[3].getbalance()
        total = balance0 + balance1 + balance2 + balance3

        # At this point, there are 42 blocks (21 for setup, then 10 rounds, then 11).
        # 32 are mature, so the sum of all wallets is the 1,000,000,000 of block 1
        # and 10 for each of the other 31.
        height = self.nodes[3].getblockcount()
        assert_equal(height, 42)
        assert_equal(total, 1000000000 + 10 * (height - COINBASE_MATURITY - 1))

        ##
        # Test restoring spender wallets from backups
        ##
        self.log.info("Restoring using wallet.dat")
        self.stop_three()
        self.erase_three()

        # Start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        # Restore wallets from backup
        shutil.copyfile(tmpdir + "/node0/wallet.bak", self.wallet_files[0])
        shutil.copyfile(tmpdir + "/node1/wallet.bak", self.wallet_files[1])
        shutil.copyfile(tmpdir + "/node2/wallet.bak", self.wallet_files[2])

        self.log.info("Re-starting nodes")
        self.start_three()
        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        self.log.info("Restoring using dumped wallet")
        self.stop_three()
        self.erase_three()

        #start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        self.start_three()

        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        self.nodes[0].importwallet(tmpdir + "/node0/wallet.dump")
        self.nodes[1].importwallet(tmpdir + "/node1/wallet.dump")
        self.nodes[2].importwallet(tmpdir + "/node2/wallet.dump")

        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)


if __name__ == '__main__':
    WalletBackupTest().main()
