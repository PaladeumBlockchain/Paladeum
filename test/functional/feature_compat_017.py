#!/usr/bin/env python3
# Copyright (c) 2026 The Paladeum Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that this build and Paladeum Core 0.1.7 accept each other's transactions and blocks.

Node 0 runs this build, node 1 the daemon that the environment variable PLBD_PREVIOUS names
(0.1.7); without it the test is skipped. The nodes are connected; every transaction is sent by
one of them and has to reach the memory pool of the other one through the network, every block
is made by one of them and has to become the tip of the other one. After each block both nodes
have the same tip, the same hash of the UTXO set (gettxoutsetinfo hash_serialized_2) and the same
tokens, tags, restrictions, freezes and issuing costs.

 - proof-of-work blocks of both versions, and proof-of-stake blocks of both versions from
   addresses that a governance command of that version authorized
 - from each version: PLB to a P2PKH address, sendmany with a message, a version 2 transaction
   with a message, PLB to a 2-of-3 P2SH multisig address and a spend from it, sends locked to a
   height and to a time and their spends, a send to an offline staking address
 - tokens from each version: an issue with its owner token, a sub token, a unique token, a
   reissue, transfers with a message and an expiry time, with a time lock, from an address, a
   message channel and messages, a username, a qualifier, a tag added and removed, a
   restricted token, an address frozen and unfrozen for it, the token frozen and unfrozen
 - governance commands from each version: freeze and unfreeze an address (a spend of the frozen
   address is refused by both), the cost of a root token, the fee address, authorize and
   unauthorize a staking address
 - reorganizations across the versions with proof-of-work and proof-of-stake blocks
 - neither node logs a message of the other one it does not know, or punishes the other one

The governance master of regtest is the empty script in both versions (strMasterAddress is ""
and no -masteraddress is given): a governance command spends an output with an empty script,
with the script signature OP_TRUE.

The token fee address of regtest in 0.1.7 has a version byte of Bitcoin's testnet, so 0.1.7
cannot issue a token on regtest, and refuses the token issues of this build, whose regtest fee
address is a valid regtest address (main and test networks have the same fee address in both).
If the earlier release still has that address (its createrawtransaction help shows it), the
token cases are left out after showing that each side refuses the other's issue; build 0.1.7
with only the regtest strTokenFeeAddress set to the one of this build to run them.
"""

import os
import re
import time
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.governance import (
    AUTHORIZE,
    FREEZE,
    UNAUTHORIZE,
    UNFREEZE,
    FEE,
    cost_command,
    script_command,
)
from test_framework.mininode import COIN, COutPoint, CTransaction, CTxIn, CTxOut, FromHex, ToHex
from test_framework.script import CScript, OP_RETURN, OP_TRUE
from test_framework.test_framework import PaladeumTestFramework, SkipTest
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes, wait_until

SLOT = 16
IPFS_HASH = "Qma21aBaw7pZmt1nV5MZKuGGeamDiZY34UbErV949wv2pg"
FEE_ADDRESS = "aWcYoTSeLQsdEaWmijA12pUYae5D2jSd93"   # regtest token fee address of this build
COST_ROOT = 1
TX_FEE = Decimal("0.01")
NODE_ARGS = ["-staking=1", "-emergencystaking=1", "-txindex=1"]


def as_list(result):
    if isinstance(result, list):
        return result
    return [result]


class Compat017Test(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [NODE_ARGS, NODE_ARGS]

    def setup_network(self):
        self.previous = os.getenv("PLBD_PREVIOUS")
        if not self.previous:
            self.log.info("PLBD_PREVIOUS is not set: no daemon of 0.1.7 to test against, the test is skipped")
            raise SkipTest("PLBD_PREVIOUS is not set")
        self.add_nodes(self.num_nodes, self.extra_args, binary=[None, self.previous])
        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)

    def stop_nodes(self):
        # 0.1.7 can end with a segmentation fault after its shutdown has flushed everything
        # (seen with -staking=1); that is no matter of this test, and it is logged
        previous = self.nodes[1] if len(self.nodes) > 1 else None
        if previous is not None and previous.running:
            previous.stop_node()
            wait_until(lambda: previous.process.poll() is not None, timeout=60, err_msg="0.1.7 stopped")
            code = previous.process.returncode
            if code != 0:
                self.log.warning("the daemon of 0.1.7 exited with code %d at its shutdown" % code)
            previous.running, previous.process, previous.rpc_connected, previous.rpc = False, None, False, None
        super().stop_nodes()

    # ---------------------------------------------------------------- helpers

    def name(self, node):
        return "2.0" if node is self.nodes[0] else "0.1.7"

    def other(self, node):
        return self.nodes[1] if node is self.nodes[0] else self.nodes[0]

    def set_time(self, t):
        self.t = t
        for node in self.nodes:
            node.setmocktime(t)

    def record(self, what, reason, layer, txid=None):
        """Something one version refused of the other one; checked at the end."""
        entry = {"what": what, "reason": reason, "layer": layer, "txid": txid}
        self.issues.append(entry)
        self.log.warning("COMPAT ISSUE (%s): %s: %s" % (layer, what, reason))

    def note(self, text):
        self.notes.append(text)
        self.log.info("NOTE: %s" % text)

    def script_of(self, node, address):
        return node.validateaddress(address)["scriptPubKey"]

    def pubkey_of(self, node, address):
        info = node.validateaddress(address)
        if "pubkey" not in info:
            info = node.getaddressinfo(address)
        return info["pubkey"]

    def raw_hex(self, node, txid):
        return node.getrawtransaction(txid)

    def vout_of(self, node, txid, amount, address=None):
        tx = node.decoderawtransaction(self.raw_hex(node, txid))
        for out in tx["vout"]:
            if out["value"] == amount and (address is None or address in out["scriptPubKey"].get("addresses", [])):
                return out["n"]
        raise AssertionError("no output of %s for %s in %s" % (amount, address, txid))

    def state(self, node):
        info = node.gettxoutsetinfo()
        tokens = node.listtokens("*", True, 100000) if self.tokens_ok else {}
        tags = {n: sorted(node.listaddressesfortag(n)) for n in sorted(tokens) if n.startswith("#")}
        verifiers = {n: node.getverifierstring(n) for n in sorted(tokens) if n.startswith("$")}
        frozen_tokens = {n: node.checkglobalrestriction(n) for n in sorted(tokens) if n.startswith("$")}
        restrictions = {a: (sorted(node.listtagsforaddress(a)), sorted(node.listaddressrestrictions(a)))
                        for a in self.watched} if self.tokens_ok else {}
        return {
            "tip": node.getbestblockhash(),
            "height": info["height"],
            "utxo": info["hash_serialized_2"],
            "txouts": info["txouts"],
            "amount": info["total_amount"],
            "tokens": tokens,
            "tags": tags,
            "verifiers": verifiers,
            "frozen tokens": frozen_tokens,
            "restrictions": restrictions,
            "freezelist": node.freezelist(),
            "cost": node.issuanceinfo()["cost"],
        }

    def check_same(self, what):
        n0, n1 = self.nodes
        s0, s1 = self.state(n0), self.state(n1)
        assert_equal(s0["tip"], s1["tip"])
        assert_equal(s0["utxo"], s1["utxo"])
        for key in s0:
            if s0[key] != s1[key]:
                self.record(what, "%s differs: 2.0 %r, 0.1.7 %r" % (key, s0[key], s1[key]), "state")
        self.blocks_checked += 1

    def relay(self, sender, txids, what):
        """Wait until the other node has the transactions in its memory pool. Returns the txids
        it did not take, after recording why."""
        receiver = self.other(sender)
        missing = []
        for txid in txids:
            try:
                wait_until(lambda: txid in receiver.getrawmempool(), timeout=45)
                self.relayed[self.name(sender)] += 1
            except AssertionError:
                hexstr = self.raw_hex(sender, txid)
                result = receiver.testmempoolaccept([hexstr])[0]
                reason = result.get("reject-reason", "accepted by testmempoolaccept, not relayed")
                self.record("%s from %s: not in the memory pool of %s" % (what, self.name(sender), self.name(receiver)),
                           reason, "relay", txid)
                missing.append(txid)
        return missing

    def stake(self, node):
        """Advance the clock by one slot; node stakes one block on its tip. Returns its hash."""
        height = node.getblockcount()
        tip_time = node.getblockheader(node.getbestblockhash())["time"]
        t = max(self.t, tip_time) + SLOT
        t -= t % SLOT
        self.set_time(t)
        wait_until(lambda: node.getblockcount() > height, timeout=120,
                   err_msg="a proof-of-stake block of %s" % self.name(node))
        assert_equal(node.getblockcount(), height + 1)
        return node.getbestblockhash()

    def make_block(self, miner, kind):
        if kind == "pos":
            return self.stake(miner)
        return miner.generate(1)[0]

    def mine(self, miner, kind, txids, what):
        """miner makes a block of the kind (pow or pos); the other node has to take it."""
        receiver = self.other(miner)
        block = self.make_block(miner, kind)
        try:
            wait_until(lambda: receiver.getbestblockhash() == block, timeout=60)
        except AssertionError:
            reason = receiver.submitblock(miner.getblock(block, False))
            log_lines = self.log_lines(receiver, r"ERROR|InvalidChainFound|bad-|invalid", 15)
            raise AssertionError("%s: the %s block %s of %s is not the tip of %s (submitblock: %s)\n%s" %
                                 (what, kind, block, self.name(miner), self.name(receiver), reason, "\n".join(log_lines)))
        flags = self.nodes[0].getblock(block)["flags"]
        assert_equal(flags, "proof-of-stake" if kind == "pos" else "proof-of-work")
        in_block = set(miner.getblock(block)["tx"])
        for txid in txids:
            assert txid in in_block, "%s: %s is not in the block of %s" % (what, txid, self.name(miner))
        self.block_counts["%s %s" % (self.name(miner), kind)] += 1
        self.check_same(what)
        return block

    def step(self, sender, what, make, miner=None, kind=None):
        """sender makes transactions (make()), the other node has to take them into its memory
        pool, then a block with them is made and both nodes have to agree on it."""
        txids = as_list(make())
        self.log.info("%s from %s (%d transaction(s))" % (what, self.name(sender), len(txids)))
        missing = self.relay(sender, txids, what)
        if miner is None:
            who, kind = self.cycle[self.cycle_index % len(self.cycle)]
            self.cycle_index += 1
            miner = sender if who == "self" else self.other(sender)
        if missing:
            miner = sender
        block = self.mine(miner, kind or "pow", txids, what)
        if missing:
            for txid in missing:
                self.record("%s from %s" % (what, self.name(sender)),
                           "refused in the memory pool, but the block of %s with it was taken: a policy difference" % self.name(sender),
                           "policy", txid)
        return txids

    def log_lines(self, node, pattern, last=None):
        path = os.path.join(node.datadir, "regtest", "debug.log")
        with open(path, encoding="utf8", errors="replace") as f:
            lines = [l.rstrip() for l in f if re.search(pattern, l)]
        return lines[-last:] if last else lines

    # ---------------------------------------------------------------- governance

    def fund_master(self, node, count=80, value=Decimal("1")):
        """A transaction of node's wallet with count outputs of value to the empty script."""
        fee = Decimal("0.5")
        need = count * value + 2 * fee
        inputs, total = [], Decimal(0)
        for coin in sorted(node.listunspent(1), key=lambda u: u["amount"], reverse=True):
            inputs.append({"txid": coin["txid"], "vout": coin["vout"]})
            total += coin["amount"]
            if total >= need:
                break
        assert total >= need, "the wallet of %s has %s, not %s" % (self.name(node), total, need)
        raw = node.createrawtransaction(inputs, {node.getrawchangeaddress(): total - count * value - fee})
        tx = FromHex(CTransaction(), raw)
        tx.vout = [CTxOut(int(value * COIN), CScript()) for _ in range(count)] + tx.vout
        signed = node.signrawtransaction(ToHex(tx))
        assert signed["complete"], signed
        txid = node.sendrawtransaction(signed["hex"])
        self.master_coins = [(txid, i, value) for i in range(count)] + getattr(self, "master_coins", [])
        return txid

    def gov(self, node, data):
        """Send a governance command (data hex) from node: it spends an output of the master
        (the empty script) and pays the rest back to it."""
        txid, n, value = self.master_coins.pop(0)
        tx = CTransaction()
        tx.nTime = self.t
        tx.vin = [CTxIn(COutPoint(int(txid, 16), n), CScript([OP_TRUE]), 0xffffffff)]
        tx.vout = [CTxOut(0, CScript([OP_RETURN, bytes.fromhex(data)])),
                   CTxOut(int((value - TX_FEE) * COIN), CScript())]
        tx.rehash()
        return node.sendrawtransaction(ToHex(tx))

    # ---------------------------------------------------------------- spends

    def spend(self, node, txid, vout, value, locktime=0):
        """Spend one output of node's wallet (also a multisig or locked one) to a new address."""
        node.lockunspent(True, [{"txid": txid, "vout": vout}])
        raw = node.createrawtransaction([{"txid": txid, "vout": vout}], {node.getnewaddress(): value - TX_FEE}, locktime)
        signed = node.signrawtransaction(raw)
        assert signed["complete"], signed
        return node.sendrawtransaction(signed["hex"])

    def wait_height(self, node, height):
        while node.getblockcount() < height:
            self.step_block(node, "a block before a locked output can be spent")

    def wait_median_time(self, node, t):
        while node.getblockchaininfo()["mediantime"] <= t:
            self.step_block(node, "a block before a time-locked output can be spent")

    def step_block(self, node, what):
        who, kind = self.cycle[self.cycle_index % len(self.cycle)]
        self.cycle_index += 1
        miner = node if who == "self" else self.other(node)
        if kind == "pow" and not self.pos_running:
            # (no staker can stake now: a later time makes the median time go up)
            self.set_time(self.t + SLOT)
        self.mine(miner, kind, [], what)

    # ---------------------------------------------------------------- the cases

    def plb_cases(self, s, o, tag):
        """PLB transactions from s (sender) to the other node o."""
        r = {}
        self.step(s, "PLB to a P2PKH address", lambda: s.sendtoaddress(o.getnewaddress(), 10))
        self.step(s, "sendmany with a message",
                  lambda: s.sendmany("", {o.getnewaddress(): 1, o.getnewaddress(): 2}, "sendmany message %s" % tag))
        txid = self.step(s, "a version 2 transaction with a message",
                         lambda: s.sendtoaddress(o.getnewaddress(), 3, 0, "message of %s" % tag))[0]
        version = s.decoderawtransaction(self.raw_hex(s, txid))["version"]
        if version != 2:
            self.note("sendtoaddress with a message from %s made a version %d transaction" % (self.name(s), version))

        keys = [self.pubkey_of(s, s.getnewaddress()) for _ in range(3)]
        multisig = s.addmultisigaddress(2, keys)
        if isinstance(multisig, dict):
            multisig = multisig["address"]
        txid = self.step(s, "PLB to a 2-of-3 P2SH multisig address", lambda: s.sendtoaddress(multisig, 50))[0]
        vout = self.vout_of(s, txid, Decimal(50), multisig)
        s.lockunspent(False, [{"txid": txid, "vout": vout}])
        self.step(s, "a spend from the 2-of-3 P2SH multisig address", lambda: self.spend(s, txid, vout, Decimal(50)))

        height_lock = s.getblockcount() + 4
        address = s.getnewaddress()
        txid = self.step(s, "PLB locked to a height", lambda: s.sendtoaddress(address, 7, height_lock))[0]
        vout = self.vout_of(s, txid, Decimal(7))
        s.lockunspent(False, [{"txid": txid, "vout": vout}])
        r["height"] = (txid, vout, height_lock)
        script = s.decoderawtransaction(self.raw_hex(s, txid))["vout"][vout]["scriptPubKey"]
        assert "OP_CHECKLOCKTIMEVERIFY" in script["asm"], script

        time_lock = s.getblockchaininfo()["mediantime"] + 60
        address = s.getnewaddress()
        txid = self.step(s, "PLB locked to a time", lambda: s.sendtoaddress(address, 8, time_lock))[0]
        vout = self.vout_of(s, txid, Decimal(8))
        s.lockunspent(False, [{"txid": txid, "vout": vout}])
        r["time"] = (txid, vout, time_lock)

        offline = s.getofflinestakingaddress(o.getnewaddress(), s.getnewaddress())
        if isinstance(offline, dict):
            offline = offline.get("address", offline)
        self.step(s, "PLB to an offline staking address", lambda: s.sendtoaddress(offline, 20))
        return r

    def plb_lock_spends(self, s, locks):
        txid, vout, height = locks["height"]
        self.wait_height(s, height)
        self.step(s, "a spend of the output locked to a height", lambda: self.spend(s, txid, vout, Decimal(7), height))
        txid, vout, t = locks["time"]
        self.wait_median_time(s, t)
        self.step(s, "a spend of the output locked to a time", lambda: self.spend(s, txid, vout, Decimal(8), t))

    def token_cases(self, s, o, tag):
        root = tag + "TOK"
        holder = s.getnewaddress()
        expire = self.t + 30 * 24 * 3600
        self.step(s, "a token issue with its owner token",
                  lambda: s.issue(root, 1000, holder, "", 2, True, True, IPFS_HASH))
        self.step(s, "a sub token", lambda: s.issue(root + "/SUB", 10))
        self.step(s, "a unique token", lambda: s.issueunique(root, ["U1"]))
        self.step(s, "a reissue", lambda: s.reissue(root, 500, holder))
        self.step(s, "a transfer with a message and an expiry time",
                  lambda: s.transfer(root, 5, o.getnewaddress(), 0, "transfer message %s" % tag, IPFS_HASH, expire))
        lock = s.getblockcount() + 300
        self.step(s, "a transfer locked to a height", lambda: s.transfer(root, 3, o.getnewaddress(), lock))
        self.step(s, "a transfer from an address", lambda: s.transferfromaddress(root, holder, 2, o.getnewaddress()))
        self.step(s, "a message channel", lambda: s.issue(root + "~CHANNEL"))
        self.step(s, "a message on the channel", lambda: s.sendmessage(root + "~CHANNEL", IPFS_HASH, expire))
        self.step(s, "a message of the owner token", lambda: s.sendmessage(root + "!", IPFS_HASH))
        self.step(s, "a username", lambda: s.registerusername("@" + tag + "USER"))

        qualifier = "#" + tag + "QUAL"
        restricted = "$" + root
        tagged = s.getnewaddress()
        self.watched.append(tagged)
        self.step(s, "a qualifier token", lambda: s.issuequalifiertoken(qualifier))
        self.step(s, "a tag added to an address", lambda: s.addtagtoaddress(qualifier, tagged))
        self.step(s, "a restricted token", lambda: s.issuerestrictedtoken(restricted, 100, qualifier, tagged))
        self.step(s, "an address frozen for the restricted token", lambda: s.freezeaddress(restricted, tagged))
        self.step(s, "the address unfrozen", lambda: s.unfreezeaddress(restricted, tagged))
        self.step(s, "the restricted token frozen", lambda: s.freezerestrictedtoken(restricted))
        self.step(s, "the restricted token unfrozen", lambda: s.unfreezerestrictedtoken(restricted))
        self.step(s, "the tag removed from the address", lambda: s.removetagfromaddress(qualifier, tagged))
        for node in self.nodes:
            assert root in node.listtokens(root + "*")

    def governance_cases(self, s, tag):
        frozen = s.getnewaddress()
        txid = self.step(s, "PLB to an address that is then frozen", lambda: s.sendtoaddress(frozen, 5))[0]
        vout = self.vout_of(s, txid, Decimal(5), frozen)
        s.lockunspent(False, [{"txid": txid, "vout": vout}])
        self.step(s, "governance: freeze an address", lambda: self.gov(s, script_command(FREEZE, self.script_of(s, frozen))))
        for node in self.nodes:
            assert_equal(node.checkfreeze(frozen), True)
        raw = s.createrawtransaction([{"txid": txid, "vout": vout}], {s.getnewaddress(): Decimal(5) - TX_FEE})
        signed = s.signrawtransaction(raw)["hex"]
        accepted = [node.testmempoolaccept([signed])[0] for node in self.nodes]
        if accepted[0]["allowed"] != accepted[1]["allowed"]:
            self.record("a spend from an address frozen by %s" % self.name(s),
                       "2.0 %r, 0.1.7 %r" % (accepted[0], accepted[1]), "consensus")
        assert not accepted[0]["allowed"], accepted[0]
        self.step(s, "governance: unfreeze the address", lambda: self.gov(s, script_command(UNFREEZE, self.script_of(s, frozen))))
        for node in self.nodes:
            assert_equal(node.checkfreeze(frozen), False)
        s.lockunspent(True, [{"txid": txid, "vout": vout}])

        self.step(s, "governance: the cost of a root token", lambda: self.gov(s, cost_command(COST_ROOT, Decimal(7))))
        for node in self.nodes:
            assert_equal(node.issuanceinfo()["cost"]["root"], 7)
        if self.tokens_ok:
            self.step(s, "a token issue at the new cost", lambda: s.issue(tag + "COST", 1))
        self.step(s, "governance: the cost of a root token back", lambda: self.gov(s, cost_command(COST_ROOT, Decimal(10))))
        fee_to = s.getnewaddress()
        self.step(s, "governance: the fee address", lambda: self.gov(s, script_command(FEE, self.script_of(s, fee_to))))
        if self.tokens_ok:
            self.step(s, "a token issue after the fee address changed", lambda: s.issue(tag + "FEE", 1))
        self.step(s, "governance: the fee address back", lambda: self.gov(s, script_command(FEE, self.script_of(s, FEE_ADDRESS))))
        extra = s.getnewaddress()
        self.step(s, "governance: authorize an address to stake", lambda: self.gov(s, script_command(AUTHORIZE, self.script_of(s, extra))))
        self.step(s, "governance: unauthorize it", lambda: self.gov(s, script_command(UNAUTHORIZE, self.script_of(s, extra))))

    def probe_token_fee_address(self):
        """0.1.7 has a regtest token fee address that is no regtest address: show what each side
        does with the other's token issue, without letting the nodes fork."""
        n0, n1 = self.nodes
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        txid = n0.issue("PROBETOKEN", 1)[0]
        hexstr = self.raw_hex(n0, txid)
        result = n1.testmempoolaccept([hexstr])[0]
        self.note("a token issue of 2.0 on regtest, offered to 0.1.7 (testmempoolaccept): %r" % result)
        n0.clearmempool()
        n0.abandontransaction(txid)
        try:
            n1.issue("PROBETOKEN", 1)
            self.note("0.1.7 issued a token on regtest")
        except JSONRPCException as e:
            self.note("0.1.7 cannot issue a token on regtest: %s" % e.error["message"])
        # (the wallet of 0.1.7 keeps the refused transaction)
        for entry in n1.listtransactions("*", 20):
            if entry["confirmations"] == 0 and entry["txid"] not in n1.getrawmempool():
                n1.abandontransaction(entry["txid"])
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(n0.getrawmempool(), [])
        assert_equal(n1.getrawmempool(), [])

    # ---------------------------------------------------------------- reorganizations

    def reorg(self, what, short, long, short_blocks, long_blocks):
        """Disconnect, short and long make their blocks (functions that make a block each, in
        turns), connect: both end on the chain of long."""
        n0, n1 = self.nodes
        self.log.info("Reorganization: %s" % what)
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        fork = n0.getbestblockhash()
        for make in short_blocks:
            make(short)
        for make in long_blocks:
            make(long)
        assert short.getbestblockhash() != long.getbestblockhash()
        assert short.getblockcount() < long.getblockcount()
        connect_nodes_bi(self.nodes, 0, 1)
        wait_until(lambda: short.getbestblockhash() == long.getbestblockhash(), timeout=90,
                   err_msg="%s takes the longer chain of %s" % (self.name(short), self.name(long)))
        self.reorgs.append("%s: %s reorganized from height %d to the chain of %s, height %d" %
                           (what, self.name(short), n0.getblockheader(fork)["height"], self.name(long), long.getblockcount()))
        self.check_same(what)
        # the transactions of the short chain went back to its memory pool: mine them on the common chain
        pending = short.getrawmempool()
        if pending:
            self.log.info("%d transaction(s) of the disconnected blocks of %s go into a block" % (len(pending), self.name(short)))
            # A transaction that goes back to the memory pool in a reorganization is not
            # announced again (as in Bitcoin Core; the wallet sends it again later): give it to
            # the other node, which has to take it.
            for txid in pending:
                if txid not in long.getrawmempool():
                    long.sendrawtransaction(short.getrawtransaction(txid))
            self.mine(long, "pow", pending, "the transactions of the disconnected blocks, mined again")

    # ---------------------------------------------------------------- the test

    def run_test(self):
        n0, n1 = self.nodes
        self.issues, self.notes, self.reorgs, self.watched = [], [], [], []
        self.block_counts = {"%s %s" % (v, k): 0 for v in ("2.0", "0.1.7") for k in ("pow", "pos")}
        self.relayed = {"2.0": 0, "0.1.7": 0}
        self.blocks_checked = 0
        self.cycle, self.cycle_index, self.pos_running = [("self", "pow"), ("other", "pow")], 0, False
        for node in self.nodes:
            info = node.getnetworkinfo()
            self.log.info("node%d: %s, protocol %d" % (node.index, info["subversion"], info["protocolversion"]))
        self.tokens_ok = FEE_ADDRESS in n1.help("createrawtransaction")
        start = int(time.time())
        self.set_time(start - start % SLOT)

        self.log.info("Proof-of-work blocks of 0.1.7, then of 2.0")
        n1.generate(20)
        wait_until(lambda: n0.getblockcount() == 20, timeout=60)
        self.check_same("20 proof-of-work blocks of 0.1.7")
        n0.generate(230)
        wait_until(lambda: n1.getblockcount() == 250, timeout=60)
        self.check_same("230 proof-of-work blocks of 2.0")
        self.block_counts["0.1.7 pow"] += 20
        self.block_counts["2.0 pow"] += 230

        if not self.tokens_ok:
            self.probe_token_fee_address()
            self.note("the token cases are left out: the regtest token fee address of %s is not that of 2.0" % self.previous)

        self.log.info("Coins of the staking addresses of both nodes")
        stakers = {}
        for node, count in ((n0, 16), (n1, 24)):
            addresses = [node.getnewaddress() for _ in range(count)]
            amount = min(Decimal(20000), int(node.getbalance() * Decimal("0.4") / count))
            self.step(node, "sendmany to the staking addresses", lambda: node.sendmany("", {a: amount for a in addresses}, ""))
            stakers[node.index] = addresses
        for _ in range(10):
            self.step_block(n0, "blocks while the coins of the stakers mature")

        for s, tag in ((n0, "NEW"), (n1, "OLD")):
            o = self.other(s)
            self.log.info("=== Transactions and blocks of %s, taken by %s ===" % (self.name(s), self.name(o)))
            self.cycle = [("self", "pow"), ("other", "pow")]
            self.pos_running = False
            self.step(s, "outputs to the governance master (the empty script)", lambda: self.fund_master(s))
            commands = [script_command(AUTHORIZE, self.script_of(s, a)) for a in stakers[s.index]]
            if s is n1:
                commands += [script_command(UNAUTHORIZE, self.script_of(s, a)) for a in stakers[0]]
            self.step(s, "governance: authorize the staking addresses of %s%s" %
                      (self.name(s), ", unauthorize those of 2.0" if s is n1 else ""),
                      lambda: [self.gov(s, c) for c in commands], miner=s, kind="pow")
            # from here on only s stakes
            self.pos_running = True
            self.cycle = [("self", "pos"), ("self", "pow"), ("other", "pow"), ("self", "pos")]
            self.stake_and_check(s, "a proof-of-stake block of %s without transactions" % self.name(s))
            locks = self.plb_cases(s, o, tag)
            if self.tokens_ok:
                self.token_cases(s, o, tag)
            self.governance_cases(s, tag)
            self.plb_lock_spends(s, locks)

        self.log.info("=== Reorganizations across the versions ===")
        # both nodes stake from now on: 0.1.7 authorizes the stakers of 2.0 again
        self.cycle = [("self", "pow")]
        self.pos_running = False
        self.step(n1, "governance: authorize the staking addresses of 2.0 again",
                  lambda: [self.gov(n1, script_command(AUTHORIZE, self.script_of(n0, a))) for a in stakers[0]], miner=n1, kind="pow")
        self.pos_running = True

        def pow_block(node):
            node.generate(1)

        def tx_block(node):
            # a transaction of node's own, in a block of its own
            node.sendtoaddress(node.getnewaddress(), 1)
            node.generate(1)

        def both_stake(_node):
            # one slot: each node stakes a block on its own chain
            h0, h1 = n0.getblockcount(), n1.getblockcount()
            tip_time = max(n.getblockheader(n.getbestblockhash())["time"] for n in self.nodes)
            t = max(self.t, tip_time) + SLOT
            self.set_time(t - t % SLOT)
            wait_until(lambda: n0.getblockcount() == h0 + 1 and n1.getblockcount() == h1 + 1, timeout=120,
                       err_msg="both nodes stake in the slot")

        def token_block(node):
            if self.tokens_ok:
                node.issue("REORG" + ("NEW" if node is n0 else "OLD") + str(node.getblockcount()), 1)
            node.generate(1)

        self.reorg("proof of work, 2.0 replaces its blocks with those of 0.1.7", n0, n1,
                   [tx_block, token_block], [pow_block, tx_block, token_block])
        self.reorg("proof of work, 0.1.7 replaces its blocks with those of 2.0", n1, n0,
                   [tx_block, token_block], [pow_block, tx_block, pow_block, token_block])
        # (both_stake gives each node one block on its own chain: the blocks of the long side after it decide)
        self.reorg("proof of stake on both sides, 2.0 replaces its blocks with those of 0.1.7", n0, n1,
                   [both_stake], [tx_block, token_block])
        self.reorg("proof of stake on both sides, 0.1.7 replaces its blocks with those of 2.0", n1, n0,
                   [both_stake], [tx_block, pow_block, token_block])
        self.step(n0, "a last block of 2.0", lambda: [], miner=n0, kind="pow")
        self.step(n1, "a last block of 0.1.7", lambda: [], miner=n1, kind="pow")

        self.log.info("=== Messages on the wire ===")
        self.check_wire()
        self.report()

    def stake_and_check(self, node, what):
        self.mine(node, "pos", [], what)

    def check_wire(self):
        n0, n1 = self.nodes
        for node in self.nodes:
            for peer in node.getpeerinfo():
                self.log.info("%s sees %s: version %d, banscore %s" %
                              (self.name(node), peer["subver"], peer["version"], peer.get("banscore")))
                if peer.get("banscore", 0):
                    self.record("peer score", "%s gives %s a ban score of %d" % (self.name(node), peer["subver"], peer["banscore"]), "p2p")
        patterns = r"Unknown command|Misbehaving|ProcessMessages\(.*Exception|CHECKSUM ERROR|HEADER ERROR|INVALID MESSAGESTART|Oversized message|Unparseable reject"
        for node in self.nodes:
            lines = self.log_lines(node, patterns)
            for line in lines:
                self.record("the log of %s" % self.name(node), line, "p2p")
            rejects = self.log_lines(node, r"Reject |received: reject")
            for line in rejects:
                self.note("%s got a reject message: %s" % (self.name(node), line))

    def report(self):
        self.log.info("=== Summary ===")
        self.log.info("blocks taken by the other node: %s" % self.block_counts)
        self.log.info("transactions relayed into the memory pool of the other node: from 2.0 %d, from 0.1.7 %d" %
                      (self.relayed["2.0"], self.relayed["0.1.7"]))
        self.log.info("blocks after which both nodes had the same tip and UTXO hash: %d" % self.blocks_checked)
        for line in self.reorgs:
            self.log.info("reorganization: %s" % line)
        for line in self.notes:
            self.log.info("note: %s" % line)
        for entry in self.issues:
            self.log.info("ISSUE %s" % entry)
        assert self.block_counts["2.0 pos"] > 0 and self.block_counts["0.1.7 pos"] > 0
        assert_equal(self.issues, [])


if __name__ == '__main__':
    Compat017Test().main()
