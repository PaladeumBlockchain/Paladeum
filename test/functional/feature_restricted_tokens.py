#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test restricted token related RPC commands."""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, assert_does_not_contain_key, assert_does_not_contain, assert_contains_key, assert_happening, assert_contains

# noinspection PyAttributeOutsideInit
class RestrictedTokensTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-tokenindex'], ['-tokenindex']]

    def activate_restricted_tokens(self):
        self.log.info("Generating PLB and activating restricted tokens...")
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        n1 = self.nodes[1]
        n0.sendtoaddress(n1.getnewaddress(), 2500)
        n0.generate(1)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'])

    def issuerestrictedtoken(self):
        self.log.info("Testing issuerestrictedtoken() with required params...")
        n0 = self.nodes[0]

        base_token_name = "ISSUERESTRICTEDTOKEN"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        to_address = n0.getnewaddress()

        # required params
        assert_raises_rpc_error(None, "Arguments:", n0.issuerestrictedtoken)
        assert_raises_rpc_error(None, "Arguments:", n0.issuerestrictedtoken, token_name)
        assert_raises_rpc_error(None, "Arguments:", n0.issuerestrictedtoken, token_name, qty)
        assert_raises_rpc_error(None, "Arguments:", n0.issuerestrictedtoken, token_name, qty, verifier)

        # valid params
        assert_raises_rpc_error(None, "Invalid token name", n0.issuerestrictedtoken, "$!N\/AL!D", qty, verifier, to_address)
        assert_raises_rpc_error(None, "Verifier string can not be empty", n0.issuerestrictedtoken, token_name, qty, "", to_address)
        assert_raises_rpc_error(None, "bad-txns-null-verifier-failed-syntax-check", n0.issuerestrictedtoken, token_name, qty, "false && true", to_address)
        assert_raises_rpc_error(None, "bad-txns-null-verifier-contains-non-issued-qualifier", n0.issuerestrictedtoken, token_name, qty, "#NONEXIZTENT", to_address)
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.issuerestrictedtoken, token_name, qty, verifier, "garbageaddress")

        # base token required
        assert_raises_rpc_error(-32600, f"Wallet doesn't have token: {base_token_name}!", n0.issuerestrictedtoken, token_name, qty, verifier, to_address)
        n0.issue(base_token_name)

        # issue
        txid = n0.issuerestrictedtoken(token_name, qty, verifier, to_address)

        # verify
        assert_equal(64, len(txid[0]))
        assert_equal(qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        n0.generate(1)
        token_data = n0.gettokendata(token_name)
        assert_equal(qty, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(1, token_data['reissuable'])
        assert_equal(0, token_data['has_ipfs'])
        assert_equal('true', token_data['verifier_string'])

    def issuerestrictedtoken_full(self):
        self.log.info("Testing issuerestrictedtoken() with optional params...")
        n0 = self.nodes[0]

        base_token_name = "ISSUERESTRICTEDTOKEN_FULL"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        units = 2
        reissuable = False
        has_ipfs = True
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        n0.issue(base_token_name)

        # valid params
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.issuerestrictedtoken, token_name, qty, verifier, to_address, "garbagechangeaddress")
        assert_raises_rpc_error(None, "Units must be between 0 and 8", n0.issuerestrictedtoken, token_name, qty, verifier, to_address, change_address, 9)
        assert_raises_rpc_error(None, "Units must be between 0 and 8", n0.issuerestrictedtoken, token_name, qty, verifier, to_address, change_address, -1)

        # issue
        txid = n0.issuerestrictedtoken(token_name, qty, verifier, to_address, change_address, units, reissuable, has_ipfs, ipfs_hash)

        # verify
        assert_equal(64, len(txid[0]))
        assert_equal(qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        n0.generate(1)
        token_data = n0.gettokendata(token_name)
        assert_equal(qty, token_data['amount'])
        assert_equal(units, token_data['units'])
        assert_equal(reissuable, token_data['reissuable'])
        assert_equal(has_ipfs, token_data['has_ipfs'])
        assert_equal(verifier, token_data['verifier_string'])
        assert_equal(verifier, n0.getverifierstring(token_name))

    def reissuerestrictedtoken_full(self):
        self.log.info("Testing reissuerestrictedtoken() with all params...")
        n0, n1 = self.nodes[0], self.nodes[1]

        base_token_name = "REISSUERESTRICTEDTOKEN_FULL"
        token_name = f"${base_token_name}"

        orig_qty = 10000
        orig_verifier = "true"
        orig_to_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.issuerestrictedtoken(token_name, orig_qty, orig_verifier, orig_to_address)
        n0.generate(1)

        foreign_base_token_name = "SEA"
        foreign_token_name = f"${foreign_base_token_name}"
        n1.issue(foreign_base_token_name)
        n1.issuerestrictedtoken(foreign_token_name, 1, "true", n1.getnewaddress())
        n1.generate(1)
        self.sync_all()

        qty = 5000
        to_address = n0.getnewaddress()
        change_verifier = True
        qualifier = "#CYA"
        verifier = qualifier
        change_address = n0.getnewaddress()
        units = -1
        reissuable = False
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"

        n0.issuequalifiertoken(qualifier)
        n0.generate(1)
        n0.addtagtoaddress(qualifier, to_address)
        n0.generate(1)

        # valid params
        assert_raises_rpc_error(None, "Invalid token name", n0.reissuerestrictedtoken, "$!N\/AL!D", qty, to_address)
        assert_raises_rpc_error(None, "Wallet doesn't have token", n0.reissuerestrictedtoken, foreign_token_name, qty, to_address)
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.reissuerestrictedtoken, token_name, qty, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.reissuerestrictedtoken, token_name, qty, to_address, change_verifier, verifier, "garbagechangeaddress")
        assert_raises_rpc_error(None, "Units must be between -1 and 8", n0.reissuerestrictedtoken, token_name, qty, to_address, change_verifier, verifier, change_address, 9)
        assert_raises_rpc_error(None, "Units must be between -1 and 8", n0.reissuerestrictedtoken, token_name, qty, to_address, change_verifier, verifier, change_address, -2)

        # reissue
        txid = n0.reissuerestrictedtoken(token_name, qty, to_address, change_verifier, verifier, change_address, units, reissuable, ipfs_hash)

        # verify
        assert_equal(64, len(txid[0]))
        assert_equal(orig_qty + qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        n0.generate(1)
        token_data = n0.gettokendata(token_name)
        assert_equal(orig_qty + qty, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(reissuable, token_data['reissuable'])
        assert_equal(True, token_data['has_ipfs'])
        assert_equal(verifier.replace("#", ""), token_data['verifier_string'])
        assert_equal("CYA", n0.getverifierstring(token_name))

    def issuequalifiertoken(self):
        self.log.info("Testing issuequalifiertoken() with required params...")
        n0 = self.nodes[0]

        token_name = "#IQA"

        assert_raises_rpc_error(None, "Invalid token name", n0.issuequalifiertoken, "#!N\/AL!D")
        assert_raises_rpc_error(None, "Invalid token name", n0.issuequalifiertoken, "N\/AL!D")

        # issue
        txid = n0.issuequalifiertoken(token_name)

        # verify
        assert_equal(64, len(txid[0]))
        assert_equal(1, n0.listmytokens(token_name, True)[token_name]['balance'])
        n0.generate(1)
        token_data = n0.gettokendata(token_name)
        assert_equal(1, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(False, token_data['reissuable'])
        assert_equal(False, token_data['has_ipfs'])

    def issue_qualifier_token_full(self):
        self.log.info("Testing issuequalifiertoken() with all params...")
        n0 = self.nodes[0]

        token_name = "#IQA_FULL"
        qty = 2
        to_address = n0.getnewaddress()
        change_address = n0.getnewaddress()
        has_ipfs = True
        ipfs_hash = "QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"

        assert_raises_rpc_error(None, "Amount must be between 1 and 10", n0.issuequalifiertoken, token_name, 0)
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.issuequalifiertoken, token_name, qty, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.issuequalifiertoken, token_name, qty, to_address, "gargabechangeaddress")
        assert_raises_rpc_error(None, "ipfs_hash must be 46 characters", n0.issuequalifiertoken, token_name, qty, to_address, change_address, True)

        # issue
        txid = n0.issuequalifiertoken(token_name, qty, to_address, change_address, has_ipfs, ipfs_hash)

        # verify
        assert_equal(64, len(txid[0]))
        assert_equal(qty, n0.listmytokens(token_name, True)[token_name]['balance'])
        n0.generate(1)
        token_data = n0.gettokendata(token_name)
        assert_equal(qty, token_data['amount'])
        assert_equal(0, token_data['units'])
        assert_equal(False, token_data['reissuable'])
        assert_equal(True, token_data['has_ipfs'])

    def transferqualifier(self):
        self.log.info("Testing transferqualifier()...")
        n0, n1 = self.nodes[0], self.nodes[1]

        token_name = "#TQA"
        nonqualifier_token_name = "NOTAQUALIFIER"
        n0_change_address = n0.getnewaddress()
        n1_address = n1.getnewaddress()
        message = "QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"

        n0.issuequalifiertoken(token_name)
        n0.issue(nonqualifier_token_name)
        n0.generate(1)
        self.sync_all()

        assert_equal(1, n0.listmytokens(token_name, True)[token_name]['balance'])
        assert_does_not_contain_key(token_name, n1.listmytokens())

        assert_raises_rpc_error(None, "Only use this rpc call to send Qualifier tokens", n0.transferqualifier, nonqualifier_token_name, 1, n1_address)
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.transferqualifier, token_name, 1, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.transferqualifier, token_name, 1, n1_address, "garbagechangeaddress")
        assert_raises_rpc_error(None, "Invalid IPFS hash", n0.transferqualifier, token_name, 1, n1_address, n0_change_address, "garbagemessage")

        # transfer
        txid = n0.transferqualifier(token_name, 1, n1_address, n0_change_address, message)
        n0.generate(1)
        self.sync_all()

        # verify
        assert_equal(64, len(txid[0]))
        assert_does_not_contain_key(token_name, n0.listmytokens())
        assert_equal(1, n1.listmytokens(token_name, True)[token_name]['balance'])

    def tagging(self):
        self.log.info("Testing tagging...")
        n0 = self.nodes[0]

        tag = "#TAG"
        address = n0.getnewaddress()
        change_address = n0.getnewaddress()

        n0.issuequalifiertoken(tag)
        n0.generate(1)

        base_token_name = "TAG_RESTRICTED"
        token_name = f"${base_token_name}"
        qty = 1000
        verifier = tag
        issue_address = n0.getnewaddress()
        n0.issue(base_token_name)
        assert_raises_rpc_error(-8, "bad-txns-null-verifier-address-failed-verification", n0.issuerestrictedtoken, token_name, qty, verifier, issue_address)

        # Isolate this test from other tagging on n0...
        def viewmytaggedaddresses():
            return list(filter(lambda x: tag == x['Tag Name'], n0.viewmytaggedaddresses()))

        assert_equal(0, len(viewmytaggedaddresses()))

        n0.addtagtoaddress(tag, issue_address, change_address)
        n0.generate(1)
        n0.issuerestrictedtoken(token_name, qty, verifier, issue_address)
        n0.generate(1)

        # pre-tagging verification
        assert_does_not_contain(address, n0.listaddressesfortag(tag))
        assert_does_not_contain(tag, n0.listtagsforaddress(address))
        assert not n0.checkaddresstag(address, tag)

        # viewmytaggedaddresses
        tagged = viewmytaggedaddresses()
        assert_equal(1, len(tagged))
        t1 = tagged[0]
        assert_equal(issue_address, t1['Address'])
        assert_equal(tag, t1['Tag Name'])
        assert_contains_key('Assigned', t1)
        assert_happening(t1['Assigned'])

        assert_raises_rpc_error(-8, "bad-txns-null-verifier-address-failed-verification", n0.transfer, token_name, 100, address)

        # special case: make sure transfer fails if change address(es) are verified even though to address isn't
        paladeum_change_address = n0.getnewaddress()
        token_change_address = n0.getnewaddress()
        n0.addtagtoaddress(tag, paladeum_change_address)
        n0.addtagtoaddress(tag, token_change_address)
        n0.generate(1)
        assert_raises_rpc_error(-8, "bad-txns-null-verifier-address-failed-verification", n0.transfer, token_name, 100, address, "", 0, paladeum_change_address, token_change_address)
        n0.removetagfromaddress(tag, paladeum_change_address)
        n0.removetagfromaddress(tag, token_change_address)
        n0.generate(1)
        ##

        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.addtagtoaddress, tag, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.addtagtoaddress, tag, address, "garbagechangeaddress")

        n0.addtagtoaddress(tag, address, change_address)
        n0.generate(1)

        assert_raises_rpc_error(-32600, "add-qualifier-when-already-assigned", n0.addtagtoaddress, tag, address, change_address)

        # post-tagging verification
        assert_contains(address, n0.listaddressesfortag(tag))
        assert_contains(tag, n0.listtagsforaddress(address))
        assert n0.checkaddresstag(address, tag)

        # viewmytaggedaddresses
        tagged = viewmytaggedaddresses()
        assert_equal(4, len(tagged))
        assert_contains(issue_address, list(map(lambda x: x['Address'], tagged)))
        assert_contains(address, list(map(lambda x: x['Address'], tagged)))
        for t in tagged:
            assert_equal(tag, t['Tag Name'])
            if 'Assigned' in t:
                assert_happening(t['Assigned'])
            else:
                assert_happening(t['Removed'])

        # special case: make sure transfer fails if the token change address isn't verified (even if the paladeum change address is)
        paladeum_change_address = n0.getnewaddress()
        token_change_address = n0.getnewaddress()
        assert_raises_rpc_error(-20, "bad-txns-null-verifier-address-failed-verification", n0.transfer, token_name, 100, address, "", 0, paladeum_change_address, token_change_address)
        n0.addtagtoaddress(tag, paladeum_change_address)
        n0.generate(1)
        assert_raises_rpc_error(-20, "bad-txns-null-verifier-address-failed-verification", n0.transfer, token_name, 100, address, "", 0, paladeum_change_address, token_change_address)
        n0.removetagfromaddress(tag, paladeum_change_address)
        n0.generate(1)

        # do the transfer already!
        txid = n0.transfer(token_name, 100, address)
        n0.generate(1)
        assert_equal(64, len(txid[0]))
        assert_equal(100, n0.listtokenbalancesbyaddress(address)[token_name])

        # do another transfer with specified, tagged token change address
        token_change_address = n0.getnewaddress()
        n0.addtagtoaddress(tag, token_change_address)
        n0.generate(1)
        txid = n0.transfer(token_name, 1, issue_address, "", 0, "", token_change_address)
        n0.generate(1)
        assert_equal(64, len(txid[0]))
        assert (n0.listtokenbalancesbyaddress(token_change_address)[token_name] > 0)

        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.removetagfromaddress, tag, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.removetagfromaddress, tag, address, "garbagechangeaddress")

        n0.removetagfromaddress(tag, address, change_address)
        n0.generate(1)

        assert_raises_rpc_error(-32600, "removing-qualifier-when-not-assigned", n0.removetagfromaddress, tag, address, change_address)

        # TODO: test without specifying change address when there are no valid change addresses (all untagged)

        # post-untagging verification
        assert_does_not_contain(address, n0.listaddressesfortag(tag))
        assert_does_not_contain(tag, n0.listtagsforaddress(address))
        assert not n0.checkaddresstag(address, tag)

        # viewmytaggedaddresses
        tagged = viewmytaggedaddresses()
        assert_equal(6, len(tagged))  # includes removed
        assert_contains(issue_address, list(map(lambda x: x['Address'], tagged)))
        assert_contains(address, list(map(lambda x: x['Address'], tagged)))
        for t in tagged:
            assert_equal(tag, t['Tag Name'])
            if issue_address == t['Address']:
                assert_happening(t['Assigned'])
            if address == t['Address']:
                assert_happening(t['Removed'])

        assert_raises_rpc_error(-8, "bad-txns-null-verifier-address-failed-verification", n0.transfer, token_name, 100, address)

    def freezing(self):
        self.log.info("Testing freezing...")
        n0, n1 = self.nodes[0], self.nodes[1]

        base_token_name = "FROZEN_TM"
        token_name = f"${base_token_name}"
        qty = 11000
        verifier = "true"
        address = n0.getnewaddress()
        safe_address = n0.getnewaddress()
        paladeum_change_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        n0.issuerestrictedtoken(token_name, qty, verifier, address)
        n0.generate(1)

        # squirrel some away
        change_address = n0.getnewaddress()
        n0.transferfromaddress(token_name, address, 1000, safe_address, "", "", "", change_address)
        n0.generate(1)
        address = change_address

        # pre-freezing verification
        assert_does_not_contain(token_name, n0.listaddressrestrictions(address))
        assert not n0.checkaddressrestriction(address, address)
        assert_equal(10000, n0.listtokenbalancesbyaddress(address)[token_name])

        # Isolate this test from other freezing on n0...
        def viewmyrestrictedaddresses():
            return list(filter(lambda x: token_name == x['Token Name'], n0.viewmyrestrictedaddresses()))

        # viewmyrestrictedaddresses
        assert_equal(0, len(viewmyrestrictedaddresses()))

        change_address = n0.getnewaddress()
        n0.transferfromaddress(token_name, address, 1000, n1.getnewaddress(), "", "", "", change_address)
        n0.generate(1)
        self.sync_all()
        assert_equal(9000, n0.listtokenbalancesbyaddress(change_address)[token_name])
        assert_equal(1000, n1.listmytokens()[token_name])
        address = change_address  # tokens have moved

        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.freezeaddress, token_name, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.freezeaddress, token_name, address, "garbagechangeaddress")

        n0.freezeaddress(token_name, address, paladeum_change_address)
        n0.generate(1)

        assert_raises_rpc_error(-32600, "freeze-address-when-already-frozen", n0.freezeaddress, token_name, address, paladeum_change_address)

        # post-freezing verification
        assert_contains(token_name, n0.listaddressrestrictions(address))
        assert n0.checkaddressrestriction(address, token_name)

        # viewmyrestrictedaddresses
        restrictions = viewmyrestrictedaddresses()
        assert_equal(1, len(restrictions))
        r = restrictions[0]
        assert_equal(address, r['Address'])
        assert_equal(token_name, r['Token Name'])
        assert_happening(r['Restricted'])

        assert_raises_rpc_error(-8, "No token outpoints are selected from the given address", n0.transferfromaddress, token_name, address, 1000, n1.getnewaddress())

        assert_raises_rpc_error(None, "Invalid Paladeum address", n0.unfreezeaddress, token_name, "garbageaddress")
        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.unfreezeaddress, token_name, address, "garbagechangeaddress")

        n0.unfreezeaddress(token_name, address, paladeum_change_address)
        n0.generate(1)

        assert_raises_rpc_error(-32600, "unfreeze-address-when-not-frozen", n0.unfreezeaddress, token_name, address, paladeum_change_address)

        # post-unfreezing verification
        assert_does_not_contain(token_name, n0.listaddressrestrictions(address))
        assert not n0.checkaddressrestriction(address, token_name)
        assert_equal(9000, n0.listtokenbalancesbyaddress(address)[token_name])

        # viewmyrestrictedaddresses
        restrictions = viewmyrestrictedaddresses()
        assert_equal(1, len(restrictions))
        r = restrictions[0]
        assert_equal(address, r['Address'])
        assert_equal(token_name, r['Token Name'])
        assert_happening(r['Derestricted'])

        change_address = n0.getnewaddress()
        n0.transferfromaddress(token_name, address, 1000, n1.getnewaddress(), "", "", "", change_address)
        n0.generate(1)
        self.sync_all()
        assert_equal(8000, n0.listtokenbalancesbyaddress(change_address)[token_name])
        assert_equal(2000, n1.listmytokens()[token_name])

    def global_freezing(self):
        self.log.info("Testing global freezing...")
        n0, n1 = self.nodes[0], self.nodes[1]

        base_token_name = "FROZEN_GLOBAL"
        token_name = f"${base_token_name}"
        qty = 10000
        verifier = "true"
        address = n0.getnewaddress()
        paladeum_change_address = n0.getnewaddress()

        n0.issue(base_token_name)
        n0.generate(1)

        n0.issuerestrictedtoken(token_name, qty, verifier, address)
        n0.generate(1)

        # pre-freeze validation
        assert_does_not_contain(token_name, n0.listglobalrestrictions())
        assert not n0.checkglobalrestriction(token_name)
        assert_equal(10000, n0.listtokenbalancesbyaddress(address)[token_name])
        change_address = n0.getnewaddress()
        n0.transferfromaddress(token_name, address, 1000, n1.getnewaddress(), "", "", "", change_address)
        n0.generate(1)
        self.sync_all()
        assert_equal(9000, n0.listtokenbalancesbyaddress(change_address)[token_name])
        assert_equal(1000, n1.listmytokens()[token_name])
        address = change_address  # tokens have moved

        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.freezerestrictedtoken, token_name, "garbagechangeaddress")

        n0.freezerestrictedtoken(token_name, paladeum_change_address)  # Can only freeze once!
        assert_raises_rpc_error(-26, "Freezing transaction already in mempool", n0.freezerestrictedtoken, token_name, paladeum_change_address)
        n0.generate(1)
        assert_raises_rpc_error(None, "global-freeze-when-already-frozen", n0.freezerestrictedtoken, token_name, paladeum_change_address)

        # post-freeze validation
        assert_contains(token_name, n0.listglobalrestrictions())
        assert n0.checkglobalrestriction(token_name)
        assert_raises_rpc_error(-8, "restricted token has been globally frozen", n0.transferfromaddress, token_name, address, 1000, n1.getnewaddress())
        assert_raises_rpc_error(None, "Invalid Paladeum change address", n0.unfreezerestrictedtoken, token_name, "garbagechangeaddress")

        n0.unfreezerestrictedtoken(token_name, paladeum_change_address)  # Can only un-freeze once!
        assert_raises_rpc_error(-26, "Unfreezing transaction already in mempool", n0.unfreezerestrictedtoken, token_name, paladeum_change_address)
        n0.generate(1)
        assert_raises_rpc_error(None, "global-unfreeze-when-not-frozen", n0.unfreezerestrictedtoken, token_name, paladeum_change_address)

        # post-unfreeze validation
        assert_does_not_contain(token_name, n0.listglobalrestrictions())
        assert not n0.checkglobalrestriction(token_name)
        assert_equal(9000, n0.listtokenbalancesbyaddress(address)[token_name])
        change_address = n0.getnewaddress()
        n0.transferfromaddress(token_name, address, 1000, n1.getnewaddress(), "", "", "", change_address)
        n0.generate(1)
        self.sync_all()
        assert_equal(8000, n0.listtokenbalancesbyaddress(change_address)[token_name])
        assert_equal(2000, n1.listmytokens()[token_name])

    def isvalidverifierstring(self):
        self.log.info("Testing isvalidverifierstring()...")
        n0 = self.nodes[0]

        n0.issuequalifiertoken("#KYC1")
        n0.issuequalifiertoken("#KYC2")
        n0.generate(1)

        valid = [
            "true",
            "#KYC1",
            "#KYC2",
            "#KYC1 & #KYC2"
        ]
        for s in valid:
            assert_equal("Valid Verifier", n0.isvalidverifierstring(s))

        invalid_empty = [
            "",
            "    "
        ]
        for s in invalid_empty:
            assert_raises_rpc_error(-8, "Verifier string can not be empty", n0.isvalidverifierstring, s)

        invalid_syntax = [
            "asdf",
            "#KYC1 - #KYC2"
        ]
        for s in invalid_syntax:
            assert_raises_rpc_error(-8, "failed-syntax", n0.isvalidverifierstring, s)

        invalid_non_issued = ["#NOPE"]
        for s in invalid_non_issued:
            assert_raises_rpc_error(-8, "contains-non-issued-qualifier", n0.isvalidverifierstring, s)

    def run_test(self):
        self.activate_restricted_tokens()

        self.issuerestrictedtoken()
        self.issuerestrictedtoken_full()
        self.reissuerestrictedtoken_full()
        self.issuequalifiertoken()
        self.issue_qualifier_token_full()
        self.transferqualifier()
        self.tagging()
        self.freezing()
        self.global_freezing()
        self.isvalidverifierstring()


if __name__ == '__main__':
    RestrictedTokensTest().main()
