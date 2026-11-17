#!/usr/bin/env python3
# Copyright (c) 2026 The Paladeum Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the optional arguments of sendtoaddress, sendfrom and sendmany.

 - conf_target and estimate_mode are taken from the place that the help and
   the table of the commands give them, by position and by name
 - the message is written into the transaction (a version 2 transaction), at
   most 120 bytes
 - timelock 0 makes no lock and from 256 on a lock (CHECKLOCKTIMEVERIFY);
   the values from 1 to 255 and negative values, which would make no lock,
   are refused, and the help says so
 - the help numbers the arguments in the order of the table of the commands;
   replaceable of sendtoaddress and sendmany does nothing (there is no
   replace-by-fee) but keeps its place, so that conf_target and estimate_mode
   stay where the 0.1.7 code read them
"""

from decimal import Decimal
import re

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class SendArgumentsTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def help_arguments(self, command):
        """The numbers and names of the arguments in the help of a command."""
        found = re.findall(r'^(\d+)\. +"?([a-z_]+)"?', self.nodes[0].help(command), re.M)
        return [(int(number), name) for number, name in found]

    def output_script(self, txid, amount):
        """The script (asm) of the output of a transaction that pays this amount."""
        outputs = [out for out in self.nodes[0].getrawtransaction(txid, True)["vout"] if out["value"] == amount]
        assert_equal(len(outputs), 1)
        return outputs[0]["scriptPubKey"]["asm"]

    def run_test(self):
        n0, n1 = self.nodes
        n0.generate(30)
        self.sync_all()
        to = n1.getnewaddress()

        self.log.info("sendtoaddress: conf_target and estimate_mode by position (9th and 10th)")
        # address amount timelock message comment comment_to subtractfeefromamount replaceable conf_target estimate_mode
        txid = n0.sendtoaddress(to, 1, 0, "", "", "", False, False, 6)
        txid = n0.sendtoaddress(to, 1, 0, "", "", "", False, False, 6, "ECONOMICAL")
        assert txid in n0.getrawmempool()
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendtoaddress, to, 1, 0, "", "", "", False, False, 0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendtoaddress, to, 1, 0, "", "", "", False, False, 6, "SOMETIMES")

        self.log.info("sendtoaddress: conf_target and estimate_mode by name")
        n0.sendtoaddress(address=to, amount=1, conf_target=6)
        n0.sendtoaddress(address=to, amount=1, estimate_mode="CONSERVATIVE")
        n0.sendtoaddress(address=to, amount=1, conf_target=2, estimate_mode="ECONOMICAL")
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendtoaddress, address=to, amount=1, conf_target=0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendtoaddress, address=to, amount=1, estimate_mode="SOMETIMES")

        self.log.info("sendmany: conf_target and estimate_mode by position (8th and 9th) and by name")
        # fromaccount amounts message minconf comment subtractfeefrom replaceable conf_target estimate_mode
        n0.sendmany("", {to: 1}, "", 1, "", [], False, 6)
        n0.sendmany("", {to: 1}, "", 1, "", [], False, 6, "ECONOMICAL")
        n0.sendmany(fromaccount="", amounts={to: 1}, conf_target=6, estimate_mode="CONSERVATIVE")
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendmany, "", {to: 1}, "", 1, "", [], False, 0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendmany, fromaccount="", amounts={to: 1}, estimate_mode="SOMETIMES")

        self.log.info("The message is written into the transaction")
        txid = n0.sendtoaddress(to, 1, 0, "for the coffee")
        tx = n0.getrawtransaction(txid, True)
        assert_equal(tx["version"], 2)
        assert_equal(tx["message"], "for the coffee")
        self.sync_all()
        assert_equal(n1.getrawtransaction(txid, True)["message"], "for the coffee")  # it travels with the transaction
        txid = n0.sendmany("", {to: 1}, "paid together")
        assert_equal(n0.getrawtransaction(txid, True)["message"], "paid together")
        # at most 120 bytes: 40 Korean letters take 120 bytes
        n0.sendtoaddress(to, 1, 0, "a" * 120)
        n0.sendtoaddress(to, 1, 0, "\uac00" * 40)
        assert_raises_rpc_error(-3, "Transaction message max length is 120", n0.sendtoaddress, to, 1, 0, "a" * 121)
        assert_raises_rpc_error(-3, "Transaction message max length is 120", n0.sendtoaddress, to, 1, 0, "\uac00" * 41)
        # without a message the transaction keeps version 1
        txid = n0.sendtoaddress(to, 1)
        assert_equal(n0.getrawtransaction(txid, True)["version"], 1)

        self.log.info("timelock: 0 makes no lock; 1 to 255 and negative values are refused")
        n0.generate(1)
        mempool = n0.getrawmempool()
        for lock in (1, 16, 100, 255, -1, -500000000):
            message = "Invalid timelock %d: use 0 for no lock, or a block height or a time of at least 256" % lock
            assert_raises_rpc_error(-8, message, n0.sendtoaddress, to, 1, lock)
            assert_raises_rpc_error(-8, message, n0.sendtoaddress, address=to, amount=1, timelock=lock)
            assert_raises_rpc_error(-8, message, n0.sendfrom, "", to, 1, lock)
            assert_raises_rpc_error(-8, message, n0.sendfrom, fromaccount="", toaddress=to, amount=1, timelock=lock)
        assert_equal(n0.getrawmempool(), mempool)  # nothing was sent
        for lock_time in (0, None):
            txid = n0.sendtoaddress(to, Decimal("1.1"), lock_time) if lock_time is not None else n0.sendtoaddress(to, Decimal("1.1"))
            assert self.output_script(txid, Decimal("1.1")).startswith("OP_DUP OP_HASH160 ")
            txid = n0.sendfrom("", to, Decimal("1.2"), lock_time) if lock_time is not None else n0.sendfrom("", to, Decimal("1.2"))
            assert self.output_script(txid, Decimal("1.2")).startswith("OP_DUP OP_HASH160 ")

        self.log.info("timelock: from 256 on, a block height or a time, the output is locked")
        for lock in (256, 1000000, 1900000000):
            txid = n0.sendtoaddress(to, Decimal("1.3"), lock)
            assert self.output_script(txid, Decimal("1.3")).startswith("%d OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 " % lock)
            txid = n0.sendfrom("", to, Decimal("1.4"), lock)
            assert self.output_script(txid, Decimal("1.4")).startswith("%d OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 " % lock)
            n0.generate(1)
        for command in ("sendtoaddress", "sendfrom", "sendfromaddress"):
            assert "Values from 1 to 255 and negative values are refused" in n0.help(command)

        self.log.info("The help numbers the arguments in the order of the table")
        assert_equal(self.help_arguments("sendtoaddress"), list(enumerate(
            ["address", "amount", "timelock", "message", "comment", "comment_to", "subtractfeefromamount", "replaceable", "conf_target", "estimate_mode"], 1)))
        assert_equal(self.help_arguments("sendmany"), list(enumerate(
            ["fromaccount", "amounts", "message", "minconf", "comment", "subtractfeefrom", "replaceable", "conf_target", "estimate_mode"], 1)))
        assert_equal(self.help_arguments("sendfrom"), list(enumerate(
            ["fromaccount", "toaddress", "amount", "timelock", "minconf", "comment", "comment_to"], 1)))
        assert_equal(self.help_arguments("sendfromaddress"), list(enumerate(
            ["from_address", "to_address", "amount", "timelock", "comment", "comment_to", "subtractfeefromamount", "conf_target", "estimate_mode"], 1)))
        # the usage line names the arguments in the same order
        for command in ("sendtoaddress", "sendmany"):
            usage = n0.help(command).split("\n")[0]
            assert " replaceable conf_target " in usage, usage
        assert "replaceable" not in n0.help("sendfromaddress").split("\n")[0]
        assert n0.help("sendtoaddress").startswith('sendtoaddress "address" amount ( timelock "message" ')
        assert n0.help("sendfrom").startswith('sendfrom "fromaccount" "toaddress" amount ( timelock minconf ')
        n0.generate(1)


if __name__ == '__main__':
    SendArgumentsTest().main()
