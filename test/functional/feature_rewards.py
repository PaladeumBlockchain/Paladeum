#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing rewards use cases"""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, assert_contains, Decimal

# noinspection PyAttributeOutsideInit
class RewardsTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [["-tokenindex", "-debug=rewards"], ["-tokenindex", "-minrewardheight=15"], ["-tokenindex"],
                           ["-tokenindex"]]

    def activate_tokens(self):
        self.log.info("Generating PLD for node[0] and activating tokens...")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()["bip9_softforks"]["tokens"]["status"])

    # Basic functionality test - PLD reward
    # - create the main owner address
    # - mine blocks to have enough PLD for the reward payments, plus purchasing the token
    # - issue the STOCK1 token to the owner
    # - create 5 shareholder addresses
    # - distribute different amounts of the STOCK1 token to each of the shareholder addresses
    # - mine some blocks
    # - retrieve the current chain height
    # - distribute an PLD reward amongst the shareholders
    # - verify that each one receives the expected amount of reward PLD
    def basic_test_paladeum(self):
        self.log.info("Running basic PLD reward test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK1 token")
        n0.issue(token_name="STOCK1", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking listtokenbalancesbyaddress()...")
        assert_equal(n0.listtokenbalancesbyaddress(owner_addr0)["STOCK1"], 10000)

        self.log.info("Transferring all tokens to a single address for tracking")
        n0.transfer(token_name="STOCK1", qty=10000, to_address=dist_addr0)
        n0.generate(10)
        self.sync_all()
        assert_equal(n0.listtokenbalancesbyaddress(dist_addr0)["STOCK1"], 10000)

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()
        shareholder_addr3 = n1.getnewaddress()
        shareholder_addr4 = n0.getnewaddress()

        self.log.info("Distributing shares")
        n0.transfer(token_name="STOCK1", qty=200, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK1", qty=300, to_address=shareholder_addr1, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK1", qty=400, to_address=shareholder_addr2, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK1", qty=500, to_address=shareholder_addr3, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK1", qty=600, to_address=shareholder_addr4, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.generate(10)
        self.sync_all()

        self.log.info("Verifying share distribution")
        # ownerDetails = n0.listmytokens("STOCK1", True)
        # self.log.info(f"Owner: {ownerDetails}")
        # distDetails = n0.listtokenbalancesbyaddress(dist_addr0)
        # self.log.info(f"Change: {distDetails}")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["STOCK1"], 200)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK1"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK1"], 400)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr3)["STOCK1"], 500)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr4)["STOCK1"], 600)
        assert_equal(n0.listtokenbalancesbyaddress(dist_addr0)["STOCK1"], 8000)

        self.log.info("Mining blocks")
        n0.generate(200)
        self.sync_all()

        self.log.info("Providing additional funding")
        self.nodes[0].sendtoaddress(owner_addr0, 2000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Requesting snapshot of STOCK1 ownership in 100 blocks")
        n0.requestsnapshot(token_name="STOCK1", block_height=tgt_block_height)

        n0.generate(61)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK1", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK1")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK1", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK1")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        owner3 = False
        owner4 = False
        owner5 = False
        for ownerAddr in snap_shot["owners"]:
            # self.log.info(f"Found owner {ownerAddr}")
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 200)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
            elif ownerAddr["address"] == shareholder_addr3:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner3 = True
            elif ownerAddr["address"] == shareholder_addr4:
                assert_equal(ownerAddr["amount_owned"], 600)
                owner4 = True
            elif ownerAddr["address"] == dist_addr0:
                assert_equal(ownerAddr["amount_owned"], 8000)
                owner5 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)
        assert_equal(owner3, True)
        assert_equal(owner4, True)
        assert_equal(owner5, True)

        #   listtokenbalancesbyaddress only lists the most recently delivered amount
        #       for the address, which I believe is a bug, since there can only be one
        #       key in the result object with the token name.
        # self.log.info("Moving shares after snapshot")
        # n0.transfer(token_name="STOCK1", qty=100, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK1", qty=100, to_address=shareholder_addr1, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK1", qty=100, to_address=shareholder_addr2, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK1", qty=100, to_address=shareholder_addr3, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK1", qty=100, to_address=shareholder_addr4, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.generate(100)
        # self.sync_all()

        # self.log.info("Verifying share distribution after snapshot")
        # assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr0)["STOCK1"], 300)
        # assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr1)["STOCK1"], 400)
        # assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr2)["STOCK1"], 500)
        # assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr3)["STOCK1"], 600)
        # assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr4)["STOCK1"], 700)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK1", snapshot_height=tgt_block_height, distribution_token_name="PLD",
                            gross_distribution_amount=2000, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PLD holdings after payout")
        assert_equal(n0.getreceivedbyaddress(shareholder_addr0, 0), 200)
        assert_equal(n1.getreceivedbyaddress(shareholder_addr1, 0), 300)
        assert_equal(n2.getreceivedbyaddress(shareholder_addr2, 0), 400)
        assert_equal(n1.getreceivedbyaddress(shareholder_addr3, 0), 500)
        assert_equal(n0.getreceivedbyaddress(shareholder_addr4, 0), 600)

    # Basic functionality test - TOKEN reward
    # - create the main owner address
    # - mine blocks to have enough PLD for the reward fees, plus purchasing the token
    # - issue the STOCK2 token to the owner
    # - create 5 shareholder addresses
    # - issue the PAYOUT1 token to the owner
    # - distribute different amounts of the PAYOUT1 token to each of the shareholder addresses
    # - mine some blocks
    # - retrieve the current chain height
    # - distribute reward of PAYOUT1 token units amongst the shareholders
    # - verify that each one receives the expected amount of PAYOUT1
    def basic_test_token(self):
        self.log.info("Running basic TOKEN reward test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK2 token")
        n0.issue(token_name="STOCK2", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()
        shareholder_addr3 = n1.getnewaddress()
        shareholder_addr4 = n0.getnewaddress()

        self.log.info("Distributing shares")
        n0.transfer(token_name="STOCK2", qty=200, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK2", qty=300, to_address=shareholder_addr1, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK2", qty=400, to_address=shareholder_addr2, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK2", qty=500, to_address=shareholder_addr3, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.transfer(token_name="STOCK2", qty=600, to_address=shareholder_addr4, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        n0.generate(10)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["STOCK2"], 200)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK2"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK2"], 400)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr3)["STOCK2"], 500)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr4)["STOCK2"], 600)

        self.log.info("Mining blocks")
        n0.generate(200)
        self.sync_all()

        self.log.info("Issuing PAYOUT1 token")
        n0.issue(token_name="PAYOUT1", qty=10000, to_address=owner_addr0, change_address="", units=8, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Requesting snapshot of STOCK2 ownership in 100 blocks")
        n0.requestsnapshot(token_name="STOCK2", block_height=tgt_block_height)

        # Mine 60 blocks to make sure the -minrewardsheight is met
        n0.generate(61)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK2", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK2")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK2", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK2")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        owner3 = False
        owner4 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 200)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
            elif ownerAddr["address"] == shareholder_addr3:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner3 = True
            elif ownerAddr["address"] == shareholder_addr4:
                assert_equal(ownerAddr["amount_owned"], 600)
                owner4 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)
        assert_equal(owner3, True)
        assert_equal(owner4, True)

        #   listtokenbalancesbyaddress only lists the most recently delivered amount
        #       for the address, which I believe is a bug, since there can only be one
        #       key in the result object with the token name.
        # self.log.info("Moving shares after snapshot")
        # n0.transfer(token_name="STOCK2", qty=100, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK2", qty=100, to_address=shareholder_addr1, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK2", qty=100, to_address=shareholder_addr2, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK2", qty=100, to_address=shareholder_addr3, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.transfer(token_name="STOCK2", qty=100, to_address=shareholder_addr4, message="", expire_time=0, change_address="", token_change_address=dist_addr0)
        # n0.generate(100)
        # self.sync_all() 
        # self.log.info("Verifying share distribution after snapshot")
        # assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr0)["STOCK2"], 300)
        # assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr1)["STOCK2"], 400)
        # assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr2)["STOCK2"], 500)
        # assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr3)["STOCK2"], 600)
        # assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr4)["STOCK2"], 700)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK2", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT1",
                            gross_distribution_amount=2000, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT1 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT1"], 200)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT1"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT1"], 400)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr3)["PAYOUT1"], 500)
        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr4)["PAYOUT1"], 600)

    # Attempts a payout without an token snapshot
    def payout_without_snapshot(self):
        self.log.info("Running payout without snapshot test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK3 token")
        n0.issue(token_name="STOCK3", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "Snapshot request not found",
                                n0.distributereward, "STOCK3", tgt_block_height, "PLD", 2000, owner_addr0)

    # Attempts a payout for an invalid ownership token
    def payout_with_invalid_ownership_token(self):
        self.log.info("Running payout with invalid ownership token test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "The token hasn't been created: STOCK4",
                                n0.distributereward, "STOCK4", tgt_block_height, "PLD", 2000, owner_addr0)

    # Attempts a payout for an invalid payout token
    def payout_with_invalid_payout_token(self):
        self.log.info("Running payout with invalid payout token test!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK5 token")
        n0.issue(token_name="STOCK5", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 100

        self.log.info("Skipping forward so that we're beyond the expected snapshot height")
        n0.generate(161)
        self.sync_all()

        self.log.info("Initiating failing reward payout")
        assert_raises_rpc_error(-32600, "Wallet doesn't have the ownership token(!) for the distribution token",
                                n0.distributereward, "STOCK5", tgt_block_height, "PAYOUT2", 2000, owner_addr0)

    # Attempts a payout for an invalid payout token
    def payout_before_minimum_height_is_reached(self):
        self.log.info("Running payout before minimum rewards height is reached!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK6 token")
        n0.issue(token_name="STOCK6", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK6 ownership in 1 blocks")
        n0.requestsnapshot(token_name="STOCK6", block_height=tgt_block_height)

        self.log.info("Skipping forward so that we're 15 blocks ahead of the snapshot height")
        n0.generate(16)
        self.sync_all()

        self.log.info(
            "Initiating failing reward payout because we are only 15 block ahead of the snapshot instead of 60")
        assert_raises_rpc_error(-32600,
                                "For security of the rewards payout, it is recommended to wait until chain is 60 blocks ahead of the snapshot height. You can modify this by using the -minrewardsheight.",
                                n0.distributereward, "STOCK6", tgt_block_height, "PLD", 2000, owner_addr0)

    # Attempts a payout using a custom rewards height of 15, and they have low paladeum balance
    def payout_custom_height_set_with_low_funds(self):
        self.log.info("Running payout before minimum rewards height is reached with custom min height value set!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n1.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(5)
        self.sync_all()
        n1.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK7 token")
        n1.issue(token_name="STOCK7", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n1.generate(10)
        self.sync_all()

        shareholder_addr0 = n2.getnewaddress()

        n1.transfer(token_name="STOCK7", qty=200, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=owner_addr0)

        n1.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n1.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK7 ownership in 1 blocks")
        n1.requestsnapshot(token_name="STOCK7", block_height=tgt_block_height)

        self.log.info("Skipping forward so that we're 30 blocks ahead of the snapshot height")
        n1.generate(31)
        self.sync_all()

        self.log.info("Initiating reward payout should succeed because -minrewardheight=15 on node1")
        n1.distributereward("STOCK7", tgt_block_height, "PLD", 2000, owner_addr0)

        n1.generate(2)
        self.sync_all()

        assert_equal(n1.getdistributestatus("STOCK7", tgt_block_height, "PLD", 2000, owner_addr0)['Status'], 3)

        n0.sendtoaddress(n1.getnewaddress(), 3000)
        n0.generate(5)
        self.sync_all()

        n1.generate(10)
        self.sync_all()

        assert_equal(n2.getreceivedbyaddress(shareholder_addr0, 1), 2000)

    # Attempts a payout using a custom rewards height of 15, and they have low paladeum balance
    def payout_with_insufficient_token_amount(self):
        self.log.info("Running payout before minimum rewards height is reached with custom min height value set!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n1.getnewaddress()

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 2000)
        n0.generate(5)
        self.sync_all()
        n1.generate(10)
        self.sync_all()

        n1.issue(token_name="STOCK_7.1", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)

        self.log.info("Issuing LOW_TOKEN_AMOUNT token")
        n1.issue(token_name="LOW_TOKEN_AMOUNT", qty=10000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n1.generate(10)
        self.sync_all()

        # Send the majority of the tokens to node0
        token_holder_address = n0.getnewaddress()
        n1.transfer(token_name="LOW_TOKEN_AMOUNT", qty=9000, to_address=token_holder_address, message="", expire_time=0, change_address="", token_change_address=owner_addr0)
        shareholder_addr0 = n2.getnewaddress()
        n1.transfer(token_name="STOCK_7.1", qty=200, to_address=shareholder_addr0, message="", expire_time=0, change_address="", token_change_address=owner_addr0)
        n1.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n1.getblockchaininfo()["blocks"] + 1

        self.log.info("Requesting snapshot of STOCK_7.1 ownership in 1 blocks")
        n1.requestsnapshot(token_name="STOCK_7.1", block_height=tgt_block_height)

        self.log.info("Skipping forward so that we're 30 blocks ahead of the snapshot height")
        n1.generate(61)
        self.sync_all()

        self.log.info("Initiating reward payout")
        n1.distributereward("STOCK_7.1", tgt_block_height, "LOW_TOKEN_AMOUNT", 2000, owner_addr0)

        n1.generate(2)
        self.sync_all()

        assert_equal(
            n1.getdistributestatus("STOCK_7.1", tgt_block_height, "LOW_TOKEN_AMOUNT", 2000, owner_addr0)['Status'], 5)

        # node0 transfer back the tokens to node1, now the distribution transaction should get created successfully. when the next block is mined
        n0.transfer(token_name="LOW_TOKEN_AMOUNT", qty=9000, to_address=owner_addr0, message="", expire_time=0, change_address="")
        n0.generate(5)
        self.sync_all()

        n1.generate(10)
        self.sync_all()

        assert_equal(n2.listtokenbalancesbyaddress(shareholder_addr0)["LOW_TOKEN_AMOUNT"], 2000)

    def list_snapshot_requests(self):
        self.log.info("Testing listsnapshotrequests()...")
        n0, n1 = self.nodes[0], self.nodes[1]

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(n1.getnewaddress(), 1000)
        n0.generate(5)
        self.sync_all()

        srl = n1.listsnapshotrequests()
        assert_equal(0, len(srl))

        token_name1 = "LISTME"
        block_height1 = n1.getblockcount() + 100

        token_name2 = "LISTME2"
        block_height2 = n1.getblockcount() + 200

        # make sure a snapshot can't be created for a non-existent
        assert_raises_rpc_error(-8, "token does not exist", n1.requestsnapshot, token_name1, block_height1)
        n1.issue(token_name1)
        n1.issue(token_name2)
        n1.generate(1)

        n1.requestsnapshot(token_name=token_name1, block_height=block_height1)
        n1.requestsnapshot(token_name=token_name1, block_height=block_height2)
        n1.requestsnapshot(token_name=token_name2, block_height=block_height2)

        n1.generate(1)
        srl = n1.listsnapshotrequests()
        assert_equal(3, len(srl))
        assert_contains({'token_name': token_name1, 'block_height': block_height1}, srl)
        assert_contains({'token_name': token_name1, 'block_height': block_height2}, srl)
        assert_contains({'token_name': token_name2, 'block_height': block_height2}, srl)

        srl = n1.listsnapshotrequests(token_name1)
        assert_equal(2, len(srl))
        assert_contains({'token_name': token_name1, 'block_height': block_height1}, srl)
        assert_contains({'token_name': token_name1, 'block_height': block_height2}, srl)

        srl = n1.listsnapshotrequests(token_name2)
        assert_equal(1, len(srl))
        assert_contains({'token_name': token_name2, 'block_height': block_height2}, srl)

    def basic_test_token_uneven_distribution(self):
        self.log.info("Running TOKEN reward test (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK8 token")
        n0.issue(token_name="STOCK8", qty=10000, to_address=dist_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(token_name="STOCK8", from_address=dist_addr0, qty=300, to_address=shareholder_addr0,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK8", from_address=dist_addr0, qty=300, to_address=shareholder_addr1,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK8", from_address=dist_addr0, qty=300, to_address=shareholder_addr2,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)['STOCK8'], 300)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK8"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK8"], 300)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT8 token")
        n0.issue(token_name="PAYOUT8", qty=10, to_address=owner_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK8 ownership in 5 blocks")
        n0.requestsnapshot(token_name="STOCK8", block_height=tgt_block_height)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK8", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK8")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK8", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK8")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK8", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT8",
                            gross_distribution_amount=10, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #     using the node that created the address (?!)
        self.log.info("Verifying PAYOUT8 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT8"], 3)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT8"], 3)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT8"], 3)

    def basic_test_token_even_distribution(self):
        self.log.info("Running TOKEN reward test (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK9 token")
        n0.issue(token_name="STOCK9", qty=10000, to_address=dist_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(token_name="STOCK9", from_address=dist_addr0, qty=300, to_address=shareholder_addr0,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK9", from_address=dist_addr0, qty=300, to_address=shareholder_addr1,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK9", from_address=dist_addr0, qty=400, to_address=shareholder_addr2,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)['STOCK9'], 300)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK9"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK9"], 400)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT9 token")
        n0.issue(token_name="PAYOUT9", qty=10, to_address=owner_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK9 ownership in 5 blocks")
        n0.requestsnapshot(token_name="STOCK9", block_height=tgt_block_height)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK9", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK9")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK9", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK9")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 400)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK9", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT9",
                            gross_distribution_amount=10, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT9 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT9"], 3)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT9"], 3)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT9"], 4)

    def basic_test_token_round_down_uneven_distribution(self):
        self.log.info("Running TOKEN reward test with uneven distribution (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK10 token")
        n0.issue(token_name="STOCK10", qty=10000, to_address=dist_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(token_name="STOCK10", from_address=dist_addr0, qty=300, to_address=shareholder_addr0,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK10", from_address=dist_addr0, qty=300, to_address=shareholder_addr1,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK10", from_address=dist_addr0, qty=500, to_address=shareholder_addr2,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)['STOCK10'], 300)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK10"], 300)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK10"], 500)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT10 token")
        n0.issue(token_name="PAYOUT10", qty=10, to_address=owner_addr0, change_address="", units=0, reissuable=True,
                 has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK10 ownership in 5 blocks")
        n0.requestsnapshot(token_name="STOCK10", block_height=tgt_block_height)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK10", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK10")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK10", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK10")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 300)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 500)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK10", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT10",
                            gross_distribution_amount=10, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT10 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT10"], 2)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT10"], 2)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT10"], 4)

    def basic_test_token_round_down_uneven_distribution_2(self):
        self.log.info("Running TOKEN reward test with uneven distribution (units = 0)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK11 token")
        n0.issue(token_name="STOCK11", qty=10000, to_address=dist_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()
        shareholder_addr3 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(token_name="STOCK11", from_address=dist_addr0, qty=9, to_address=shareholder_addr0,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK11", from_address=dist_addr0, qty=3, to_address=shareholder_addr1,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK11", from_address=dist_addr0, qty=2, to_address=shareholder_addr2,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK11", from_address=dist_addr0, qty=1, to_address=shareholder_addr3,
                               message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)

        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)['STOCK11'], 9)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK11"], 3)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK11"], 2)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr3)["STOCK11"], 1)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT11 token")
        n0.issue(token_name="PAYOUT11", qty=10, to_address=owner_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK11 ownership in 5 blocks")
        n0.requestsnapshot(token_name="STOCK11", block_height=tgt_block_height)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK11", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK11")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK11", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK11")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 9)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 3)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 2)
                owner2 = True
            elif ownerAddr["address"] == shareholder_addr3:
                assert_equal(ownerAddr["amount_owned"], 1)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK11", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT11",
                            gross_distribution_amount=10, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT11 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT11"], 6)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT11"], 2)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT11"], 1)

    def basic_test_token_round_down_uneven_distribution_3(self):
        self.log.info("Running TOKEN reward test with uneven distribution (units = 1)!")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing STOCK12 token")
        n0.issue(token_name="STOCK12", qty=10000, to_address=dist_addr0, change_address="", units=0, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Creating shareholder addresses")
        shareholder_addr0 = n0.getnewaddress()
        shareholder_addr1 = n1.getnewaddress()
        shareholder_addr2 = n2.getnewaddress()
        shareholder_addr3 = n2.getnewaddress()

        self.log.info("Distributing shares")
        n0.transferfromaddress(token_name="STOCK12", from_address=dist_addr0, qty=9, to_address=shareholder_addr0, message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK12", from_address=dist_addr0, qty=3, to_address=shareholder_addr1, message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK12", from_address=dist_addr0, qty=2, to_address=shareholder_addr2, message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
        n0.transferfromaddress(token_name="STOCK12", from_address=dist_addr0, qty=1, to_address=shareholder_addr3,  message="", expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)

        n0.generate(150)
        self.sync_all()

        self.log.info("Verifying share distribution")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)['STOCK12'], 9)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["STOCK12"], 3)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["STOCK12"], 2)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr3)["STOCK12"], 1)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Issuing PAYOUT12 token")
        n0.issue(token_name="PAYOUT12", qty=10, to_address=owner_addr0, change_address="", units=1, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of STOCK12 ownership in 5 blocks")
        n0.requestsnapshot(token_name="STOCK12", block_height=tgt_block_height)

        # Mine 10 blocks to make sure snapshot is created
        n0.generate(10)

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="STOCK12", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "STOCK12")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(61)
        self.sync_all()

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="STOCK12", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "STOCK12")
        assert_equal(snap_shot["height"], tgt_block_height)
        owner0 = False
        owner1 = False
        owner2 = False
        for ownerAddr in snap_shot["owners"]:
            if ownerAddr["address"] == shareholder_addr0:
                assert_equal(ownerAddr["amount_owned"], 9)
                owner0 = True
            elif ownerAddr["address"] == shareholder_addr1:
                assert_equal(ownerAddr["amount_owned"], 3)
                owner1 = True
            elif ownerAddr["address"] == shareholder_addr2:
                assert_equal(ownerAddr["amount_owned"], 2)
                owner2 = True
            elif ownerAddr["address"] == shareholder_addr3:
                assert_equal(ownerAddr["amount_owned"], 1)
                owner2 = True
        assert_equal(owner0, True)
        assert_equal(owner1, True)
        assert_equal(owner2, True)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="STOCK12", snapshot_height=tgt_block_height, distribution_token_name="PAYOUT12",
                            gross_distribution_amount=10, exception_addresses=dist_addr0)
        n0.generate(10)
        self.sync_all()

        #  Inexplicably, order matters here. We need to verify the amount
        #      using the node that created the address (?!)
        self.log.info("Verifying PAYOUT12 holdings after payout")
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr0)["PAYOUT12"], 6)
        assert_equal(n1.listtokenbalancesbyaddress(shareholder_addr1)["PAYOUT12"], 2)
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr2)["PAYOUT12"], Decimal(str(1.3)))
        assert_equal(n0.listtokenbalancesbyaddress(shareholder_addr3)["PAYOUT12"], Decimal(str(0.6)))

    def test_paladeum_bulk(self):
        self.log.info("Running basic PLD reward test!")
        n0, n1, n2, n3 = self.nodes[0], self.nodes[1], self.nodes[2], self.nodes[3]

        self.log.info("Creating owner address")
        owner_addr0 = n0.getnewaddress()
        # self.log.info(f"Owner address: {owner_addr0}")

        self.log.info("Creating distributor address")
        dist_addr0 = n0.getnewaddress()
        # self.log.info(f"Distributor address: {dist_addr0}")

        self.log.info("Providing funding")
        self.nodes[0].sendtoaddress(owner_addr0, 1000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Issuing BULK1 token")
        n0.issue(token_name="BULK1", qty=100000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.issue(token_name="TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1", qty=100000, to_address=owner_addr0, change_address="", units=4, reissuable=True, has_ipfs=False)
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking listtokenbalancesbyaddress()...")
        assert_equal(n0.listtokenbalancesbyaddress(owner_addr0)["BULK1"], 100000)

        self.log.info("Transferring all tokens to a single address for tracking")
        n0.transfer(token_name="BULK1", qty=100000, to_address=dist_addr0)
        n0.generate(10)
        self.sync_all()
        assert_equal(n0.listtokenbalancesbyaddress(dist_addr0)["BULK1"], 100000)

        self.log.info("Creating shareholder addresses")
        address_list = [None] * 9999
        for i in range(0, 9999, 3):
            address_list[i] = n1.getnewaddress()
            address_list[i + 1] = n2.getnewaddress()
            address_list[i + 2] = n3.getnewaddress()

        self.log.info("Distributing shares")
        count = 0
        for address in address_list:
            n0.transferfromaddress(token_name="BULK1", from_address=dist_addr0, qty=10, to_address=address, message="",
                                   expire_time=0, paladeum_change_address="", token_change_address=dist_addr0)
            count += 1
            if count > 190:
                n0.generate(1)
                count = 0
                self.sync_all()

        n0.generate(1)
        self.sync_all()

        self.log.info("Verifying share distribution")
        for i in range(0, 9999, 3):
            assert_equal(n1.listtokenbalancesbyaddress(address_list[i])["BULK1"], 10)
            assert_equal(n2.listtokenbalancesbyaddress(address_list[i + 1])["BULK1"], 10)
            assert_equal(n3.listtokenbalancesbyaddress(address_list[i + 2])["BULK1"], 10)

        self.log.info("Mining blocks")
        n0.generate(10)
        self.sync_all()

        self.log.info("Providing additional funding")
        self.nodes[0].sendtoaddress(owner_addr0, 2000)
        n0.generate(100)
        self.sync_all()

        self.log.info("Retrieving chain height")
        tgt_block_height = n0.getblockchaininfo()["blocks"] + 5

        self.log.info("Requesting snapshot of BULK1 ownership in 100 blocks")
        n0.requestsnapshot(token_name="BULK1", block_height=tgt_block_height)

        self.log.info("Skipping forward to allow snapshot to process")
        n0.generate(66)
        self.sync_all()

        self.log.info("Retrieving snapshot request")
        snap_shot_req = n0.getsnapshotrequest(token_name="BULK1", block_height=tgt_block_height)
        assert_equal(snap_shot_req["token_name"], "BULK1")
        assert_equal(snap_shot_req["block_height"], tgt_block_height)

        self.log.info("Retrieving snapshot for ownership validation")
        snap_shot = n0.getsnapshot(token_name="BULK1", block_height=tgt_block_height)
        assert_equal(snap_shot["name"], "BULK1")
        assert_equal(snap_shot["height"], tgt_block_height)
        for ownerAddr in snap_shot["owners"]:
            assert_equal(ownerAddr["amount_owned"], 10)

        self.log.info("Initiating reward payout")
        n0.distributereward(token_name="BULK1", snapshot_height=tgt_block_height,
                            distribution_token_name="TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1", gross_distribution_amount=100000,
                            exception_addresses=dist_addr0, change_address="", dry_run=False)
        # print(result)
        n0.generate(10)
        self.sync_all()

        # 10000 / 999 = 10.01001001
        self.log.info("Checking reward payout")
        for i in range(0, 9999, 3):
            assert_equal(n1.listtokenbalancesbyaddress(address_list[i])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))
            assert_equal(n2.listtokenbalancesbyaddress(address_list[i + 1])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))
            assert_equal(n3.listtokenbalancesbyaddress(address_list[i + 2])['TTTTTTTTTTTTTTTTTTTTTTTTTTTTT1'], Decimal(str(10.0010)))

    def run_test(self):
        self.activate_tokens()
        self.basic_test_paladeum()
        self.basic_test_token()
        self.payout_without_snapshot()
        self.payout_with_invalid_ownership_token()
        self.payout_with_invalid_payout_token()
        self.payout_before_minimum_height_is_reached()
        self.list_snapshot_requests()
        self.payout_custom_height_set_with_low_funds()
        self.payout_with_insufficient_token_amount()
        self.basic_test_token_uneven_distribution()
        self.basic_test_token_even_distribution()
        self.basic_test_token_round_down_uneven_distribution()
        self.basic_test_token_round_down_uneven_distribution_2()
        self.basic_test_token_round_down_uneven_distribution_3()
        # self.test_token_bulk()


if __name__ == "__main__":
    RewardsTest().main()
