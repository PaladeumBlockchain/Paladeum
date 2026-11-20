#!/usr/bin/env python3
# Copyright (c) 2026 The Paladeum Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Governance commands of the master address, and the authorization of stakers.

A governance command is a data output of a transaction that spends a coin of the master
address: 'G' 'A' <action> <argument>. It takes effect when its block is connected.

Paladeum accepts a proof-of-stake block only if the script of its coinstake output is
authorized (CGovernance::CanStake; a pay-to-pubkey script counts as its pay-to-pubkey-hash
script), and the staking thread only stakes coins of authorized scripts. On a fresh regtest
chain the only authorized script is the address of init_authorized in CRegTestParams, whose
key the tests do not have. A test that stakes therefore starts all its nodes with MASTER_ARGS
(-masteraddress, regtest only), imports MASTER_PRIVKEY into a wallet (import_master_key),
pays coins to MASTER_ADDRESS (fund_master) and authorizes the addresses that stake with
authorize_stakers() while blocks are still proof of work. The wallet does not spend or list
the coins of an authorized script unless the node runs with -sendauthorized.
"""

from decimal import Decimal
import struct

# A key of the tests (regtest only, it holds nothing anywhere): the master address of the tests
MASTER_ADDRESS = "ag9XtMpySCHs2EBMAa22WbEufwSrss6LQp"
MASTER_PRIVKEY = "cMe8srzi9VKXJyHsDrFp9EQzSPKRFuFb9Ssas8emVjxXikGR5UMd"
MASTER_ARGS = ["-masteraddress=" + MASTER_ADDRESS]

# Actions (src/governance/governance.h)
FREEZE = 70
UNFREEZE = 85
COST = 67
FEE = 102
AUTHORIZE = 97
UNAUTHORIZE = 117


def command(action, payload):
    """The data (hex) of a governance output: the marker 'G' 'A', the action, its argument."""
    return (bytes([71, 65, action]) + payload).hex()


def script_command(action, script_hex):
    """A command whose argument is a script: freeze, unfreeze, fee address, (un)authorize."""
    script = bytes.fromhex(script_hex)
    return command(action, bytes([len(script)]) + script)


def cost_command(cost_type, amount):
    """A command that sets the cost of issuing a type of token (GOVERNANCE_COST_*)."""
    return command(COST, bytes([cost_type]) + struct.pack("<q", int(amount * 100000000)))


def import_master_key(node, rescan=False):
    """Import the key of MASTER_ADDRESS into the wallet of node."""
    node.importprivkey(MASTER_PRIVKEY, "master", rescan)


def lock_master_coins(node, txid, master_address=MASTER_ADDRESS):
    """Lock the outputs of transaction txid that pay to master_address, so that the other
    payments of the wallet leave them to the governance commands (locks last until a restart)."""
    tx = node.decoderawtransaction(node.gettransaction(txid)["hex"])
    outputs = [{"txid": txid, "vout": out["n"]} for out in tx["vout"]
               if out["scriptPubKey"].get("addresses") == [master_address]]
    assert outputs, "transaction %s pays nothing to %s" % (txid, master_address)
    node.lockunspent(False, outputs)
    return outputs


def fund_master(node, amount=1000, master_address=MASTER_ADDRESS):
    """Pay amount from the wallet of node to the master address and lock that coin for the
    governance commands. Returns the txid."""
    txid = node.sendtoaddress(master_address, amount)
    lock_master_coins(node, txid, master_address)
    return txid


def master_coins(node, master_address=MASTER_ADDRESS):
    """The coins of master_address (txid, vout, amount) that the wallet of node has, locked or not."""
    coins = [(u["txid"], u["vout"], u["amount"]) for u in node.listunspent(0, 9999999, [master_address])]
    for locked in node.listlockunspent():
        out = node.gettxout(locked["txid"], locked["vout"], True)
        if out is not None and out["scriptPubKey"].get("addresses") == [master_address]:
            coins.append((locked["txid"], locked["vout"], out["value"]))
    return coins


def send_commands(node, commands, master_address=MASTER_ADDRESS, fee=Decimal("0.01")):
    """Send each command (data hex) in a transaction that spends a coin of master_address
    from the wallet of node (see fund_master), each one the change of the one before, which
    stays locked. Returns the txids; the commands take effect in the block that confirms them."""
    coins = [c for c in master_coins(node, master_address) if c[2] > fee * (len(commands) + 1)]
    assert coins, "no coin of the master address %s in the wallet" % master_address
    txid, vout, amount = max(coins, key=lambda c: c[2])
    txids = []
    for data in commands:
        amount -= fee
        raw = node.createrawtransaction([{"txid": txid, "vout": vout}], {"data": data, master_address: amount})
        signed = node.signrawtransaction(raw)
        assert signed["complete"], signed
        txid = node.sendrawtransaction(signed["hex"])
        vout = lock_master_coins(node, txid, master_address)[0]["vout"]
        txids.append(txid)
    return txids


def coin_addresses(node, exclude=(MASTER_ADDRESS,)):
    """The addresses of the coins in the wallet of node (also unconfirmed ones), without exclude."""
    return sorted(set(u["address"] for u in node.listunspent(0) if u.get("address") and u["address"] not in exclude))


def authorize_stakers(node, addresses, master_address=MASTER_ADDRESS):
    """Authorize the scripts of addresses to stake. Returns the txids of the commands;
    mine a block to confirm them."""
    return send_commands(node, [script_command(AUTHORIZE, node.validateaddress(address)["scriptPubKey"])
                                for address in addresses], master_address)


def unauthorize_stakers(node, addresses, master_address=MASTER_ADDRESS):
    """Take the authorization to stake from the scripts of addresses."""
    return send_commands(node, [script_command(UNAUTHORIZE, node.validateaddress(address)["scriptPubKey"])
                                for address in addresses], master_address)
