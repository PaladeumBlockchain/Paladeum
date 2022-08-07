#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test restricted token related RPC commands."""

import math
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal

BURN_ADDRESSES = {
    'issue_restricted': 'n1issueRestrictedXXXXXXXXXXXXZVT9V',
    'reissue_restricted': 'n1ReissueTokenXXXXXXXXXXXXXXWG9NLd',
    'issue_qualifier': 'n1issueQuaLifierXXXXXXXXXXXXUysLTj',
    'issue_subqualifier': 'n1issueSubQuaLifierXXXXXXXXXYffPLh',
    'tag_address': 'n1addTagBurnXXXXXXXXXXXXXXXXX5oLMH',
}

BURN_AMOUNTS = {
    'issue_restricted': 1500,
    'reissue_restricted': 100,
    'issue_qualifier': 1000,
    'issue_subqualifier': 100,
    'tag_address': 0.1,
}

FEE_AMOUNT = 0.01


def truncate(number, digits=8):
    stepper = pow(10.0, digits)
    return math.trunc(stepper * number) / stepper


def get_tx_issue_hex(node, to_address, token_name, token_quantity=1000, verifier_string="true", units=0, reissuable=1, has_ipfs=0, ipfs_hash="", owner_change_address=""):
    change_address = node.getnewaddress()

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > BURN_AMOUNTS['issue_restricted'])
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    owner_token_name = token_name[1:] + '!'
    owner_unspent = node.listmytokens(owner_token_name, True)[owner_token_name]['outpoints'][0]
    owner_inputs = [{k: owner_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        BURN_ADDRESSES['issue_restricted']: BURN_AMOUNTS['issue_restricted'],
        change_address: truncate(float(paladeum_unspent['amount']) - BURN_AMOUNTS['issue_restricted'] - FEE_AMOUNT),
        to_address: {
            'issue_restricted': {
                'token_name': token_name,
                'token_quantity': token_quantity,
                'verifier_string': verifier_string,
                'units': units,
                'reissuable': reissuable,
                'has_ipfs': has_ipfs,
            }
        }
    }
    if has_ipfs == 1:
        outputs[to_address]['issue_restricted']['ipfs_hash'] = ipfs_hash
    if len(owner_change_address) > 0:
        outputs[to_address]['issue_restricted']['owner_change_address'] = owner_change_address

    tx_issue = node.createrawtransaction(paladeum_inputs + owner_inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


def get_tx_reissue_hex(node, to_address, token_name, token_quantity, reissuable=1, verifier_string="", ipfs_hash="", owner_change_address=""):
    change_address = node.getnewaddress()

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > BURN_AMOUNTS['reissue_restricted'])
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    owner_token_name = token_name[1:] + '!'
    owner_unspent = node.listmytokens(owner_token_name, True)[owner_token_name]['outpoints'][0]
    owner_inputs = [{k: owner_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        BURN_ADDRESSES['reissue_restricted']: BURN_AMOUNTS['reissue_restricted'],
        change_address: truncate(float(paladeum_unspent['amount']) - BURN_AMOUNTS['reissue_restricted'] - FEE_AMOUNT),
        to_address: {
            'reissue_restricted': {
                'token_name': token_name,
                'token_quantity': token_quantity,
                'reissuable': reissuable,
            }
        }
    }
    if len(verifier_string) > 0:
        outputs[to_address]['reissue_restricted']['verifier_string'] = verifier_string
    if len(ipfs_hash) > 0:
        outputs[to_address]['reissue_restricted']['ipfs_hash'] = ipfs_hash
    if len(owner_change_address) > 0:
        outputs[to_address]['reissue_restricted']['owner_change_address'] = owner_change_address

    tx_issue = node.createrawtransaction(paladeum_inputs + owner_inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


def get_tx_issue_qualifier_hex(node, to_address, token_name, token_quantity=1, has_ipfs=0, ipfs_hash="", root_change_address="", change_qty=1):
    change_address = node.getnewaddress()

    is_sub_qualifier = len(token_name.split('/')) > 1

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > BURN_AMOUNTS['issue_qualifier'])
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    root_inputs = []
    if is_sub_qualifier:
        root_token_name = token_name.split('/')[0]
        root_unspent = node.listmytokens(root_token_name, True)[root_token_name]['outpoints'][0]
        root_inputs = [{k: root_unspent[k] for k in ['txid', 'vout']}]

    burn_address = BURN_ADDRESSES['issue_subqualifier'] if is_sub_qualifier else BURN_ADDRESSES['issue_qualifier']
    burn_amount = BURN_AMOUNTS['issue_subqualifier'] if is_sub_qualifier else BURN_AMOUNTS['issue_qualifier']
    outputs = {
        burn_address: burn_amount,
        change_address: truncate(float(paladeum_unspent['amount']) - burn_amount - FEE_AMOUNT),
        to_address: {
            'issue_qualifier': {
                'token_name': token_name,
                'token_quantity': token_quantity,
                'has_ipfs': has_ipfs,
            }
        }
    }
    if has_ipfs == 1:
        outputs[to_address]['issue_qualifier']['ipfs_hash'] = ipfs_hash
    if len(root_change_address) > 0:
        outputs[to_address]['issue_qualifier']['root_change_address'] = root_change_address
    if change_qty > 1:
        outputs[to_address]['issue_qualifier']['change_quantity'] = change_qty

    tx_issue = node.createrawtransaction(paladeum_inputs + root_inputs, outputs)
    tx_issue_signed = node.signrawtransaction(tx_issue)
    tx_issue_hex = tx_issue_signed['hex']
    return tx_issue_hex


def get_tx_transfer_hex(node, to_address, token_name, token_quantity):
    change_address = node.getnewaddress()
    token_change_address = node.getnewaddress()

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > FEE_AMOUNT)
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    token_unspent = node.listmytokens(token_name, True)[token_name]['outpoints'][0]
    token_unspent_qty = token_unspent['amount']
    token_inputs = [{k: token_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        change_address: truncate(float(paladeum_unspent['amount']) - FEE_AMOUNT),
        to_address: {
            'transfer': {
                token_name: token_quantity
            }
        }
    }
    if token_unspent_qty > token_quantity:
        outputs[token_change_address] = {
            'transfer': {
                token_name: token_unspent_qty - token_quantity
            }
        }

    tx_transfer = node.createrawtransaction(paladeum_inputs + token_inputs, outputs)
    tx_transfer_signed = node.signrawtransaction(tx_transfer)
    tx_transfer_hex = tx_transfer_signed['hex']
    return tx_transfer_hex


def get_tx_tag_address_hex(node, op, qualifier_name, tag_addresses, qualifier_change_address, change_qty=1):
    change_address = node.getnewaddress()

    burn_amount = truncate(float(BURN_AMOUNTS['tag_address'] * len(tag_addresses)))

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > burn_amount)
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    qualifier_unspent = node.listmytokens(qualifier_name, True)[qualifier_name]['outpoints'][0]
    qualifier_inputs = [{k: qualifier_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        BURN_ADDRESSES['tag_address']: burn_amount,
        change_address: truncate(float(paladeum_unspent['amount']) - burn_amount - FEE_AMOUNT),
        qualifier_change_address: {
            f"{op}_addresses": {
                'qualifier': qualifier_name,
                'addresses': tag_addresses,
            }
        },
    }
    if change_qty > 1:
        outputs[qualifier_change_address][f"{op}_addresses"]['change_quantity'] = change_qty

    tx_tag = node.createrawtransaction(paladeum_inputs + qualifier_inputs, outputs)
    tx_tag_signed = node.signrawtransaction(tx_tag)
    tx_tag_hex = tx_tag_signed['hex']
    return tx_tag_hex


def get_tx_freeze_address_hex(node, op, token_name, freeze_addresses, owner_change_address):
    change_address = node.getnewaddress()

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > FEE_AMOUNT)
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    owner_token_name = token_name[1:] + '!'
    owner_unspent = node.listmytokens(owner_token_name, True)[owner_token_name]['outpoints'][0]
    owner_inputs = [{k: owner_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        change_address: truncate(float(paladeum_unspent['amount']) - FEE_AMOUNT),
        owner_change_address: {
            f"{op}_addresses": {
                'token_name': token_name,
                'addresses': freeze_addresses,
            }
        },
    }

    tx_freeze = node.createrawtransaction(paladeum_inputs + owner_inputs, outputs)
    tx_freeze_signed = node.signrawtransaction(tx_freeze)
    tx_freeze_hex = tx_freeze_signed['hex']
    return tx_freeze_hex


# get_tx_freeze_token_hex(n0, "freeze", token_name, owner_change_address)
def get_tx_freeze_token_hex(node, op, token_name, owner_change_address):
    change_address = node.getnewaddress()

    paladeum_unspent = next(u for u in node.listunspent() if u['amount'] > FEE_AMOUNT)
    paladeum_inputs = [{k: paladeum_unspent[k] for k in ['txid', 'vout']}]

    owner_token_name = token_name[1:] + '!'
    owner_unspent = node.listmytokens(owner_token_name, True)[owner_token_name]['outpoints'][0]
    owner_inputs = [{k: owner_unspent[k] for k in ['txid', 'vout']}]

    outputs = {
        change_address: truncate(float(paladeum_unspent['amount']) - FEE_AMOUNT),
        owner_change_address: {
            f"{op}_token": {
                'token_name': token_name,
            }
        },
    }

    tx_freeze = node.createrawtransaction(paladeum_inputs + owner_inputs, outputs)
    tx_freeze_signed = node.signrawtransaction(tx_freeze)
    tx_freeze_hex = tx_freeze_signed['hex']
    return tx_freeze_hex


class RawRestrictedTokensTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-tokenindex'], ['-tokenindex']]

    def activate_restricted_tokens(self):
        self.log.info("Generating PLD and activating restricted tokens...")
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        n1 = self.nodes[1]
        n0.sendtoaddress(n1.getnewaddress(), 2500)
        n0.generate(1)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'])

    def issue_restricted_test(self):
        self.log.info("Testing raw issue_restricted...")
        n0 = self.nodes[0]

        base_token_name = "ISSUE_RESTRICTED_TEST"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        to_address = n0.getnewaddress()
        units = 2
        reissuable = 0
        has_ipfs = 1
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        owner_change_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        hex_data = get_tx_issue_hex(n0, to_address, token_name, qty, verifier, units, reissuable, has_ipfs, ipfs_hash, owner_change_address)
        txid = n0.sendrawtransaction(hex_data)
        n0.generate(1)

        # verify
        assert_equal(64, len(txid))
        assert_equal(qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        token_data = n0.gettokendata(token_name)
        assert_equal(qty, token_data['amount'])
        assert_equal(verifier, token_data['verifier_string'])
        assert_equal(units, token_data['units'])
        assert_equal(reissuable, token_data['reissuable'])
        assert_equal(has_ipfs, token_data['has_ipfs'])
        assert_equal(ipfs_hash, token_data['ipfs_hash'])
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

    def reissue_restricted_test(self):
        self.log.info("Testing raw reissue_restricted...")
        n0 = self.nodes[0]

        base_token_name = "REISSUE_RESTRICTED_TEST"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        to_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        n0.issuerestrictedtoken(token_name, qty, verifier, to_address)
        n0.generate(1)

        reissue_qty = 5000
        reissuable = 0
        qualifier = "#CYA"
        reissue_verifier = qualifier[1:]
        has_ipfs = 1
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        owner_change_address = n0.getnewaddress()

        n0.issuequalifiertoken(qualifier)
        n0.generate(1)
        n0.addtagtoaddress(qualifier, to_address)
        n0.generate(1)

        hex_data = get_tx_reissue_hex(n0, to_address, token_name, reissue_qty, reissuable, reissue_verifier, ipfs_hash, owner_change_address)
        txid = n0.sendrawtransaction(hex_data)
        n0.generate(1)

        # verify
        assert_equal(64, len(txid))
        assert_equal(qty + reissue_qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        token_data = n0.gettokendata(token_name)
        assert_equal(qty + reissue_qty, token_data['amount'])
        assert_equal(reissuable, token_data['reissuable'])
        assert_equal(reissue_verifier, token_data['verifier_string'])
        assert_equal(has_ipfs, token_data['has_ipfs'])
        assert_equal(ipfs_hash, token_data['ipfs_hash'])
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

    def issue_qualifier_test(self):
        self.log.info("Testing raw issue_qualifier...")
        n0 = self.nodes[0]

        token_name = "#UROK"
        qty = 2
        to_address = n0.getnewaddress()
        has_ipfs = 1
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"

        # ROOT QUALIFIER
        hex_data = get_tx_issue_qualifier_hex(n0, to_address, token_name, qty, has_ipfs, ipfs_hash)
        txid = n0.sendrawtransaction(hex_data)
        n0.generate(1)

        # verify
        assert_equal(64, len(txid))
        assert_equal(qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        token_data = n0.gettokendata(token_name)
        assert_equal(qty, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(0, token_data['reissuable'])
        assert_equal(has_ipfs, token_data['has_ipfs'])
        assert_equal(ipfs_hash, token_data['ipfs_hash'])

        sub_token_name = "#UROK/#IGUESS"
        sub_qty = 5
        sub_to_address = n0.getnewaddress()
        sub_has_ipfs = 1
        sub_ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        root_change_address = n0.getnewaddress()

        # SUB-QUALIFIER
        sub_hex = get_tx_issue_qualifier_hex(n0, sub_to_address, sub_token_name, sub_qty, sub_has_ipfs, sub_ipfs_hash, root_change_address, qty)
        sub_txid = n0.sendrawtransaction(sub_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(sub_txid))
        assert_equal(sub_qty, n0.listmytokens(sub_token_name, True)[sub_token_name]['balance'])
        token_data = n0.gettokendata(sub_token_name)
        assert_equal(sub_qty, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(0, token_data['reissuable'])
        assert_equal(sub_has_ipfs, token_data['has_ipfs'])
        assert_equal(sub_ipfs_hash, token_data['ipfs_hash'])
        assert_equal(qty, n0.listtokenbalancesbyaddress(root_change_address)[token_name])

    def transfer_qualifier_test(self):
        self.log.info("Testing raw transfer qualifier...")
        n0, n1 = self.nodes[0], self.nodes[1]

        token_name = "#XFERME"
        qty = 5
        n0_address = n0.getnewaddress()

        hex_data = get_tx_issue_qualifier_hex(n0, n0_address, token_name, qty)
        n0.sendrawtransaction(hex_data)
        n0.generate(1)

        n1_address = n1.getnewaddress()
        xfer_qty = 2
        hex_data = get_tx_transfer_hex(n0, n1_address, token_name, xfer_qty)
        n0.sendrawtransaction(hex_data)
        n0.generate(1)
        self.sync_all()

        # verify
        assert_equal(qty - xfer_qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        assert_equal(xfer_qty, n1.listtokenbalancesbyaddress(n1_address)[token_name])

    def address_tagging_test(self):
        self.log.info("Testing address tagging/untagging...")
        n0 = self.nodes[0]

        qualifier_name = "#LGTM"
        qty = 2
        issue_address = n0.getnewaddress()

        issue_hex = get_tx_issue_qualifier_hex(n0, issue_address, qualifier_name, qty)
        n0.sendrawtransaction(issue_hex)
        n0.generate(1)

        tag_addresses = [n0.getnewaddress(), n0.getnewaddress(), n0.getnewaddress()]

        # verify
        for tag_address in tag_addresses:
            assert (not n0.checkaddresstag(tag_address, qualifier_name))

        # tag
        change_address = n0.getnewaddress()
        tag_hex = get_tx_tag_address_hex(n0, "tag", qualifier_name, tag_addresses, change_address, 2)
        tag_txid = n0.sendrawtransaction(tag_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(tag_txid))
        for tag_address in tag_addresses:
            assert (n0.checkaddresstag(tag_address, qualifier_name))
        assert_equal(qty, n0.listtokenbalancesbyaddress(change_address)[qualifier_name])

        # untag
        change_address = n0.getnewaddress()
        tag_hex = get_tx_tag_address_hex(n0, "untag", qualifier_name, tag_addresses, change_address, 2)
        tag_txid = n0.sendrawtransaction(tag_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(tag_txid))
        for tag_address in tag_addresses:
            assert (not n0.checkaddresstag(tag_address, qualifier_name))
        assert_equal(qty, n0.listtokenbalancesbyaddress(change_address)[qualifier_name])

    def address_freezing_test(self):
        self.log.info("Testing address freezing/unfreezing...")
        n0 = self.nodes[0]

        base_token_name = "ADDRESS_FREEZING_TEST"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        issue_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        n0.issuerestrictedtoken(token_name, qty, verifier, issue_address)
        n0.generate(1)

        freeze_addresses = [n0.getnewaddress(), n0.getnewaddress(), n0.getnewaddress()]

        # verify
        for freeze_address in freeze_addresses:
            assert (not n0.checkaddressrestriction(freeze_address, token_name))

        # freeze
        owner_change_address = n0.getnewaddress()
        freeze_hex = get_tx_freeze_address_hex(n0, "freeze", token_name, freeze_addresses, owner_change_address)
        freeze_txid = n0.sendrawtransaction(freeze_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(freeze_txid))
        for freeze_address in freeze_addresses:
            assert (n0.checkaddressrestriction(freeze_address, token_name))
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

        # unfreeze
        owner_change_address = n0.getnewaddress()
        freeze_hex = get_tx_freeze_address_hex(n0, "unfreeze", token_name, freeze_addresses, owner_change_address)
        freeze_txid = n0.sendrawtransaction(freeze_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(freeze_txid))
        for freeze_address in freeze_addresses:
            assert (not n0.checkaddressrestriction(freeze_address, token_name))
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

    def token_freezing_test(self):
        self.log.info("Testing token freezing/unfreezing...")
        n0 = self.nodes[0]

        base_token_name = "TOKEN_FREEZING_TEST"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        issue_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        n0.issuerestrictedtoken(token_name, qty, verifier, issue_address)
        n0.generate(1)

        # verify
        assert (not n0.checkglobalrestriction(token_name))

        # freeze
        owner_change_address = n0.getnewaddress()
        freeze_hex = get_tx_freeze_token_hex(n0, "freeze", token_name, owner_change_address)
        freeze_txid = n0.sendrawtransaction(freeze_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(freeze_txid))
        assert (n0.checkglobalrestriction(token_name))
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

        # unfreeze
        owner_change_address = n0.getnewaddress()
        freeze_hex = get_tx_freeze_token_hex(n0, "unfreeze", token_name, owner_change_address)
        freeze_txid = n0.sendrawtransaction(freeze_hex)
        n0.generate(1)

        # verify
        assert_equal(64, len(freeze_txid))
        assert (not n0.checkglobalrestriction(token_name))
        assert_equal(1, n0.listtokenbalancesbyaddress(owner_change_address)[f"{base_token_name}!"])

    def run_test(self):
        self.activate_restricted_tokens()

        self.issue_restricted_test()
        self.reissue_restricted_test()
        self.issue_qualifier_test()
        self.transfer_qualifier_test()
        self.address_tagging_test()
        self.address_freezing_test()
        self.token_freezing_test()


if __name__ == '__main__':
    RawRestrictedTokensTest().main()
