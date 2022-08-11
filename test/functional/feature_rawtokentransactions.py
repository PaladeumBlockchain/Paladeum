#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test the rawtransaction RPCs for token transactions.
"""

import math
from io import BytesIO
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, assert_is_hash_string, assert_does_not_contain_key, assert_contains_key, assert_contains_pair
from test_framework.mininode import CTransaction, hex_str_to_bytes, bytes_to_hex_str, CScriptReissue, CScriptOwner, CScriptTransfer, CTxOut, CScriptIssue

def truncate(number, digits=8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper


# noinspection PyTypeChecker,PyUnboundLocalVariable,PyUnresolvedReferences
def get_first_unspent(self: object, node: object, needed: float = 500.1) -> object:
    # Find the first unspent with enough required for transaction
    for n in range(0, len(node.listunspent())):
        unspent = node.listunspent()[n]
        if float(unspent['amount']) > needed:
            self.log.info("Found unspent index %d with more than %s available.", n, needed)
            return unspent
    assert (float(unspent['amount']) < needed)


def get_tx_issue_hex(self, node, token_name, token_quantity, token_units=0):
    to_address = node.getnewaddress()
    change_address = node.getnewaddress()
    unspent = get_first_unspent(self, node)
    inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
    outputs = {
        'n1issueTokenXXXXXXXXXXXXXXXXWdnemQ': 500,
        change_address: truncate(float(unspent['amount']) - 500.1),
        to_address: {
            'issue': {
                'token_name':       token_name,
                'token_quantity':   token_quantity,
                'units':            token_units,
                'reissuable':       1,
                'has_ipfs':         0,
            }
        }
    }

    tx_issue = node.createrawtransaction(inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


# noinspection PyTypeChecker
class RawTokenTransactionsTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def activate_tokens(self):
        self.log.info("Generating PLB for node[0] and activating tokens...")
        n0 = self.nodes[0]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['tokens']['status'])

    def reissue_tampering_test(self):
        self.log.info("Tampering with raw reissues...")

        n0 = self.nodes[0]

        ########################################
        # issue a couple of tokens
        token_name = 'REISSUE_TAMPERING'
        owner_name = f"{token_name}!"
        alternate_token_name = 'ANOTHER_TOKEN'
        alternate_owner_name = f"{alternate_token_name}!"
        n0.sendrawtransaction(get_tx_issue_hex(self, n0, token_name, 1000))
        n0.sendrawtransaction(get_tx_issue_hex(self, n0, alternate_token_name, 1000))
        n0.generate(1)

        ########################################
        # try a reissue with no owner input
        to_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            'n1ReissueTokenXXXXXXXXXXXXXXWG9NLd': 100,
            n0.getnewaddress(): truncate(float(unspent['amount']) - 100.1),
            to_address: {
                'reissue': {
                    'token_name':       token_name,
                    'token_quantity':   1000,
                }
            }
        }
        tx_hex = n0.createrawtransaction(inputs, outputs)
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for token that you don't have: {owner_name}",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # try a reissue where the owner input doesn't match the token name
        unspent_token_owner = n0.listmytokens(alternate_owner_name, True)[alternate_owner_name]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]
        tx_hex = n0.createrawtransaction(inputs, outputs)
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for token that you don't have: {owner_name}",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # fix it to use the right input
        unspent_token_owner = n0.listmytokens(owner_name, True)[owner_name]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]
        tx_hex = n0.createrawtransaction(inputs, outputs)

        ########################################
        # try tampering to change the name of the token being issued
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumr = '72766e72'  # paladeumr
        op_drop = '75'
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumr in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                reissue_script_hex = script_hex[script_hex.index(paladeumr) + len(paladeumr):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(reissue_script_hex))
                reissue = CScriptReissue()
                reissue.deserialize(f)
                reissue.name = alternate_token_name.encode()
                tampered_reissue = bytes_to_hex_str(reissue.serialize())
                tampered_script = script_hex[:script_hex.index(paladeumr)] + paladeumr + tampered_reissue + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_hex_bad = bytes_to_hex_str(tx.serialize())
        tx_signed = n0.signrawtransaction(tx_hex_bad)['hex']
        assert_raises_rpc_error(-26, "bad-txns-reissue-owner-outpoint-not-found", n0.sendrawtransaction, tx_signed)

        ########################################
        # try tampering to remove owner output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: paladeumt not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_hex_bad = bytes_to_hex_str(tx.serialize())
        tx_signed = n0.signrawtransaction(tx_hex_bad)['hex']
        assert_raises_rpc_error(-26, "bad-txns-reissue-owner-outpoint-not-found",
                                n0.sendrawtransaction, tx_signed)

        ########################################
        # try tampering to remove token output...
        # ...this is actually ok, just an awkward donation to reissue burn address!

        ########################################
        # reissue!
        tx_signed = n0.signrawtransaction(tx_hex)['hex']
        tx_hash = n0.sendrawtransaction(tx_signed)
        assert_is_hash_string(tx_hash)
        n0.generate(1)
        assert_equal(2000, n0.listmytokens(token_name)[token_name])

    def issue_tampering_test(self):
        self.log.info("Tampering with raw issues...")

        n0 = self.nodes[0]

        ########################################
        # get issue tx
        token_name = 'TAMPER_TEST_TOKEN'
        tx_issue_hex = get_tx_issue_hex(self, n0, token_name, 1000)

        ########################################
        # try tampering to issue an token with no owner output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        paladeumo = '72766e6f'  # paladeumo
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: paladeumo not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-token-transaction",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue an token with duplicate owner outputs
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        paladeumo = '72766e6f'  # paladeumo
        # find the owner output from vout and insert a duplicate back in
        owner_vout = list(filter(lambda out_script: paladeumo in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))[0]
        tx.vout.insert(-1, owner_vout)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-token-formatting-check",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue an owner token with no token
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        paladeumq = '72766e71'  # paladeumq
        # remove the owner output from vout
        bad_vout = list(filter(lambda out_script: paladeumq not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-token-transaction",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to issue a mismatched owner/token
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        paladeumo = '72766e6f'  # paladeumo
        op_drop = '75'
        # change the owner name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumo in bytes_to_hex_str(out.scriptPubKey):
                owner_out = out
                owner_script_hex = bytes_to_hex_str(owner_out.scriptPubKey)
                token_script_hex = owner_script_hex[owner_script_hex.index(paladeumo) + len(paladeumo):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(token_script_hex))
                owner = CScriptOwner()
                owner.deserialize(f)
                owner.name = b"NOT_MY_TOKEN!"
                tampered_owner = bytes_to_hex_str(owner.serialize())
                tampered_script = owner_script_hex[:owner_script_hex.index(paladeumo)] + paladeumo + tampered_owner + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-owner-name-doesn't-match",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try tampering to make owner output script invalid
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_hex))
        tx.deserialize(f)
        paladeumo = '72766e6f'  # paladeumo
        PLBO = '52564e4f'  # PLBO
        # change the owner output script type to be invalid
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumo in bytes_to_hex_str(out.scriptPubKey):
                owner_script_hex = bytes_to_hex_str(out.scriptPubKey)
                tampered_script = owner_script_hex.replace(paladeumo, PLBO)
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_issue_signed = n0.signrawtransaction(tx_bad_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-op-paladeum-token-not-in-right-script-location",
                                n0.sendrawtransaction, tx_bad_issue_signed)

        ########################################
        # try to generate and issue an token that already exists
        tx_duplicate_issue_hex = get_tx_issue_hex(self, n0, token_name, 42)
        n0.sendrawtransaction(tx_issue_hex)
        n0.generate(1)
        assert_raises_rpc_error(-8, f"Invalid parameter: token_name '{token_name}' has already been used", get_tx_issue_hex, self, n0, token_name, 55)
        assert_raises_rpc_error(-25, f"Missing inputs", n0.sendrawtransaction, tx_duplicate_issue_hex)

    def issue_reissue_transfer_test(self):
        self.log.info("Doing a big issue-reissue-transfer test...")
        n0, n1 = self.nodes[0], self.nodes[1]

        ########################################
        # issue
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
        outputs = {
            'n1issueTokenXXXXXXXXXXXXXXXXWdnemQ': 500,
            change_address: truncate(float(unspent['amount']) - 500.1),
            to_address: {
                'issue': {
                    'token_name':       'TEST_TOKEN',
                    'token_quantity':   1000,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         0,
                }
            }
        }
        tx_issue = n0.createrawtransaction(inputs, outputs)
        tx_issue_signed = n0.signrawtransaction(tx_issue)
        tx_issue_hash = n0.sendrawtransaction(tx_issue_signed['hex'])
        assert_is_hash_string(tx_issue_hash)
        self.log.info("issue tx: " + tx_issue_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(1000, n0.listmytokens('TEST_TOKEN')['TEST_TOKEN'])
        assert_equal(1, n0.listmytokens('TEST_TOKEN!')['TEST_TOKEN!'])

        ########################################
        # reissue
        unspent = get_first_unspent(self, n0)
        unspent_token_owner = n0.listmytokens('TEST_TOKEN!', True)['TEST_TOKEN!']['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]

        outputs = {
            'n1ReissueTokenXXXXXXXXXXXXXXWG9NLd': 100,
            change_address: truncate(float(unspent['amount']) - 100.1),
            to_address: {
                'reissue': {
                    'token_name':       'TEST_TOKEN',
                    'token_quantity':   1000,
                }
            }
        }

        tx_reissue = n0.createrawtransaction(inputs, outputs)
        tx_reissue_signed = n0.signrawtransaction(tx_reissue)
        tx_reissue_hash = n0.sendrawtransaction(tx_reissue_signed['hex'])
        assert_is_hash_string(tx_reissue_hash)
        self.log.info("reissue tx: " + tx_reissue_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(2000, n0.listmytokens('TEST_TOKEN')['TEST_TOKEN'])
        assert_equal(1, n0.listmytokens('TEST_TOKEN!')['TEST_TOKEN!'])

        self.sync_all()

        ########################################
        # transfer
        remote_to_address = n1.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token = n0.listmytokens('TEST_TOKEN', True)['TEST_TOKEN']['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.1),
            remote_to_address: {
                'transfer': {
                    'TEST_TOKEN': 400
                }
            },
            to_address: {
                'transfer': {
                    'TEST_TOKEN': truncate(float(unspent_token['amount']) - 400, 0)
                }
            }
        }
        tx_transfer = n0.createrawtransaction(inputs, outputs)
        tx_transfer_signed = n0.signrawtransaction(tx_transfer)
        tx_hex = tx_transfer_signed['hex']

        ########################################
        # try tampering with the sig
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        script_sig = bytes_to_hex_str(tx.vin[1].scriptSig)
        tampered_sig = script_sig[:-8] + "deadbeef"
        tx.vin[1].scriptSig = hex_str_to_bytes(tampered_sig)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Script failed an OP_EQUALVERIFY operation)",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering with the token script
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'
        # change token outputs from 400,600 to 500,500
        for i in range(1, 3):
            script_hex = bytes_to_hex_str(tx.vout[i].scriptPubKey)
            f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(paladeumt) + len(paladeumt):-len(op_drop)]))
            transfer = CScriptTransfer()
            transfer.deserialize(f)
            transfer.amount = 50000000000
            tampered_transfer = bytes_to_hex_str(transfer.serialize())
            tampered_script = script_hex[:script_hex.index(paladeumt)] + paladeumt + tampered_transfer + op_drop
            tx.vout[i].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)", n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering with token outs so ins and outs don't add up
        for n in (-20, -2, -1, 1, 2, 20):
            bad_outputs = {
                change_address: truncate(float(unspent['amount']) - 0.0001),
                remote_to_address: {
                    'transfer': {
                        'TEST_TOKEN': 400
                    }
                },
                to_address: {
                    'transfer': {
                        'TEST_TOKEN': float(unspent_token['amount']) - (400 + n)
                    }
                }
            }
            tx_bad_transfer = n0.createrawtransaction(inputs, bad_outputs)
            tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)
            tx_bad_hex = tx_bad_transfer_signed['hex']
            assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - Tokens would be burnt TEST_TOKEN", n0.sendrawtransaction, tx_bad_hex)

        ########################################
        # try tampering with token outs so they don't use proper units
        for n in (-0.1, -0.00000001, 0.1, 0.00000001):
            bad_outputs = {
                change_address: truncate(float(unspent['amount']) - 0.0001),
                remote_to_address: {
                    'transfer': {
                        'TEST_TOKEN': (400 + n)
                    }
                },
                to_address: {
                    'transfer': {
                        'TEST_TOKEN': float(unspent_token['amount']) - (400 + n)
                    }
                }
            }
            tx_bad_transfer = n0.createrawtransaction(inputs, bad_outputs)
            tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)
            tx_bad_hex = tx_bad_transfer_signed['hex']
            assert_raises_rpc_error(-26, "bad-txns-transfer-token-amount-not-match-units",
                                    n0.sendrawtransaction, tx_bad_hex)

        ########################################
        # try tampering to change the output token name to one that doesn't exist
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'
        # change token name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumt in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(paladeumt) + len(paladeumt):-len(op_drop)]))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.name = b"TOKEN_DOES_NOT_EXIST"
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = script_hex[:script_hex.index(paladeumt)] + paladeumt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-txns-transfer-token-not-exist",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering to change the output token name to one that exists
        alternate_token_name = "ALTERNATE"
        n0.issue(alternate_token_name)
        n0.generate(1)
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'
        # change token name
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumt in bytes_to_hex_str(out.scriptPubKey):
                script_hex = bytes_to_hex_str(out.scriptPubKey)
                f = BytesIO(hex_str_to_bytes(script_hex[script_hex.index(paladeumt) + len(paladeumt):-len(op_drop)]))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.name = alternate_token_name.encode()
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = script_hex[:script_hex.index(paladeumt)] + paladeumt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - " +
                                f"Trying to create outpoint for token that you don't have: {alternate_token_name}",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # try tampering to remove the token output
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        # remove the transfer output from vout
        bad_vout = list(filter(lambda out_script: paladeumt not in bytes_to_hex_str(out_script.scriptPubKey), tx.vout))
        tx.vout = bad_vout
        tampered_hex = bytes_to_hex_str(tx.serialize())
        assert_raises_rpc_error(-26, "bad-tx-token-inputs-size-does-not-match-outputs-size",
                                n0.sendrawtransaction, tampered_hex)

        ########################################
        # send the good transfer
        tx_transfer_hash = n0.sendrawtransaction(tx_hex)
        assert_is_hash_string(tx_transfer_hash)
        self.log.info("transfer tx: " + tx_transfer_hash)

        n0.generate(1)
        self.sync_all()
        assert_equal(1600, n0.listmytokens('TEST_TOKEN')['TEST_TOKEN'])
        assert_equal(1, n0.listmytokens('TEST_TOKEN!')['TEST_TOKEN!'])
        assert_equal(400, n1.listmytokens('TEST_TOKEN')['TEST_TOKEN'])

    def unique_tokens_test(self):
        self.log.info("Testing unique tokens...")
        n0 = self.nodes[0]

        bad_burn = "n1BurnXXXXXXXXXXXXXXXXXXXXXXU1qejP"
        unique_burn = "n1issueUniqueTokenXXXXXXXXXXS4695i"

        root = "RINGU"
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(10)

        token_tags = ["myprecious1", "bind3", "gold7", "men9"]
        ipfs_hashes = ["QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"] * len(token_tags)

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token_owner = n0.listmytokens(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 5 * len(token_tags)

        ############################################
        # try first with bad burn address
        outputs = {
            bad_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue_unique': {
                    'root_name':    root,
                    'token_tags':   token_tags,
                    'ipfs_hashes':  ipfs_hashes,
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-unique-token-burn-outpoints-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to proper burn address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue_unique': {
                    'root_name':    root,
                    'token_tags':   token_tags,
                    'ipfs_hashes':  ipfs_hashes,
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        for tag in token_tags:
            token_name = f"{root}#{tag}"
            assert_equal(1, n0.listmytokens()[token_name])
            assert_equal(1, n0.listtokens(token_name, True)[token_name]['has_ipfs'])
            assert_equal(ipfs_hashes[0], n0.listtokens(token_name, True)[token_name]['ipfs_hash'])

    def unique_tokens_via_issue_test(self):
        self.log.info("Testing unique tokens via issue...")
        n0 = self.nodes[0]

        root = "RINGU2"
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(1)

        token_name = f"{root}#unique"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token_owner = n0.listmytokens(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 5
        outputs = {
            'n1issueUniqueTokenXXXXXXXXXXS4695i': burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   100000000,
                    'units':            0,
                    'reissuable':       0,
                    'has_ipfs':         0,
                }
            },
        }

        ########################################
        # bad qty
        for n in (2, 20, 20000):
            outputs[to_address]['issue']['token_quantity'] = n
            assert_raises_rpc_error(-8, "Invalid parameter: amount must be 100000000", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['token_quantity'] = 1

        ########################################
        # bad units
        for n in (1, 2, 3, 4, 5, 6, 7, 8):
            outputs[to_address]['issue']['units'] = n
            assert_raises_rpc_error(-8, "Invalid parameter: units must be 0", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['units'] = 0

        ########################################
        # reissuable
        outputs[to_address]['issue']['reissuable'] = 1
        assert_raises_rpc_error(-8, "Invalid parameter: reissuable must be 0", n0.createrawtransaction, inputs, outputs)
        outputs[to_address]['issue']['reissuable'] = 0

        ########################################
        # ok
        hex_data = n0.signrawtransaction(n0.createrawtransaction(inputs, outputs))['hex']
        n0.sendrawtransaction(hex_data)
        n0.generate(1)
        assert_equal(1, n0.listmytokens()[root])
        assert_equal(1, n0.listmytokens()[token_name])
        assert_equal(1, n0.listmytokens()[owner])

    def bad_ipfs_hash_test(self):
        self.log.info("Testing bad ipfs_hash...")
        n0 = self.nodes[0]

        token_name = 'SOME_OTHER_TOKEN_3'
        owner = f"{token_name}!"
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        bad_hash = "RncvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"

        ########################################
        # issue
        inputs = [{k: unspent[k] for k in ['txid', 'vout']}]
        outputs = {
            'n1issueTokenXXXXXXXXXXXXXXXXWdnemQ': 500,
            change_address: truncate(float(unspent['amount']) - 500.0001),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        bad_hash,
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: ipfs_hash is not valid, or txid hash is not the right length", n0.createrawtransaction, inputs, outputs)

        ########################################
        # reissue
        n0.issue(token_name=token_name, qty=1000, to_address=to_address, change_address=change_address, units=0, reissuable=True, has_ipfs=False)
        n0.generate(1)
        unspent_token_owner = n0.listmytokens(owner, True)[owner]['outpoints'][0]
        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            'n1ReissueTokenXXXXXXXXXXXXXXWG9NLd': 100,
            change_address: truncate(float(unspent['amount']) - 100.0001),
            to_address: {
                'reissue': {
                    'token_name':       token_name,
                    'token_quantity':   1000,
                    'ipfs_hash':        bad_hash,
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: ipfs_hash is not valid, or txid hash is not the right length", n0.createrawtransaction, inputs, outputs)

    def issue_invalid_address_test(self):
        self.log.info("Testing issue with invalid burn and address...")
        n0 = self.nodes[0]

        unique_burn = "n1issueUniqueTokenXXXXXXXXXXS4695i"
        issue_burn = "n1issueTokenXXXXXXXXXXXXXXXXWdnemQ"
        sub_burn = "n1issueSubTokenXXXXXXXXXXXXXbNiH6v"

        token_name = "BURN_TEST"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # start with invalid burn amount and valid address
        burn = 499
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to invalid burn amount again
        burn = 501
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn amount, but sending it to the sub token burn address
        burn = 500
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to unique address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn address, and valid burn amount
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

    def issue_sub_invalid_address_test(self):
        self.log.info("Testing issue sub invalid amount and address...")
        n0 = self.nodes[0]

        unique_burn = "n1issueUniqueTokenXXXXXXXXXXS4695i"
        issue_burn = "n1issueTokenXXXXXXXXXXXXXXXXWdnemQ"
        reissue_burn = "n1ReissueTokenXXXXXXXXXXXXXXWG9NLd"
        sub_burn = "n1issueSubTokenXXXXXXXXXXXXXbNiH6v"

        token_name = "ISSUE_SUB_INVALID"
        owner = f"{token_name}!"
        n0.issue(token_name)
        n0.generate(1)
        self.sync_all()

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token_owner = n0.listmytokens(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]

        burn = 99
        token_name_sub = token_name + '/SUB1'

        ############################################
        # try first with bad burn amount
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to invalid burn amount again
        burn = 101
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn amount, but sending it to the issue token burn address
        burn = 100
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to reissue address, should be invalid because it needs to be sub token burn address
        outputs = {
            reissue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch burn address to unique address, should be invalid because it needs to be sub token burn address
        outputs = {
            unique_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-burn-not-found", n0.sendrawtransaction, signed_hex)

        ############################################
        # switch to valid burn address, and valid burn amount
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

    def issue_multiple_outputs_test(self):
        self.log.info("Testing issue with extra issues in the tx...")
        n0 = self.nodes[0]

        issue_burn = "n1issueTokenXXXXXXXXXXXXXXXXWdnemQ"

        token_name = "ISSUE_MULTIPLE_TEST"
        token_name_multiple = "ISSUE_MULTIPLE_TEST_2"

        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)

        multiple_to_address = n0.getnewaddress()

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # Try tampering with an token by adding another issue
        burn = 500
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.001)),
            multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-token-formatting-check", n0.sendrawtransaction, signed_hex)

    def issue_sub_multiple_outputs_test(self):
        self.log.info("Testing issue with an extra sub token issue in the tx...")
        n0 = self.nodes[0]

        issue_burn = "n1issueTokenXXXXXXXXXXXXXXXXWdnemQ"
        sub_burn =   "n1issueSubTokenXXXXXXXXXXXXXbNiH6v"

        # create the root token that the sub token will try to be created from
        root = "ISSUE_SUB_MULTIPLE_TEST"
        token_name_multiple_sub = root + '/SUB'
        owner = f"{root}!"
        n0.issue(root)
        n0.generate(1)
        self.sync_all()

        token_name = "ISSUE_MULTIPLE_TEST"

        to_address = n0.getnewaddress()
        sub_multiple_to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        owner_change_address = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token_owner = n0.listmytokens(owner, True)[owner]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token_owner[k] for k in ['txid', 'vout']},
        ]

        ############################################
        # Try tampering with an token transaction by having a sub token issue hidden in the transaction
        burn = 500
        outputs = {
            issue_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            },
            to_address: {
                'issue': {
                    'token_name':       token_name,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-failed-issue-token-formatting-check", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub token transaction by having another owner token transfer
        burn = 100
        second_owner_change_address = n0.getnewaddress()
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            second_owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-tx-inputs-outputs-mismatch Bad Transaction - Tokens would be burnt ISSUE_SUB_MULTIPLE_TEST!", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub token transaction by not having any owner change
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-new-token-missing-owner-token", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue sub token transaction by not having any owner change
        self.log.info("Testing issue sub token and tampering with the owner change...")
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-issue-new-token-missing-owner-token", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue by changing the owner amount transferred to 2
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 2,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        hex_data = n0.createrawtransaction(inputs, outputs)
        signed_hex = n0.signrawtransaction(hex_data)['hex']
        assert_raises_rpc_error(-26, "bad-txns-transfer-owner-amount-was-not-1", n0.sendrawtransaction, signed_hex)

        ############################################
        # Try tampering with an issue by changing the owner amount transferred to 0
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.0001)),
            owner_change_address: {
                'transfer': {
                    owner: 0,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }
        assert_raises_rpc_error(-8, "Invalid parameter: token amount can't be equal to or less than zero.", n0.createrawtransaction, inputs, outputs)

        # Create the valid sub token and broadcast the transaction
        outputs = {
            sub_burn: burn,
            change_address: truncate(float(unspent['amount']) - (burn + 0.01)),
            owner_change_address: {
                'transfer': {
                    owner: 1,
                }
            },
            sub_multiple_to_address: {
                'issue': {
                    'token_name':       token_name_multiple_sub,
                    'token_quantity':   1,
                    'units':            0,
                    'reissuable':       1,
                    'has_ipfs':         1,
                    'ipfs_hash':        "QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
                }
            }
        }

        tx_issue_sub_hex = n0.createrawtransaction(inputs, outputs)

        ############################################
        # try tampering to issue sub token a mismatched the transfer amount to 0
        self.log.info("Testing issue sub token tamper with the owner change transfer amount...")
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_issue_sub_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'
        # change the transfer amount
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)
                token_script_hex = transfer_script_hex[transfer_script_hex.index(paladeumt) + len(paladeumt):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(token_script_hex))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.amount = 0
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = transfer_script_hex[:transfer_script_hex.index(paladeumt)] + paladeumt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_transfer = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)['hex']
        assert_raises_rpc_error(-26, "bad-txns-transfer-owner-amount-was-not-1", n0.sendrawtransaction, tx_bad_transfer_signed)

        # Sign and create the valid sub token transaction
        signed_hex = n0.signrawtransaction(tx_issue_sub_hex)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        assert_equal(1, n0.listmytokens()[root])
        assert_equal(1, n0.listmytokens()[token_name_multiple_sub])
        assert_equal(1, n0.listmytokens()[token_name_multiple_sub + '!'])
        assert_equal(1, n0.listmytokens()[owner])

    def transfer_token_tampering_test(self):
        self.log.info("Testing transfer of token transaction tampering...")
        n0, n1 = self.nodes[0], self.nodes[1]

        ########################################
        # create the root token that the sub token will try to be created from
        root = "TRANSFER_TX_TAMPERING"
        n0.issue(root, 10)
        n0.generate(1)
        self.sync_all()

        to_address = n1.getnewaddress()
        change_address = n0.getnewaddress()
        token_change = n0.getnewaddress()
        unspent = get_first_unspent(self, n0)
        unspent_token = n0.listmytokens(root, True)[root]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token[k] for k in ['txid', 'vout']},
        ]

        ########################################
        # Create the valid transfer outputs
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.01),
            to_address: {
                'transfer': {
                    root: 4,
                }
            },
            token_change: {
                'transfer': {
                    root: 6,
                }
            }
        }

        tx_transfer_hex = n0.createrawtransaction(inputs, outputs)

        ########################################
        # try tampering to issue sub token a mismatched the transfer amount to 0
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_transfer_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'
        # change the transfer amounts = 0
        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)
                token_script_hex = transfer_script_hex[transfer_script_hex.index(paladeumt) + len(paladeumt):-len(op_drop)]
                f = BytesIO(hex_str_to_bytes(token_script_hex))
                transfer = CScriptTransfer()
                transfer.deserialize(f)
                transfer.amount = 0
                tampered_transfer = bytes_to_hex_str(transfer.serialize())
                tampered_script = transfer_script_hex[:transfer_script_hex.index(paladeumt)] + paladeumt + tampered_transfer + op_drop
                tx.vout[n].scriptPubKey = hex_str_to_bytes(tampered_script)
        tx_bad_transfer = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_bad_transfer)['hex']
        assert_raises_rpc_error(-26, "Invalid parameter: token amount can't be equal to or less than zero.",
                                n0.sendrawtransaction, tx_bad_transfer_signed)

        signed_hex = n0.signrawtransaction(tx_transfer_hex)['hex']
        n0.sendrawtransaction(signed_hex)
        n0.generate(1)
        self.sync_all()

        assert_equal(6, n0.listmytokens()[root])
        assert_equal(4, n1.listmytokens()[root])
        assert_equal(1, n0.listmytokens()[root + '!'])

    def transfer_token_inserting_tampering_test(self):
        self.log.info("Testing of token issue inserting tampering...")
        n0 = self.nodes[0]

        # create the root token that the sub token will try to be created from
        root = "TRANSFER_TOKEN_INSERTING"
        n0.issue(root, 10)
        n0.generate(1)
        self.sync_all()

        change_address = n0.getnewaddress()
        to_address = n0.getnewaddress()  # back to n0 from n0
        unspent = get_first_unspent(self, n0)
        unspent_token = n0.listmytokens(root, True)[root]['outpoints'][0]

        inputs = [
            {k: unspent[k] for k in ['txid', 'vout']},
            {k: unspent_token[k] for k in ['txid', 'vout']},
        ]

        # Create the valid transfer and broadcast the transaction
        outputs = {
            change_address: truncate(float(unspent['amount']) - 0.00001),
            to_address: {
                'transfer': {
                    root: 10,
                }
            }
        }

        tx_transfer_hex = n0.createrawtransaction(inputs, outputs)

        # try tampering to issue sub token a mismatched the transfer amount to 0
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(tx_transfer_hex))
        tx.deserialize(f)
        paladeumt = '72766e74'  # paladeumt
        op_drop = '75'

        # create a new issue CTxOut
        issue_out = CTxOut()
        issue_out.nValue = 0

        # create a new issue script
        issue_script = CScriptIssue()
        issue_script.name = b'BYTE_ISSUE'
        issue_script.amount = 1
        issue_serialized = bytes_to_hex_str(issue_script.serialize())
        paladeumq = '72766e71'  # paladeumq

        for n in range(0, len(tx.vout)):
            out = tx.vout[n]
            if paladeumt in bytes_to_hex_str(out.scriptPubKey):
                transfer_out = out
                transfer_script_hex = bytes_to_hex_str(transfer_out.scriptPubKey)

                # Generate a script that has a valid destination address but switch it with paladeumq and the issue_serialized data
                issue_out.scriptPubKey = hex_str_to_bytes(transfer_script_hex[:transfer_script_hex.index(paladeumt)] + paladeumq + issue_serialized + op_drop)

        tx.vout.insert(0, issue_out)  # Insert the issue transaction at the begin on the vouts

        tx_inserted_issue = bytes_to_hex_str(tx.serialize())
        tx_bad_transfer_signed = n0.signrawtransaction(tx_inserted_issue)['hex']
        assert_raises_rpc_error(-26, "bad-txns-bad-token-transaction",
                                n0.sendrawtransaction, tx_bad_transfer_signed)

    def atomic_swaps_test(self):
        self.log.info("Testing atomic swaps...")
        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        jaina = "JAINA"
        anduin = "ANDUIN"
        jaina_owner = f"{jaina}!"
        anduin_owner = f"{anduin}!"

        starting_amount = 1000

        receive1 = n1.getnewaddress()
        change1 = n1.getnewaddress()
        n0.sendtoaddress(receive1, 50000)
        n0.generate(1)
        self.sync_all()
        n1.issue(jaina, starting_amount)
        n1.generate(1)
        self.sync_all()
        balance1 = float(n1.getwalletinfo()['balance'])

        receive2 = n2.getnewaddress()
        change2 = n2.getnewaddress()
        n0.sendtoaddress(receive2, 50000)
        n0.generate(1)
        self.sync_all()
        n2.issue(anduin, starting_amount)
        n2.generate(1)
        self.sync_all()
        balance2 = float(n2.getwalletinfo()['balance'])

        ########################################
        # paladeum for tokens

        # n1 buys 400 ANDUIN from n2 for 4000 PLB
        price = 4000
        amount = 400
        fee = 0.01

        unspent1 = get_first_unspent(self, n1, price + fee)
        unspent_amount1 = float(unspent1['amount'])

        unspent_token2 = n2.listmytokens(anduin, True)[anduin]['outpoints'][0]
        unspent_token_amount2 = unspent_token2['amount']
        assert (unspent_token_amount2 > amount)

        inputs = [
            {k: unspent1[k] for k in ['txid', 'vout']},
            {k: unspent_token2[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: {
                'transfer': {
                    anduin: amount
                }
            },
            change1: truncate(unspent_amount1 - price - fee),
            receive2: price,
            change2: {
                'transfer': {
                    anduin: truncate(unspent_token_amount2 - amount, 0)
                }
            },
        }

        unsigned = n1.createrawtransaction(inputs, outputs)
        signed1 = n1.signrawtransaction(unsigned)['hex']
        signed2 = n2.signrawtransaction(signed1)['hex']
        _tx_id = n0.sendrawtransaction(signed2)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        newbalance2 = float(n2.getwalletinfo()['balance'])
        assert_equal(truncate(balance1 - price - fee), newbalance1)
        assert_equal(balance2 + price, newbalance2)

        assert_equal(amount, int(n1.listmytokens()[anduin]))
        assert_equal(starting_amount - amount, int(n2.listmytokens()[anduin]))

        ########################################
        # paladeum for owner

        # n2 buys JAINA! from n1 for 20000 PLB
        price = 20000
        amount = 1
        balance1 = newbalance1
        balance2 = newbalance2

        unspent2 = get_first_unspent(self, n2, price + fee)
        unspent_amount2 = float(unspent2['amount'])

        unspent_owner1 = n1.listmytokens(jaina_owner, True)[jaina_owner]['outpoints'][0]
        unspent_owner_amount1 = unspent_owner1['amount']
        assert_equal(amount, unspent_owner_amount1)

        inputs = [
            {k: unspent2[k] for k in ['txid', 'vout']},
            {k: unspent_owner1[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: price,
            receive2: {
                'transfer': {
                    jaina_owner: amount
                }
            },
            change2: truncate(unspent_amount2 - price - fee),
        }

        unsigned = n2.createrawtransaction(inputs, outputs)
        signed2 = n2.signrawtransaction(unsigned)['hex']
        signed1 = n1.signrawtransaction(signed2)['hex']
        _tx_id = n0.sendrawtransaction(signed1)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        newbalance2 = float(n2.getwalletinfo()['balance'])
        assert_equal(balance1 + price, newbalance1)
        assert_equal(truncate(balance2 - price - fee), newbalance2)

        assert_does_not_contain_key(jaina_owner, n1.listmytokens())
        assert_equal(amount, int(n2.listmytokens()[jaina_owner]))

        ########################################
        # tokens for tokens and owner

        # n1 buys ANDUIN! and 300 ANDUIN from n2 for 900 JAINA
        price = 900
        amount = 300
        amount_owner = 1
        balance1 = newbalance1

        unspent1 = get_first_unspent(self, n1)
        unspent_amount1 = float(unspent1['amount'])
        assert (unspent_amount1 > fee)

        unspent_token1 = n1.listmytokens(jaina, True)[jaina]['outpoints'][0]
        unspent_token_amount1 = unspent_token1['amount']

        unspent_token2 = n2.listmytokens(anduin, True)[anduin]['outpoints'][0]
        unspent_token_amount2 = unspent_token2['amount']

        unspent_owner2 = n2.listmytokens(anduin_owner, True)[anduin_owner]['outpoints'][0]
        unspent_owner_amount2 = unspent_owner2['amount']

        assert (unspent_token_amount1 > price)
        assert (unspent_token_amount2 > amount)
        assert_equal(amount_owner, unspent_owner_amount2)

        inputs = [
            {k: unspent1[k] for k in ['txid', 'vout']},
            {k: unspent_token1[k] for k in ['txid', 'vout']},
            {k: unspent_token2[k] for k in ['txid', 'vout']},
            {k: unspent_owner2[k] for k in ['txid', 'vout']},
        ]
        outputs = {
            receive1: {
                'transfer': {
                    anduin: amount,
                    anduin_owner: amount_owner,
                }
            },
            # output map can't use change1 twice...
            n1.getnewaddress(): truncate(unspent_amount1 - fee),
            change1: {
                'transfer': {
                    jaina: truncate(unspent_token_amount1 - price)
                }
            },
            receive2: {
                'transfer': {
                    jaina: price
                }
            },
            change2: {
                'transfer': {
                    anduin: truncate(unspent_token_amount2 - amount, 0)
                }
            },
        }

        unsigned = n1.createrawtransaction(inputs, outputs)
        signed1 = n1.signrawtransaction(unsigned)['hex']
        signed2 = n2.signrawtransaction(signed1)['hex']
        _tx_id = n0.sendrawtransaction(signed2)
        n0.generate(10)
        self.sync_all()

        newbalance1 = float(n1.getwalletinfo()['balance'])
        assert_equal(truncate(balance1 - fee), newbalance1)

        assert_does_not_contain_key(anduin_owner, n2.listmytokens())
        assert_equal(amount_owner, int(n1.listmytokens()[anduin_owner]))

        assert_equal(unspent_token_amount1 - price, n1.listmytokens()[jaina])
        assert_equal(unspent_token_amount2 - amount, n2.listmytokens()[anduin])

    def getrawtransaction(self):
        self.log.info("Testing token info in getrawtransaction...")
        n0 = self.nodes[0]

        token_name = "RAW"
        token_amount = 1000
        units = 2
        units2 = 4
        reissuable = True
        ipfs_hash = "QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E"
        ipfs_hash2 = "QmQ7DysAQmy92cyQrkb5y1M96pGG1fKxnRkiB19qWSmH75"

        tx_id = n0.issue(token_name, token_amount, "", "", units, reissuable, True, ipfs_hash)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        token_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('token', token_out_script)
        token_section = token_out_script['token']
        assert_equal(token_name, token_section['name'])
        assert_equal(token_amount, token_section['amount'])
        assert_equal(units, token_section['units'])
        assert_equal(reissuable, token_section['reissuable'])
        assert_equal(ipfs_hash, token_section['ipfs_hash'])

        token_out_script = raw_json['vout'][-2]['scriptPubKey']
        assert_contains_key('token', token_out_script)
        token_section = token_out_script['token']
        assert_equal(token_name + "!", token_section['name'])
        assert_equal(1, token_section['amount'])
        assert_does_not_contain_key('units', token_section)
        assert_does_not_contain_key('reissuable', token_section)
        assert_does_not_contain_key('ipfs_hash', token_section)

        address = n0.getnewaddress()
        tx_id = n0.reissue(token_name, token_amount, address, "", True, -1, ipfs_hash2)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        token_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('token', token_out_script)
        token_section = token_out_script['token']
        assert_equal(token_name, token_section['name'])
        assert_equal(token_amount, token_section['amount'])
        assert_does_not_contain_key('units', token_section)
        assert_equal(ipfs_hash2, token_section['ipfs_hash'])

        address = n0.getnewaddress()
        tx_id = n0.reissue(token_name, token_amount, address, "", False, units2)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        token_out_script = raw_json['vout'][-1]['scriptPubKey']
        assert_contains_key('token', token_out_script)
        token_section = token_out_script['token']
        assert_equal(token_name, token_section['name'])
        assert_equal(token_amount, token_section['amount'])
        assert_equal(units2, token_section['units'])
        assert_does_not_contain_key('ipfs_hash', token_section)

        address = n0.getnewaddress()
        tx_id = n0.transfer(token_name, token_amount, address)[0]
        n0.generate(1)
        raw_json = n0.getrawtransaction(tx_id, True)
        found_token_out = False
        token_out_script = ''
        for vout in raw_json['vout']:
            out_script = vout['scriptPubKey']
            if 'token' in out_script:
                found_token_out = True
                token_out_script = out_script
        assert found_token_out
        token_section = token_out_script['token']
        assert_equal(token_name, token_section['name'])
        assert_equal(token_amount, token_section['amount'])

    def fundrawtransaction_transfer_outs(self):
        self.log.info("Testing fundrawtransaction with transfer outputs...")
        n0 = self.nodes[0]
        n2 = self.nodes[2]
        token_name = "DONT_FUND_PLB"
        token_amount = 100
        paladeum_amount = 100

        n2_address = n2.getnewaddress()

        n0.issue("XXX")
        n0.issue("YYY")
        n0.issue("ZZZ")
        n0.generate(1)
        n0.transfer("XXX", 1, n2_address)
        n0.transfer("YYY", 1, n2_address)
        n0.transfer("ZZZ", 1, n2_address)
        n0.generate(1)
        self.sync_all()

        # issue token
        n0.issue(token_name, token_amount)
        n0.generate(1)
        for _ in range(0, 5):
            n0.transfer(token_name, token_amount / 5, n2_address)
        n0.generate(1)
        self.sync_all()

        for _ in range(0, 5):
            n0.sendtoaddress(n2_address, paladeum_amount / 5)
        n0.generate(1)
        self.sync_all()

        inputs = []
        unspent_token = n2.listmytokens(token_name, True)[token_name]['outpoints'][0]
        inputs.append({k: unspent_token[k] for k in ['txid', 'vout']})
        n0_address = n0.getnewaddress()
        outputs = {n0_address: {'transfer': {token_name: token_amount / 5}}}
        tx = n2.createrawtransaction(inputs, outputs)

        tx_funded = n2.fundrawtransaction(tx)['hex']
        signed = n2.signrawtransaction(tx_funded)['hex']
        n2.sendrawtransaction(signed)
        # no errors, yay

    def fundrawtransaction_nonwallet_transfer_outs(self):
        self.log.info("Testing fundrawtransaction with non-wallet transfer outputs...")
        n0 = self.nodes[0]
        n1 = self.nodes[1]
        n2 = self.nodes[2]
        token_name = "NODE0_STUFF"
        n1_address = n1.getnewaddress()
        n2_address = n2.getnewaddress()

        # fund n2
        n0.sendtoaddress(n2_address, 1000)
        n0.generate(1)
        self.sync_all()

        # issue
        token_amount = 100
        n0.issue(token_name, token_amount)
        n0.generate(1)
        self.sync_all()

        # have n2 construct transfer to n1_address using n0's utxos
        inputs = []
        unspent_token = n0.listmytokens(token_name, True)[token_name]['outpoints'][0]
        inputs.append({k: unspent_token[k] for k in ['txid', 'vout']})
        outputs = {n1_address: {'transfer': {token_name: token_amount}}}
        tx = n2.createrawtransaction(inputs, outputs)

        # n2 pays postage (fee)
        tx_funded = n2.fundrawtransaction(tx, {"feeRate": 0.02})['hex']

        # n2 signs postage; n0 signs transfer
        signed1 = n2.signrawtransaction(tx_funded)
        signed2 = n0.signrawtransaction(signed1['hex'])

        # send and verify
        n2.sendrawtransaction(signed2['hex'])
        n2.generate(1)
        self.sync_all()
        assert_contains_pair(token_name, token_amount, n1.listmytokens())

    def run_test(self):
        self.activate_tokens()
        self.issue_reissue_transfer_test()
        self.unique_tokens_test()
        self.issue_tampering_test()
        self.reissue_tampering_test()
        self.transfer_token_tampering_test()
        self.transfer_token_inserting_tampering_test()
        self.unique_tokens_via_issue_test()
        self.bad_ipfs_hash_test()
        self.atomic_swaps_test()
        self.issue_invalid_address_test()
        self.issue_sub_invalid_address_test()
        self.issue_multiple_outputs_test()
        self.issue_sub_multiple_outputs_test()
        self.getrawtransaction()
        self.fundrawtransaction_transfer_outs()
        self.fundrawtransaction_nonwallet_transfer_outs()


if __name__ == '__main__':
    RawTokenTransactionsTest().main()
