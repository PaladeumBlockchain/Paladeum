#!/usr/bin/env python3
# Copyright (c) 2026 The Paladeum Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test -stakecombinethreshold: the size of the coins that staking leaves.

A stake gathers the coins of its address that are smaller than the threshold
until it has twice the threshold, and is paid out in two halves when it has.
 - small coins are merged, a few per stake, never more than needed
 - a large coin is halved until its parts are below twice the threshold
 - the option is validated

Only authorized scripts stake: the nodes use the master address of the tests,
which authorizes the address of the staker during the proof-of-work phase
(-sendauthorized keeps its coins in listunspent).
"""

from decimal import Decimal

from test_framework.governance import MASTER_ARGS, authorize_stakers, fund_master, import_master_key
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

LAST_POW = 100
SLOT = 16
THRESHOLD = 50
REWARD = 10  # the block reward of a stake (no fees in these blocks)


class StakeCombineTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        base = MASTER_ARGS + ["-lastpowblock=%d" % LAST_POW]
        self.base = base
        self.extra_args = [base, base + ["-sendauthorized=1", "-staking=1", "-emergencystaking=1", "-stakecombinethreshold=%d" % THRESHOLD]]

    def run_test(self):
        funder, staker = self.nodes
        t = funder.getblock(funder.getbestblockhash())["time"] + 600
        t -= t % SLOT
        for node in self.nodes:
            node.setmocktime(t)

        self.log.info("The staker gets 150 coins of 10 and one of 500 at one address")
        import_master_key(funder)
        funder.generate(30)
        fund_master(funder)
        address = staker.getnewaddress()
        for _ in range(150):
            funder.sendtoaddress(address, 10)
        funder.sendtoaddress(address, 500)
        funder.generate(LAST_POW - 31)
        # the address of the staker stakes, authorized in the last proof-of-work block
        authorize_stakers(funder, [address])
        funder.generate(1)
        sync_blocks(self.nodes)
        assert_equal(len(staker.listunspent()), 151)
        assert_equal(staker.getbalance(), Decimal("2000"))

        self.log.info("The staker stakes 12 blocks (a stake takes about ten of the small coins, and what it pays out is immature)")
        t = funder.getblock(funder.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        stakes = []
        for _ in range(12):
            t += SLOT
            height = staker.getblockcount()
            for node in self.nodes:
                node.setmocktime(t)
            wait_until(lambda: staker.getblockcount() > height, timeout=60)
            sync_blocks(self.nodes)
            block = staker.getblock(staker.getbestblockhash(), 2)
            assert "proof-of-stake" in block["flags"]
            stakes.append(block["tx"][1])

        merged = 0
        for stake in stakes:
            inputs = [funder.getrawtransaction(vin["txid"], True)["vout"][vin["vout"]]["value"] for vin in stake["vin"]]
            outputs = [out["value"] for out in stake["vout"] if out["value"] > 0]
            kernel, others = inputs[0], inputs[1:]
            # only coins below the threshold are added, and only until there is enough for two outputs
            assert all(value < THRESHOLD for value in others), inputs
            if others:
                assert kernel + sum(others[:-1]) < 2 * THRESHOLD, inputs
                merged += len(others)
            total = sum(inputs)
            reward = sum(outputs) - total
            assert Decimal("0") < reward <= REWARD + Decimal("0.1"), (inputs, outputs)
            if sum(outputs) >= 2 * THRESHOLD:
                # paid out in two halves
                assert_equal(len(outputs), 2)
                assert abs(outputs[0] - outputs[1]) < Decimal("0.02"), outputs
            else:
                assert_equal(len(outputs), 1)
        assert merged >= 60, merged

        self.log.info("The coins end up between one and two times the threshold")
        # (immature stakes included)
        coins = [u["amount"] for u in staker.listunspent(0)] + [out["value"] for stake in stakes[-60:] for out in stake["vout"] if out["value"] > 0]
        spent = set((vin["txid"], vin["vout"]) for stake in stakes for vin in stake["vin"])
        staked_outputs = [out["value"] for stake in stakes for out in stake["vout"] if out["value"] > 0 and (stake["txid"], out["n"]) not in spent]
        small_left = [u for u in staker.listunspent(0) if u["amount"] == Decimal("10")]
        assert len(small_left) <= 150 - 60, len(small_left)
        # nothing that a stake produced from small coins is as large as twice the threshold plus one
        # coin and the reward; the parts of the 500 coin are larger (halved to 255, then to 132.5)
        for value in staked_outputs:
            assert value < 2 * THRESHOLD + 10 + REWARD or value >= (500 + 3 * REWARD) / 4, staked_outputs
        assert any(Decimal("250") < value < Decimal("260") for value in staked_outputs) or any(u["amount"] == Decimal("500") for u in staker.listunspent(0)), staked_outputs

        self.log.info("The option is validated")
        self.stop_node(1)
        self.assert_start_raises_init_error(1, self.base + ["-stakecombinethreshold=0.5"], "Invalid amount for -stakecombinethreshold")
        self.assert_start_raises_init_error(1, self.base + ["-stakecombinethreshold=abc"], "Invalid amount for -stakecombinethreshold")
        self.start_node(1, self.base + ["-mocktime=%d" % t])
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)


if __name__ == '__main__':
    StakeCombineTest().main()
