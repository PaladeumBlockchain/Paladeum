#!/usr/bin/env python3
# Copyright (c) 2026 The Paladeum Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test offline staking: coins that one wallet owns and another wallet stakes.

An offline staking address is made of a staking address (of the staking node)
and a spending address (of the owner). Coins sent to it
 - are staked by the staking node, which cannot spend them
 - stay at the same address when they are staked, with nine tenths of the
   reward; the staking node gets the other tenth at its staking address
 - can be spent by the owner at any time

Only authorized scripts stake: the nodes use the master address of the tests,
which authorizes the offline staking address during the proof-of-work phase.
The wallet does not list or spend the coins of an authorized script without
-sendauthorized, which the owner uses.
"""

from decimal import Decimal

from test_framework.governance import MASTER_ARGS, authorize_stakers, fund_master, import_master_key
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, sync_blocks, wait_until

LAST_POW = 100
SLOT = 16
REWARD = 10  # the block reward of a stake (no fees in these blocks)


class OfflineStakingTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        base = MASTER_ARGS + ["-lastpowblock=%d" % LAST_POW]
        self.extra_args = [base, base + ["-staking=1", "-emergencystaking=1"], base + ["-sendauthorized=1"]]

    def run_test(self):
        # (the coins come from a third wallet: the owner's wallet spends coins at its offline staking
        # address like any other coin of its own when it pays something)
        funder, staker, owner = self.nodes
        t = owner.getblock(owner.getbestblockhash())["time"] + 600
        t -= t % SLOT
        for node in self.nodes:
            node.setmocktime(t)

        self.log.info("The owner delegates 8 coins of 1000 to the staking key of the other node")
        import_master_key(funder)
        funder.generate(30)
        fund_master(funder)
        self.sync_all()
        staking_address = staker.getnewaddress()
        spending_address = owner.getnewaddress()
        offline = owner.getofflinestakingaddress(staking_address, spending_address)
        info = owner.validateaddress(offline)
        assert_equal(info["isvalid"], True)
        assert_equal(info["stakingaddress"], staking_address)
        assert_equal(info["spendingaddress"], spending_address)
        assert_raises_rpc_error(-1, "should be different", owner.getofflinestakingaddress, staking_address, staking_address)
        txids = [funder.sendtoaddress(offline, 1000) for _ in range(8)]
        funder.generate(LAST_POW - 31)
        # the offline staking address stakes, authorized in the last proof-of-work block
        authorize_stakers(funder, [offline])
        funder.generate(1)
        sync_blocks(self.nodes)
        assert_equal(owner.getbalance(), Decimal("8000"))

        self.log.info("The staking node sees them as stakeable, not as its own")
        assert_equal(staker.getbalance(), 0)
        balances = staker.getbalances()["mine"]
        assert_equal(balances["trusted"], 0)
        assert_equal(balances["offline_staking"], Decimal("8000"))
        assert_equal(staker.getstakinginfo()["weight"], 8000 * 10 ** 8)
        assert_raises_rpc_error(-6, "Insufficient funds", staker.sendtoaddress, staker.getnewaddress(), 100)
        scan = staker.scantxoutset("start", ["staking(%s)" % staking_address])
        assert_equal(scan["total_amount"], Decimal("8000"))
        assert_equal(len(scan["unspents"]), 8)
        assert_equal(staker.scantxoutset("start", ["staking(*)"])["total_amount"], Decimal("8000"))
        assert_equal(staker.scantxoutset("start", ["staking(%s)" % spending_address])["unspents"], [])
        script = scan["unspents"][0]["scriptPubKey"]
        # a raw transaction signed by the staking node does not spend them either
        raw = staker.createrawtransaction([{"txid": scan["unspents"][0]["txid"], "vout": scan["unspents"][0]["vout"]}], {staker.getnewaddress(): 999})
        signed = staker.signrawtransactionwithwallet(raw)
        if signed["complete"]:
            assert_equal(staker.testmempoolaccept([signed["hex"]])[0]["allowed"], False)

        self.log.info("The staking node stakes them; the coins and most of the reward stay at the address")
        t = owner.getblock(owner.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        rewards = Decimal("0")
        staked = Decimal("0")
        staker_share = Decimal(REWARD) / 10
        for _ in range(3):
            t += SLOT
            height = staker.getblockcount()
            for node in self.nodes:
                node.setmocktime(t)
            wait_until(lambda: staker.getblockcount() > height, timeout=60)
            sync_blocks(self.nodes)
            block = owner.getblock(owner.getbestblockhash(), 2)
            assert "proof-of-stake" in block["flags"]
            stake = block["tx"][1]
            spent = sum(owner.getrawtransaction(vin["txid"], True)["vout"][vin["vout"]]["value"] for vin in stake["vin"])
            paid = [out for out in stake["vout"] if out["value"] > 0]
            delegated = [out for out in paid if out["scriptPubKey"]["hex"] == script]
            share = [out for out in paid if out["scriptPubKey"]["hex"] != script]
            assert delegated, stake
            # a tenth of the reward goes to the staking address of the staking node
            assert_equal(len(share), 1)
            assert_equal(share[0]["scriptPubKey"]["addresses"], [staking_address])
            assert_equal(share[0]["value"], staker_share)
            rewards += sum(out["value"] for out in delegated) - spent
            staked += sum(out["value"] for out in paid)
        assert_equal(rewards, 3 * (REWARD - staker_share))
        # the staking node counts the stakes it made as immature, the delegated coins in them
        # too (it cannot spend those), and has nothing to spend
        assert_equal(staker.getbalance(), 0)
        assert_equal(staker.getwalletinfo()["immature_balance"], staked)
        assert_equal(staker.getstakinginfo()["stakes_24h"], 3)
        assert_equal(staker.scantxoutset("start", ["staking(%s)" % staking_address])["total_amount"], 8000 + rewards)
        assert_raises_rpc_error(-6, "Insufficient funds", staker.sendtoaddress, staker.getnewaddress(), 1)

        self.log.info("A payment of the owner does not take delegated coins on its own")
        assert owner.getbalance() >= 5000  # (the coins that were staked are immature)
        assert_raises_rpc_error(-4, "Insufficient funds", owner.sendtoaddress, funder.getnewaddress(), 500)
        # with ordinary coins in the wallet a payment takes those and leaves the delegated ones
        funder.sendtoaddress(owner.getnewaddress(), 50)
        self.sync_all()
        t += SLOT
        height = staker.getblockcount()
        for node in self.nodes:
            node.setmocktime(t)
        wait_until(lambda: staker.getblockcount() > height, timeout=60)
        sync_blocks(self.nodes)
        delegated = staker.scantxoutset("start", ["staking(%s)" % staking_address])["total_amount"]
        paid = owner.sendtoaddress(funder.getnewaddress(), 20)
        for vin in owner.getrawtransaction(paid, True)["vin"]:
            prev = owner.getrawtransaction(vin["txid"], True)["vout"][vin["vout"]]
            assert prev["scriptPubKey"]["hex"] != script, "a delegated coin was spent"
        self.sync_all()
        assert_equal(staker.scantxoutset("start", ["staking(%s)" % staking_address])["total_amount"], delegated)

        self.log.info("The owner has the coins with the rewards and spends them")
        unspent = [u for u in owner.listunspent(0) if u["address"] == offline]
        assert len(unspent) >= 2, unspent
        mature = [u for u in unspent if u["txid"] in txids and u["spendable"]]
        assert len(mature) >= 2, unspent
        target = owner.getnewaddress()
        raw = owner.createrawtransaction([{"txid": u["txid"], "vout": u["vout"]} for u in mature[:2]], {target: Decimal("1999.99")})
        signed = owner.signrawtransactionwithwallet(raw)
        assert_equal(signed["complete"], True)
        assert_equal(owner.testmempoolaccept([signed["hex"]])[0]["allowed"], True)
        spend = owner.sendrawtransaction(signed["hex"])
        sync_blocks(self.nodes)
        wait_until(lambda: spend in staker.getrawmempool(), timeout=30)
        # the staking node includes it in its next block, and has two coins less to stake
        t += SLOT
        height = staker.getblockcount()
        for node in self.nodes:
            node.setmocktime(t)
        wait_until(lambda: staker.getblockcount() > height, timeout=60)
        sync_blocks(self.nodes)
        assert owner.gettransaction(spend)["confirmations"] >= 1
        left = staker.scantxoutset("start", ["staking(%s)" % staking_address])
        # (8000 and nine tenths of five rewards, less the two coins)
        assert left["total_amount"] < 6000 + 6 * REWARD, left["total_amount"]
        assert_equal(owner.scantxoutset("start", [target])["total_amount"], Decimal("1999.99"))


if __name__ == '__main__':
    OfflineStakingTest().main()
