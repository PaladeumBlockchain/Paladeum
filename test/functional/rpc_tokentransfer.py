#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test transferring tokens rpc calls"""

from test_framework.test_framework import YonaTestFramework
from test_framework.util import connect_all_nodes_bi, assert_equal, assert_raises_rpc_error

class TokenTransferTest(YonaTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def setup_network(self):
        self.add_nodes(2, [
            # Nodes 0/1 are "wallet" nodes
            ["-tokenindex"],
            ["-tokenindex"]])

        self.start_nodes()

        connect_all_nodes_bi(self.nodes)

        self.sync_all()

    def run_test(self):
        self.log.info("Mining blocks...")
        self.nodes[0].generate(250)
        self.sync_all()
        self.nodes[1].generate(250)
        self.sync_all()

        chain_height_0 = self.nodes[0].getblockcount()
        chain_height_1 = self.nodes[1].getblockcount()
        assert_equal(chain_height_0, 500)
        assert_equal(chain_height_1, 500)

        n0, n1 = self.nodes[0], self.nodes[1]

        self.log.info("Calling issue()...")
        address0 = n0.getnewaddress()
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        n0.issue(token_name="TRANSFER_TEST", qty=1000, to_address=address0, change_address="", units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        n0.generate(1)

        self.sync_all()

        self.log.info("Testing transfer with dedicated token change address...")

        n1_address = n1.getnewaddress()

        n0_yona_change = n0.getnewaddress()
        n0_token_change = n0.getnewaddress()

        n0.transfer(token_name="TRANSFER_TEST", qty=200, to_address=n1_address, message='', expire_time=0, change_address=n0_yona_change, token_change_address=n0_token_change)

        n0.generate(1)
        self.sync_all()

        assert_equal(n0.listtokenbalancesbyaddress(n1_address)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n0_token_change)["TRANSFER_TEST"], 800)

        self.log.info("Testing transferfromaddress with dedicated token change address...")

        n0_from_address = n0_token_change
        n1_already_received_address = n1_address

        n1_address = n1.getnewaddress()
        n0_yona_change = n0.getnewaddress()
        n0_token_change = n0.getnewaddress()

        n0.transferfromaddress(token_name="TRANSFER_TEST", from_address=n0_from_address, qty=200, to_address=n1_address, message='', expire_time=0, yona_change_address=n0_yona_change, token_change_address=n0_token_change)

        n0.generate(1)
        self.sync_all()

        assert_equal(n0.listtokenbalancesbyaddress(n1_already_received_address)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n1_address)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n0_token_change)["TRANSFER_TEST"], 600)

        self.log.info("Testing transferfromaddresses with dedicated token change address...")

        # transfer some tokens into another address node0 controls
        n0_new_address = n0.getnewaddress()
        n0.transfer(token_name="TRANSFER_TEST", qty=200, to_address=n0_new_address, message='', expire_time=0, change_address=n0_yona_change, token_change_address=n0_token_change)

        n0.generate(1)
        self.sync_all()

        n0_from_addresses = [n0_token_change, n0_new_address]

        n1_already_received_address_2 = n1_address

        n1_address = n1.getnewaddress()
        n0_yona_change = n0.getnewaddress()
        n0_token_change = n0.getnewaddress()

        n0.transferfromaddresses(token_name="TRANSFER_TEST", from_addresses=n0_from_addresses, qty=450, to_address=n1_address, message='', expire_time=0, yona_change_address=n0_yona_change, token_change_address=n0_token_change)

        n0.generate(1)
        self.sync_all()

        assert_equal(n0.listtokenbalancesbyaddress(n1_already_received_address)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n1_already_received_address_2)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n1_address)["TRANSFER_TEST"], 450)
        assert_equal(n0.listtokenbalancesbyaddress(n0_token_change)["TRANSFER_TEST"], 150)

        self.log.info("Testing transferfromaddresses with not enough funds...")

        # Add the address the only contain 150 TRANSFER_TEST tokens
        n0_from_addresses = [n0_token_change]

        assert_raises_rpc_error(-25, "Insufficient token funds", n0.transferfromaddresses, "TRANSFER_TEST", n0_from_addresses, 450, n1_address, '', 0, n0_yona_change, n0_token_change)

        # Verify that the failed transaction doesn't change the already mined address values on the wallet
        assert_equal(n0.listtokenbalancesbyaddress(n1_already_received_address)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n1_already_received_address_2)["TRANSFER_TEST"], 200)
        assert_equal(n0.listtokenbalancesbyaddress(n1_address)["TRANSFER_TEST"], 450)
        assert_equal(n0.listtokenbalancesbyaddress(n0_token_change)["TRANSFER_TEST"], 150)



        # Verify that node 1 has the same values
        assert_equal(n1.listtokenbalancesbyaddress(n1_already_received_address)["TRANSFER_TEST"], 200)
        assert_equal(n1.listtokenbalancesbyaddress(n1_already_received_address_2)["TRANSFER_TEST"], 200)
        assert_equal(n1.listtokenbalancesbyaddress(n1_address)["TRANSFER_TEST"], 450)
        assert_equal(n1.listtokenbalancesbyaddress(n0_token_change)["TRANSFER_TEST"], 150)

        self.log.info("All Tests Passed")


if __name__ == '__main__':
    TokenTransferTest().main()
